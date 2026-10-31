// src/handlers/obj_handle.c - 对象相关 HTTP 处理器
#include "cJSON.h"
#include "error_response.h"
#include "handle_utils.h"
#include "lmjcore.h"
#include "nested_value.h"
#include "router.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==================== 对象处理器 ====================

int handle_obj_create(void *params, void *cbdata) {
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

  // 创建对象
  lmjcore_ptr obj_ptr;
  rc = lmjcore_obj_create(txn, obj_ptr);
  if (rc != LMJCORE_SUCCESS) {
    rc = json_response_lmjcore_error(response, rc);
    goto cleanup;
  }

  TXN_COMMIT(txn, txn_owned, response, cleanup);

  // 将指针转换为字符串
  char ptr_str[LMJCORE_PTR_STRING_LEN + 1];
  lmjcore_ptr_to_string(obj_ptr, ptr_str, sizeof(ptr_str));

  // 构建响应
  rc = json_response_set(response, HTTP_STATUS_CREATED, json_new_ptr(ptr_str));

cleanup:
  TXN_ABORT(txn, txn_owned);
  return rc;
}

int handle_obj_get(void *params, void *cbdata) {
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
  lmjcore_ptr obj_ptr;
  if (lmjcore_ptr_from_string(ptr_str, obj_ptr) != LMJCORE_SUCCESS) {
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
  int exists = lmjcore_entity_exist(txn, obj_ptr);
  if (exists != 1) {
    rc = json_response_error(response, HTTP_STATUS_NOT_FOUND, "Object not found");
    goto cleanup;
  }

  // 分配缓冲区读取对象内容
  size_t total_value_len = 0;
  size_t total_value_count = 0;
  size_t total_member_len = 0;
  size_t member_count = 0;

  lmjcore_obj_stat_values(txn, obj_ptr, &total_value_len, &total_value_count);
  lmjcore_obj_stat_members(txn, obj_ptr, &total_member_len, &member_count);

  // 估算缓冲区大小
  size_t buf_size = sizeof(lmjcore_result_obj) +
                    member_count * sizeof(lmjcore_member_descriptor) +
                    total_member_len + total_value_len + 1024;

  result_buf = (uint8_t *)malloc(buf_size);
  if (!result_buf) {
    rc = json_response_error(response, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                             "Failed to allocate memory");
    goto cleanup;
  }

  lmjcore_result_obj *result_head = NULL;
  rc = lmjcore_obj_get(txn, obj_ptr, result_buf, buf_size, &result_head);
  if (rc != LMJCORE_SUCCESS) {
    rc = json_response_lmjcore_error(response, rc);
    goto cleanup;
  }

  // 构建 JSON 响应：{"ptr":"...","members":[{"name","value","type"}...],"count":N}
  char ptr_out[LMJCORE_PTR_STRING_LEN + 1];
  lmjcore_ptr_to_string(obj_ptr, ptr_out, sizeof(ptr_out));

  cJSON *members = NULL;
  root = json_new_entity(ptr_out, "members", &members);
  if (!root) {
    rc = json_response_error(response, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                             "Failed to allocate memory");
    goto cleanup;
  }

  size_t built_count = 0;
  for (size_t i = 0; i < result_head->member_count; i++) {
    lmjcore_member_descriptor *desc = &result_head->members[i];

    // 结果缓冲区中的成员名不带结尾符，需按长度复制后再交给 cJSON
    size_t name_len = desc->member_name.value_len;
    if (name_len > LMJCORE_MAX_MEMBER_NAME_LEN) {
      name_len = LMJCORE_MAX_MEMBER_NAME_LEN;
    }
    char member_name[LMJCORE_MAX_MEMBER_NAME_LEN + 1];
    memcpy(member_name, result_buf + desc->member_name.value_offset, name_len);
    member_name[name_len] = '\0';

    // 解码成员值
    char *value_str = NULL;
    api_value_type_t value_type = VALUE_TYPE_NULL;
    const uint8_t *value_data =
        (const uint8_t *)(result_buf + desc->member_value.value_offset);

    if (lmjcore_decode_value(value_data, desc->member_value.value_len,
                             &value_str, &value_type) != LMJCORE_SUCCESS) {
      continue; // 无法解码的成员跳过，保证响应始终为合法 JSON
    }

    cJSON *item = json_new_value_entry("name", member_name, value_str,
                                       value_type_to_string(value_type));
    free(value_str);

    if (!json_array_append(members, item)) {
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

int handle_obj_member_get(void *params, void *cbdata) {
  handle_params_t *hp = (handle_params_t *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 获取参数：ptr 和 member_name
  const char *ptr_str = route_params_get(hp->params, 0);
  const char *member_name_encoded = route_params_get(hp->params, 1);

  if (!ptr_str || !member_name_encoded) {
    RETURN_ERROR_MISSING_PARAM("ptr or member", response);
  }

  // URL 解码成员名
  char member_name[512];
  size_t member_name_len = strlen(member_name_encoded);
  if (url_decode(member_name_encoded, member_name_len, member_name,
                 sizeof(member_name)) < 0) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 转换指针
  lmjcore_ptr obj_ptr;
  if (lmjcore_ptr_from_string(ptr_str, obj_ptr) != LMJCORE_SUCCESS) {
    RETURN_ERROR_INVALID_PTR(response);
  }

  lmjcore_txn *txn = NULL;
  int txn_owned = 0;
  int rc = -1;
  uint8_t *value_buf = NULL;
  char *value_str = NULL;
  cJSON *root = NULL;

  TXN_BEGIN(txn, hp, LMJCORE_TXN_READONLY, txn_owned, response, cleanup);
  TXN_CHECK_TIMEOUT(hp, txn, txn_owned, response, cleanup);

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, obj_ptr);
  if (exists != 1) {
    rc = json_response_error(response, HTTP_STATUS_NOT_FOUND, "Object not found");
    goto cleanup;
  }

  // 读取成员值：缓冲区按需增长，超过上限返回 413（不再固定 4096 截断）
  size_t value_len = 0;
  rc = lmjcore_obj_member_get_capped(txn, obj_ptr, member_name,
                                     strlen(member_name), hp->max_value_bytes,
                                     &value_buf, &value_len);
  if (rc == LMJCORE_ERROR_MEMBER_NOT_FOUND) {
    rc = json_response_error(response, HTTP_STATUS_NOT_FOUND, "Member not found");
    goto cleanup;
  }
  if (rc == LMJCORE_ERROR_VALUE_TOO_LARGE) {
    size_t limit = hp->max_value_bytes ? hp->max_value_bytes
                                       : (size_t)HANDLE_DEFAULT_MAX_VALUE_BYTES;
    rc = json_response_value_too_large(response, limit);
    goto cleanup;
  }
  if (rc != LMJCORE_SUCCESS) {
    rc = json_response_lmjcore_error(response, rc);
    goto cleanup;
  }

  // 解码值
  api_value_type_t value_type = VALUE_TYPE_NULL;
  rc = lmjcore_decode_value(value_buf, value_len, &value_str, &value_type);
  if (rc != LMJCORE_SUCCESS) {
    rc = json_response_lmjcore_error(response, rc);
    goto cleanup;
  }

  // 构建响应：{"member":"...","value":"...","type":"..."}
  root = json_new_value_entry("member", member_name, value_str,
                              value_type_to_string(value_type));
  if (!root) {
    rc = json_response_error(response, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                             "Failed to allocate memory");
    goto cleanup;
  }

  rc = json_response_set(response, HTTP_STATUS_OK, root);
  root = NULL; // 所有权已转移给响应

cleanup:
  TXN_ABORT(txn, txn_owned);
  free(value_buf);
  free(value_str);
  cJSON_Delete(root);
  return rc;
}

int handle_obj_member_put(void *params, void *cbdata) {
  handle_params_t *hp = (handle_params_t *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 获取参数：ptr 和 member_name
  const char *ptr_str = route_params_get(hp->params, 0);
  const char *member_name_encoded = route_params_get(hp->params, 1);

  if (!ptr_str || !member_name_encoded) {
    RETURN_ERROR_MISSING_PARAM("ptr or member", response);
  }

  // URL 解码成员名
  char member_name[512];
  size_t member_name_len = strlen(member_name_encoded);
  if (url_decode(member_name_encoded, member_name_len, member_name,
                 sizeof(member_name)) < 0) {
    RETURN_ERROR_INVALID_PARAM(response);
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
  lmjcore_ptr obj_ptr;
  if (lmjcore_ptr_from_string(ptr_str, obj_ptr) != LMJCORE_SUCCESS) {
    rc = json_response_error(response, HTTP_STATUS_BAD_REQUEST,
                             "Invalid pointer format");
    goto cleanup;
  }

  TXN_BEGIN(txn, hp, 0, txn_owned, response, cleanup);
  TXN_CHECK_TIMEOUT(hp, txn, txn_owned, response, cleanup);

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, obj_ptr);
  if (exists != 1) {
    rc = json_response_error(response, HTTP_STATUS_NOT_FOUND, "Object not found");
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

  // 设置成员值（使用解码后的成员名）
  rc = lmjcore_obj_member_put(txn, obj_ptr, (const uint8_t *)member_name,
                              strlen(member_name), encoded_value, encoded_len);
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

int handle_obj_member_del(void *params, void *cbdata) {
  handle_params_t *hp = (handle_params_t *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 获取参数：ptr 和 member_name
  const char *ptr_str = route_params_get(hp->params, 0);
  const char *member_name_encoded = route_params_get(hp->params, 1);

  if (!ptr_str || !member_name_encoded) {
    RETURN_ERROR_MISSING_PARAM("ptr or member", response);
  }

  // URL 解码成员名
  char member_name[512];
  size_t member_name_len = strlen(member_name_encoded);
  if (url_decode(member_name_encoded, member_name_len, member_name,
                 sizeof(member_name)) < 0) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 转换指针
  lmjcore_ptr obj_ptr;
  if (lmjcore_ptr_from_string(ptr_str, obj_ptr) != LMJCORE_SUCCESS) {
    RETURN_ERROR_INVALID_PTR(response);
  }

  lmjcore_txn *txn = NULL;
  int txn_owned = 0;
  int rc = -1;

  TXN_BEGIN(txn, hp, 0, txn_owned, response, cleanup);
  TXN_CHECK_TIMEOUT(hp, txn, txn_owned, response, cleanup);

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, obj_ptr);
  if (exists != 1) {
    rc = json_response_error(response, HTTP_STATUS_NOT_FOUND, "Object not found");
    goto cleanup;
  }

  // 删除成员（使用解码后的成员名）
  rc = lmjcore_obj_member_del(txn, obj_ptr, (const uint8_t *)member_name,
                              strlen(member_name));
  if (rc == LMJCORE_ERROR_MEMBER_NOT_FOUND) {
    rc = json_response_error(response, HTTP_STATUS_NOT_FOUND, "Member not found");
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
  return rc;
}

int handle_obj_del(void *params, void *cbdata) {
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
  lmjcore_ptr obj_ptr;
  if (lmjcore_ptr_from_string(ptr_str, obj_ptr) != LMJCORE_SUCCESS) {
    RETURN_ERROR_INVALID_PTR(response);
  }

  lmjcore_txn *txn = NULL;
  int txn_owned = 0;
  int rc = -1;

  TXN_BEGIN(txn, hp, 0, txn_owned, response, cleanup);
  TXN_CHECK_TIMEOUT(hp, txn, txn_owned, response, cleanup);

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, obj_ptr);
  if (exists != 1) {
    rc = json_response_error(response, HTTP_STATUS_NOT_FOUND, "Object not found");
    goto cleanup;
  }

  // 删除对象
  rc = lmjcore_obj_del(txn, obj_ptr);
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

// ==================== 对象初始化处理器（原子创建 + 嵌套成员） ====================

/**
 * @brief POST /obj/init - 在同一事务内创建对象并填充成员值（支持嵌套创建）
 *
 * 请求体直接为 JSON 对象：{ "<成员名>": <任意 JSON 值>, ... }
 * 成员值由嵌套工具统一处理（不做类型推断）：
 *   - JSON object -> 同一事务内创建嵌套 obj（存 REF）
 *   - JSON array  -> 同一事务内创建嵌套 set（存 REF）
 *   - JSON null   -> null
 *   - JSON string -> raw（原样存储，即使内容形似指针）
 *   - JSON number/boolean -> raw（其 JSON 文本）
 *
 * 成功：HTTP 201，返回 {"ptr":"...","member_count":N}
 * 失败：自动回滚并返回失败原因 {"error":"..."}（含逐层路径）；
 *       若运行于批量(batch)共享事务中，不自行提交/回滚，由调用方统一处理
 */
int handle_obj_init(void *params, void *cbdata) {
  handle_params_t *hp = (handle_params_t *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 解析请求体：必须为 JSON 对象（成员映射）
  cJSON *root = hp->body ? cJSON_ParseWithOpts(hp->body, NULL, 0) : NULL;
  lmjcore_txn *txn = NULL;
  int txn_owned = 0;
  int rc = -1;

  if (!root) {
    return json_response_error(response, HTTP_STATUS_BAD_REQUEST,
                               "Invalid JSON in request body");
  }

  if (!cJSON_IsObject(root)) {
    rc = json_response_error(
        response, HTTP_STATUS_BAD_REQUEST,
        "Request body must be a JSON object (member map)");
    goto cleanup;
  }

  TXN_BEGIN(txn, hp, 0, txn_owned, response, cleanup);
  TXN_CHECK_TIMEOUT(hp, txn, txn_owned, response, cleanup);

  // 1. 创建根对象
  lmjcore_ptr obj_ptr;
  rc = lmjcore_obj_create(txn, obj_ptr);
  if (rc != LMJCORE_SUCCESS) {
    rc = json_response_lmjcore_error(response, rc);
    goto cleanup;
  }

  // 2. 逐成员写入（值可为任意 JSON，嵌套对象/数组由工具自动创建）
  char err[1024];
  char reason[1152];
  int filled = 0;
  const cJSON *kv = NULL;
  for (kv = root->child; kv; kv = kv->next) {
    const char *name = kv->string ? kv->string : "";
    size_t name_len = strlen(name);

    // 成员名校验
    rc = lmjcore_nested_member_name_check(name, name_len, err, sizeof(err));
    if (rc != LMJCORE_SUCCESS) {
      snprintf(reason, sizeof(reason), "%s (in member '%.*s')", err,
               (int)name_len, name);
      rc = json_response_error(response, lmjcore_error_to_http_status(rc),
                               "%s", reason);
      goto cleanup;
    }

    // 递归编码成员值（标量或嵌套创建）
    uint8_t *enc = NULL;
    size_t enc_len = 0;
    rc = lmjcore_json_encode_value(txn, kv, 0, err, sizeof(err), &enc,
                                   &enc_len);
    if (rc != LMJCORE_SUCCESS) {
      snprintf(reason, sizeof(reason), "%s (in member '%.*s')", err,
               (int)name_len, name);
      rc = json_response_error(response, lmjcore_error_to_http_status(rc),
                               "%s", reason);
      goto cleanup;
    }

    // 写入成员值
    rc = lmjcore_obj_member_put(txn, obj_ptr, (const uint8_t *)name, name_len,
                                enc, enc_len);
    free(enc);
    if (rc != LMJCORE_SUCCESS) {
      snprintf(reason, sizeof(reason), "%s (in member '%.*s')",
               lmjcore_strerror(rc), (int)name_len, name);
      rc = json_response_error(response, lmjcore_error_to_http_status(rc),
                               "%s", reason);
      goto cleanup;
    }

    filled++;
  }

  TXN_COMMIT(txn, txn_owned, response, cleanup);

  // 将指针转换为字符串并构建响应
  char ptr_str[LMJCORE_PTR_STRING_LEN + 1];
  lmjcore_ptr_to_string(obj_ptr, ptr_str, sizeof(ptr_str));

  rc = json_response_set(
      response, HTTP_STATUS_CREATED,
      json_new_counted_ptr(ptr_str, "member_count", (size_t)filled));

cleanup:
  TXN_ABORT(txn, txn_owned);
  cJSON_Delete(root);
  return rc;
}
