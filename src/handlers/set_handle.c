// src/handlers/set_handle.c - 集合相关 HTTP 处理器
#include "cJSON.h"
#include "error_response.h"
#include "handle_utils.h"
#include "lmjcore.h"
#include "nested_value.h"
#include "router.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==================== 集合处理器 ====================

int handle_set_create(void *params, void *cbdata) {
  handle_params_t *hp = (handle_params_t *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  lmjcore_txn *txn = NULL;
  int txn_owned = 0;
  int rc = -1;

  TXN_BEGIN(txn, hp, 0, txn_owned, response, cleanup);
  TXN_CHECK_TIMEOUT(hp, txn, txn_owned, response, cleanup);

  // 创建集合
  lmjcore_ptr set_ptr;
  rc = lmjcore_set_create(txn, set_ptr);
  if (rc != LMJCORE_SUCCESS) {
    rc = json_response_lmjcore_error(response, rc);
    goto cleanup;
  }

  TXN_COMMIT(txn, txn_owned, response, cleanup);

  // 将指针转换为字符串
  char ptr_str[LMJCORE_PTR_STRING_LEN + 1];
  lmjcore_ptr_to_string(set_ptr, ptr_str, sizeof(ptr_str));

  // 构建响应
  rc = json_response_set(response, HTTP_STATUS_CREATED, json_new_ptr(ptr_str));

cleanup:
  TXN_ABORT(txn, txn_owned);
  return rc;
}

int handle_set_get(void *params, void *cbdata) {
  handle_params_t *hp = (handle_params_t *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 获取指针参数
  const char *ptr_str = route_params_get(hp->params, 0);
  if (!ptr_str) {
    RETURN_ERROR_MISSING_PARAM("ptr", response);
  }

  // 转换指针
  lmjcore_ptr set_ptr;
  if (lmjcore_ptr_from_string(ptr_str, set_ptr) != LMJCORE_SUCCESS) {
    RETURN_ERROR_INVALID_PTR(response);
  }

  lmjcore_txn *txn = NULL;
  int txn_owned = 0;
  int rc = -1;
  uint8_t *result_buf = NULL;
  cJSON *root = NULL;

  TXN_BEGIN(txn, hp, LMJCORE_TXN_READONLY, txn_owned, response, cleanup);
  TXN_CHECK_TIMEOUT(hp, txn, txn_owned, response, cleanup);

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, set_ptr);
  if (exists != 1) {
    rc = json_response_error(response, HTTP_STATUS_NOT_FOUND, "Set not found");
    goto cleanup;
  }

  // 统计集合大小
  size_t total_value_len = 0;
  size_t element_count = 0;
  lmjcore_set_stat(txn, set_ptr, &total_value_len, &element_count);

  // 分配缓冲区
  size_t buf_size = sizeof(lmjcore_result_set) +
                    element_count * sizeof(lmjcore_descriptor) +
                    total_value_len + 1024;

  result_buf = (uint8_t *)malloc(buf_size);
  if (!result_buf) {
    rc = json_response_error(response, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                             "Failed to allocate memory");
    goto cleanup;
  }

  lmjcore_result_set *result_head = NULL;
  rc = lmjcore_set_get(txn, set_ptr, result_buf, buf_size, &result_head);
  if (rc != LMJCORE_SUCCESS) {
    rc = json_response_lmjcore_error(response, rc);
    goto cleanup;
  }

  // 构建 JSON 响应：{"ptr":"...","elements":[{"value","type"}...],"count":N}
  char ptr_out[LMJCORE_PTR_STRING_LEN + 1];
  lmjcore_ptr_to_string(set_ptr, ptr_out, sizeof(ptr_out));

  cJSON *elements = NULL;
  root = json_new_entity(ptr_out, "elements", &elements);
  if (!root) {
    rc = json_response_error(response, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                             "Failed to allocate memory");
    goto cleanup;
  }

  size_t built_count = 0;
  for (size_t i = 0; i < result_head->element_count; i++) {
    lmjcore_descriptor *desc = &result_head->elements[i];
    const uint8_t *element_value =
        (const uint8_t *)(result_buf + desc->value_offset);

    // 解码元素值
    char *value_str = NULL;
    api_value_type_t value_type = VALUE_TYPE_NULL;
    if (lmjcore_decode_value(element_value, desc->value_len, &value_str,
                             &value_type) != LMJCORE_SUCCESS) {
      continue; // 无法解码的元素跳过，保证响应始终为合法 JSON
    }

    cJSON *item = json_new_value(value_str, value_type_to_string(value_type));
    free(value_str);

    if (!json_array_append(elements, item)) {
      rc = json_response_error(response, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                               "Failed to allocate memory");
      goto cleanup;
    }
    built_count++;
  }

  if (json_entity_set_count(root, built_count) != 0) {
    rc = json_response_error(response, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                             "Failed to allocate memory");
    goto cleanup;
  }

  rc = json_response_set(response, HTTP_STATUS_OK, root);
  root = NULL; // 所有权已转移给响应

cleanup:
  TXN_ABORT(txn, txn_owned);
  cJSON_Delete(root);
  free(result_buf);
  return rc;
}

int handle_set_add(void *params, void *cbdata) {
  handle_params_t *hp = (handle_params_t *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 获取参数
  const char *ptr_str = route_params_get(hp->params, 0);
  if (!ptr_str) {
    RETURN_ERROR_MISSING_PARAM("ptr", response);
  }

  // 解析请求体：{"value": string, "type": "raw"|"ref"|"null"}
  cJSON *body = NULL;
  lmjcore_txn *txn = NULL;
  int txn_owned = 0;
  int rc = -1;
  uint8_t *encoded_value = NULL;

  body = cJSON_Parse(hp->body);
  if (!body) {
    RETURN_ERROR_BODY_PARSE(response);
  }

  const char *value_str = NULL;
  size_t value_len = 0;
  int type = LMJCORE_VALUE_TYPE_RAW;
  if (handle_parse_typed_value(body, &value_str, &value_len, &type) !=
      LMJCORE_SUCCESS) {
    rc = json_response_error(response, HTTP_STATUS_BAD_REQUEST,
                             "Request body must be {\"value\": string, "
                             "\"type\": \"raw\"|\"ref\"|\"null\"}");
    goto cleanup;
  }

  // 转换指针
  lmjcore_ptr set_ptr;
  if (lmjcore_ptr_from_string(ptr_str, set_ptr) != LMJCORE_SUCCESS) {
    rc = json_response_error(response, HTTP_STATUS_BAD_REQUEST,
                             "Invalid pointer format");
    goto cleanup;
  }

  TXN_BEGIN(txn, hp, 0, txn_owned, response, cleanup);
  TXN_CHECK_TIMEOUT(hp, txn, txn_owned, response, cleanup);

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, set_ptr);
  if (exists != 1) {
    rc = json_response_error(response, HTTP_STATUS_NOT_FOUND, "Set not found");
    goto cleanup;
  }

  // 编码值
  size_t encoded_size = 1 + LMJCORE_PTR_LEN + value_len + 16;
  encoded_value = (uint8_t *)malloc(encoded_size);
  if (!encoded_value) {
    rc = json_response_error(response, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                             "Failed to allocate memory");
    goto cleanup;
  }

  size_t encoded_len = 0;
  rc = lmjcore_encode_value(value_str, value_len, type, encoded_value,
                            encoded_size, &encoded_len);
  if (rc != LMJCORE_SUCCESS) {
    rc = json_response_lmjcore_error(response, rc);
    goto cleanup;
  }

  // 添加元素
  rc = lmjcore_set_add(txn, set_ptr, encoded_value, encoded_len);
  if (rc == LMJCORE_ERROR_MEMBER_EXISTS) {
    rc = json_response_error(response, HTTP_STATUS_CONFLICT,
                             "Element already exists");
    goto cleanup;
  }
  if (rc != LMJCORE_SUCCESS) {
    rc = json_response_lmjcore_error(response, rc);
    goto cleanup;
  }

  TXN_COMMIT(txn, txn_owned, response, cleanup);

  rc = json_response_success(response, HTTP_STATUS_OK);

cleanup:
  TXN_ABORT(txn, txn_owned);
  free(encoded_value);
  cJSON_Delete(body);
  return rc;
}

int handle_set_remove(void *params, void *cbdata) {
  handle_params_t *hp = (handle_params_t *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 获取参数
  const char *ptr_str = route_params_get(hp->params, 0);
  if (!ptr_str) {
    RETURN_ERROR_MISSING_PARAM("ptr", response);
  }

  // 解析请求体：{"value": string, "type": "raw"|"ref"|"null"}
  cJSON *body = NULL;
  lmjcore_txn *txn = NULL;
  int txn_owned = 0;
  int rc = -1;
  uint8_t *encoded_value = NULL;

  body = cJSON_Parse(hp->body);
  if (!body) {
    RETURN_ERROR_BODY_PARSE(response);
  }

  const char *value_str = NULL;
  size_t value_len = 0;
  int type = LMJCORE_VALUE_TYPE_RAW;
  if (handle_parse_typed_value(body, &value_str, &value_len, &type) !=
      LMJCORE_SUCCESS) {
    rc = json_response_error(response, HTTP_STATUS_BAD_REQUEST,
                             "Request body must be {\"value\": string, "
                             "\"type\": \"raw\"|\"ref\"|\"null\"}");
    goto cleanup;
  }

  // 转换指针
  lmjcore_ptr set_ptr;
  if (lmjcore_ptr_from_string(ptr_str, set_ptr) != LMJCORE_SUCCESS) {
    rc = json_response_error(response, HTTP_STATUS_BAD_REQUEST,
                             "Invalid pointer format");
    goto cleanup;
  }

  TXN_BEGIN(txn, hp, 0, txn_owned, response, cleanup);
  TXN_CHECK_TIMEOUT(hp, txn, txn_owned, response, cleanup);

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, set_ptr);
  if (exists != 1) {
    rc = json_response_error(response, HTTP_STATUS_NOT_FOUND, "Set not found");
    goto cleanup;
  }

  // 编码值
  size_t encoded_size = 1 + LMJCORE_PTR_LEN + value_len + 16;
  encoded_value = (uint8_t *)malloc(encoded_size);
  if (!encoded_value) {
    rc = json_response_error(response, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                             "Failed to allocate memory");
    goto cleanup;
  }

  size_t encoded_len = 0;
  rc = lmjcore_encode_value(value_str, value_len, type, encoded_value,
                            encoded_size, &encoded_len);
  if (rc != LMJCORE_SUCCESS) {
    rc = json_response_lmjcore_error(response, rc);
    goto cleanup;
  }

  // 删除元素
  rc = lmjcore_set_remove(txn, set_ptr, encoded_value, encoded_len);
  if (rc != LMJCORE_SUCCESS) {
    rc = json_response_lmjcore_error(response, rc);
    goto cleanup;
  }

  TXN_COMMIT(txn, txn_owned, response, cleanup);

  rc = json_response_success(response, HTTP_STATUS_OK);

cleanup:
  TXN_ABORT(txn, txn_owned);
  free(encoded_value);
  cJSON_Delete(body);
  return rc;
}

int handle_set_del(void *params, void *cbdata) {
  handle_params_t *hp = (handle_params_t *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 获取指针参数
  const char *ptr_str = route_params_get(hp->params, 0);
  if (!ptr_str) {
    RETURN_ERROR_MISSING_PARAM("ptr", response);
  }

  // 转换指针
  lmjcore_ptr set_ptr;
  if (lmjcore_ptr_from_string(ptr_str, set_ptr) != LMJCORE_SUCCESS) {
    RETURN_ERROR_INVALID_PTR(response);
  }

  lmjcore_txn *txn = NULL;
  int txn_owned = 0;
  int rc = -1;

  TXN_BEGIN(txn, hp, 0, txn_owned, response, cleanup);
  TXN_CHECK_TIMEOUT(hp, txn, txn_owned, response, cleanup);

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, set_ptr);
  if (exists != 1) {
    rc = json_response_error(response, HTTP_STATUS_NOT_FOUND, "Set not found");
    goto cleanup;
  }

  // 删除集合
  rc = lmjcore_set_del(txn, set_ptr);
  if (rc != LMJCORE_SUCCESS) {
    rc = json_response_lmjcore_error(response, rc);
    goto cleanup;
  }

  TXN_COMMIT(txn, txn_owned, response, cleanup);

  rc = json_response_success(response, HTTP_STATUS_OK);

cleanup:
  TXN_ABORT(txn, txn_owned);
  return rc;
}

// ==================== 集合初始化处理器（原子创建 + 嵌套元素） ====================

/**
 * @brief POST /set/init - 在同一事务内创建集合并添加元素值（支持嵌套创建）
 *
 * 请求体直接为 JSON 数组：[ <任意 JSON 值>, ... ]
 * 元素值由嵌套工具统一处理（不做类型推断）：
 *   - JSON object -> 同一事务内创建嵌套 obj（存 REF）
 *   - JSON array  -> 同一事务内创建嵌套 set（存 REF）
 *   - JSON null   -> null
 *   - JSON string -> raw（原样存储，即使内容形似指针）
 *   - JSON number/boolean -> raw（其 JSON 文本）
 * 集合自动去重：请求内重复元素不报错。
 *
 * 成功：HTTP 201，返回 {"ptr":"...","element_count":N}（去重后真实数量）
 * 失败：自动回滚并返回失败原因 {"error":"..."}（含逐层路径）；
 *       若运行于批量(batch)共享事务中，不自行提交/回滚，由调用方统一处理
 */
int handle_set_init(void *params, void *cbdata) {
  handle_params_t *hp = (handle_params_t *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 解析请求体：必须为 JSON 数组（元素列表）
  cJSON *root = hp->body ? cJSON_ParseWithOpts(hp->body, NULL, 0) : NULL;
  if (!root) {
    return json_response_error(response, HTTP_STATUS_BAD_REQUEST,
                               "Invalid JSON in request body");
  }

  lmjcore_txn *txn = NULL;
  int txn_owned = 0;
  int rc = -1;

  if (!cJSON_IsArray(root)) {
    rc = json_response_error(
        response, HTTP_STATUS_BAD_REQUEST,
        "Request body must be a JSON array (element list)");
    goto cleanup;
  }

  TXN_BEGIN(txn, hp, 0, txn_owned, response, cleanup);
  TXN_CHECK_TIMEOUT(hp, txn, txn_owned, response, cleanup);

  // 1. 创建根集合
  lmjcore_ptr set_ptr;
  rc = lmjcore_set_create(txn, set_ptr);
  if (rc != LMJCORE_SUCCESS) {
    rc = json_response_lmjcore_error(response, rc);
    goto cleanup;
  }

  // 2. 逐个添加元素（值可为任意 JSON，嵌套对象/数组由工具自动创建）
  char err[1024];
  char reason[1152];
  int index = 0;
  const cJSON *item = NULL;
  for (item = root->child; item; item = item->next) {
    uint8_t *enc = NULL;
    size_t enc_len = 0;
    rc = lmjcore_json_encode_value(txn, item, 0, err, sizeof(err), &enc,
                                   &enc_len);
    if (rc != LMJCORE_SUCCESS) {
      snprintf(reason, sizeof(reason), "%s (in element [%d])", err, index);
      rc = json_response_error(response, lmjcore_error_to_http_status(rc),
                               "%s", reason);
      goto cleanup;
    }

    // 添加元素（重复元素自动去重，不报错）
    rc = lmjcore_set_add(txn, set_ptr, enc, enc_len);
    free(enc);
    if (rc == LMJCORE_ERROR_MEMBER_EXISTS) {
      rc = LMJCORE_SUCCESS;
    }
    if (rc != LMJCORE_SUCCESS) {
      snprintf(reason, sizeof(reason), "%s (in element [%d])",
               lmjcore_strerror(rc), index);
      rc = json_response_error(response, lmjcore_error_to_http_status(rc),
                               "%s", reason);
      goto cleanup;
    }
    index++;
  }

  // 3. 统计去重后的真实元素数量（提交前在同一事务内读取）
  size_t distinct_count = 0;
  size_t total_value_len = 0;
  rc = lmjcore_set_stat(txn, set_ptr, &total_value_len, &distinct_count);
  if (rc != LMJCORE_SUCCESS) {
    rc = json_response_lmjcore_error(response, rc);
    goto cleanup;
  }

  TXN_COMMIT(txn, txn_owned, response, cleanup);

  // 将指针转换为字符串并构建响应
  char ptr_str[LMJCORE_PTR_STRING_LEN + 1];
  lmjcore_ptr_to_string(set_ptr, ptr_str, sizeof(ptr_str));

  rc = json_response_set(
      response, HTTP_STATUS_CREATED,
      json_new_counted_ptr(ptr_str, "element_count", distinct_count));

cleanup:
  TXN_ABORT(txn, txn_owned);
  cJSON_Delete(root);
  return rc;
}
