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

  // 检查是否已有事务（批量操作场景）
  lmjcore_txn *txn = NULL;
  int auto_commit = 1;  // 是否自动提交事务

  if (hp->txn && !hp->auto_manage_txn) {
    // 使用已有事务（批量操作场景）
    txn = hp->txn;
    auto_commit = 0;
  } else {
    // 开启写事务
    int rc = lmjcore_txn_begin(hp->env, NULL, 0, &txn);
    if (rc != LMJCORE_SUCCESS || !txn) {
      RETURN_ERROR_TXN_FAILED("begin", response);
    }
  }

  // 检查事务超时
  CHECK_TXN_TIMEOUT(hp, response, txn);

  // 创建集合
  lmjcore_ptr set_ptr;
  int rc = lmjcore_set_create(txn, set_ptr);
  if (rc != LMJCORE_SUCCESS) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    build_lmjcore_error_response(rc, response);
    return -1;
  }

  // 提交事务（仅当自动管理时）
  if (auto_commit) {
    rc = lmjcore_txn_commit(txn);
    if (rc != LMJCORE_SUCCESS) {
      lmjcore_txn_abort(txn);
      RETURN_ERROR_TXN_FAILED("commit", response);
    }
  }

  // 将指针转换为字符串
  char ptr_str[LMJCORE_PTR_STRING_LEN + 1];
  lmjcore_ptr_to_string(set_ptr, ptr_str, sizeof(ptr_str));

  // 构建响应
  char json_buf[512];
  snprintf(json_buf, sizeof(json_buf), "{\"ptr\":\"%s\"}", ptr_str);

  return build_success_response(HTTP_STATUS_CREATED, json_buf, response);
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

  // 检查是否已有事务（批量操作场景）
  lmjcore_txn *txn = NULL;
  int auto_commit = 1;  // 是否自动提交事务

  if (hp->txn && !hp->auto_manage_txn) {
    // 使用已有事务（批量操作场景）
    txn = hp->txn;
    auto_commit = 0;
  } else {
    // 开启读事务
    int rc = lmjcore_txn_begin(hp->env, NULL, LMJCORE_TXN_READONLY, &txn);
    if (rc != LMJCORE_SUCCESS || !txn) {
      RETURN_ERROR_TXN_FAILED("begin", response);
    }
  }

  // 检查事务超时
  CHECK_TXN_TIMEOUT(hp, response, txn);

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, set_ptr);
  if (exists != 1) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    RETURN_ERROR_NOT_FOUND("Set", response);
  }

  // 统计集合大小
  size_t total_value_len = 0;
  size_t element_count = 0;
  lmjcore_set_stat(txn, set_ptr, &total_value_len, &element_count);

  // 分配缓冲区
  size_t buf_size = sizeof(lmjcore_result_set) +
                    element_count * sizeof(lmjcore_descriptor) +
                    total_value_len + 1024;

  uint8_t *result_buf = (uint8_t *)malloc(buf_size);
  if (!result_buf) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    RETURN_ERROR_NO_MEMORY(response);
  }

  lmjcore_result_set *result_head = NULL;
  int rc = lmjcore_set_get(txn, set_ptr, result_buf, buf_size, &result_head);

  // 读事务完成，仅在自动管理时中止
  if (auto_commit) {
    lmjcore_txn_abort(txn);
  }

  if (rc != LMJCORE_SUCCESS) {
    free(result_buf);
    build_lmjcore_error_response(rc, response);
    return -1;
  }

  // 构建 JSON 响应 - 使用动态扩展策略
  size_t json_size = 4096;
  char *json_buf = (char *)malloc(json_size);
  if (!json_buf) {
    free(result_buf);
    RETURN_ERROR_NO_MEMORY(response);
  }

  char ptr_out[LMJCORE_PTR_STRING_LEN + 1];
  lmjcore_ptr_to_string(set_ptr, ptr_out, sizeof(ptr_out));

  int offset =
      snprintf(json_buf, json_size, "{\"ptr\":\"%s\",\"elements\":[", ptr_out);

  // 遍历元素
  for (size_t i = 0; i < result_head->element_count; i++) {
    lmjcore_descriptor *desc = &result_head->elements[i];
    char *element_value = (char *)(result_buf + desc->value_offset);

    // 解码元素值
    char *value_str = NULL;
    api_value_type_t value_type;
    lmjcore_decode_value((uint8_t *)element_value, desc->value_len, &value_str,
                         &value_type);

    const char *type_str = (value_type == VALUE_TYPE_RAW)    ? "raw"
                           : (value_type == VALUE_TYPE_REF)  ? "ref"
                           : (value_type == VALUE_TYPE_SET)  ? "set"
                           : (value_type == VALUE_TYPE_NULL) ? "null"
                                                             : "unknown";

    // 计算所需空间（预留足够余量）
    size_t needed = strlen(value_str ? value_str : "") + 64;
    
    // 检查缓冲区是否需要扩展
    while ((size_t)offset + needed >= json_size) {
      json_size *= 2;
      char *new_buf = (char *)realloc(json_buf, json_size);
      if (!new_buf) {
        free(value_str);
        free(result_buf);
        free(json_buf);
        RETURN_ERROR_NO_MEMORY(response);
      }
      json_buf = new_buf;
    }

    int written = snprintf(json_buf + offset, json_size - offset,
                       "%s{\"value\":\"%s\",\"type\":\"%s\"}", i > 0 ? "," : "",
                       value_str ? value_str : "", type_str);
    
    if (written < 0 || (size_t)written >= json_size - offset) {
      // 缓冲区仍然不够，继续扩展
      json_size *= 2;
      char *new_buf = (char *)realloc(json_buf, json_size);
      if (!new_buf) {
        free(value_str);
        free(result_buf);
        free(json_buf);
        RETURN_ERROR_NO_MEMORY(response);
      }
      json_buf = new_buf;
      written = snprintf(json_buf + offset, json_size - offset,
                       "%s{\"value\":\"%s\",\"type\":\"%s\"}", i > 0 ? "," : "",
                       value_str ? value_str : "", type_str);
    }
    offset += written;

    free(value_str);
  }

  // 确保结尾有足够空间
  while ((size_t)offset + 32 >= json_size) {
    json_size *= 2;
    char *new_buf = (char *)realloc(json_buf, json_size);
    if (!new_buf) {
      free(result_buf);
      free(json_buf);
      RETURN_ERROR_NO_MEMORY(response);
    }
    json_buf = new_buf;
  }
  
  offset += snprintf(json_buf + offset, json_size - offset, "],\"count\":%zu}",
                     result_head->element_count);

  free(result_buf);

  response->status_code = 200;
  response->body = json_buf;
  response->body_len = strlen(json_buf);

  return 0;
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

  // 解析请求体获取 value
  cJSON *body = cJSON_Parse(hp->body);
  if (!body) {
    RETURN_ERROR_BODY_PARSE(response);
  }

  cJSON *value_item = cJSON_GetObjectItemCaseSensitive(body, "value");
  if (!value_item || !cJSON_IsString(value_item)) {
    cJSON_Delete(body);
    RETURN_ERROR_BODY_PARSE(response);
  }

  const char *value_str = value_item->valuestring;
  size_t value_len = strlen(value_str);

  // 转换指针
  lmjcore_ptr set_ptr;
  if (lmjcore_ptr_from_string(ptr_str, set_ptr) != LMJCORE_SUCCESS) {
    cJSON_Delete(body);
    RETURN_ERROR_INVALID_PTR(response);
  }

  // 检查是否已有事务（批量操作场景）
  lmjcore_txn *txn = NULL;
  int auto_commit = 1;  // 是否自动提交事务

  if (hp->txn && !hp->auto_manage_txn) {
    // 使用已有事务（批量操作场景）
    txn = hp->txn;
    auto_commit = 0;
  } else {
    // 开启写事务
    int rc = lmjcore_txn_begin(hp->env, NULL, 0, &txn);
    if (rc != LMJCORE_SUCCESS || !txn) {
      cJSON_Delete(body);
      RETURN_ERROR_TXN_FAILED("begin", response);
    }
  }

  // 检查事务超时
  CHECK_TXN_TIMEOUT(hp, response, txn);

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, set_ptr);
  if (exists != 1) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    cJSON_Delete(body);
    RETURN_ERROR_NOT_FOUND("Set", response);
  }

  // 编码值
  size_t encoded_size = 1 + LMJCORE_PTR_LEN + value_len + 16;
  uint8_t *encoded_value = (uint8_t *)malloc(encoded_size);
  if (!encoded_value) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    cJSON_Delete(body);
    RETURN_ERROR_NO_MEMORY(response);
  }

  size_t encoded_len = 0;
  int rc = lmjcore_encode_value(value_str, value_len, encoded_value, encoded_size,
                            &encoded_len);

  if (rc != LMJCORE_SUCCESS) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    free(encoded_value);
    cJSON_Delete(body);
    build_lmjcore_error_response(rc, response);
    return -1;
  }

  // 添加元素
  rc = lmjcore_set_add(txn, set_ptr, encoded_value, encoded_len);
  free(encoded_value);

  if (rc == LMJCORE_ERROR_MEMBER_EXISTS) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    cJSON_Delete(body);
    return build_error_response(HTTP_STATUS_CONFLICT,
                                "Element already exists", response);
  }

  if (rc != LMJCORE_SUCCESS) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    cJSON_Delete(body);
    build_lmjcore_error_response(rc, response);
    return -1;
  }

  // 提交事务（仅当自动管理时）
  if (auto_commit) {
    rc = lmjcore_txn_commit(txn);
    if (rc != LMJCORE_SUCCESS) {
      lmjcore_txn_abort(txn);
      cJSON_Delete(body);
      RETURN_ERROR_TXN_FAILED("commit", response);
    }
  }

  cJSON_Delete(body);
  return build_success_response(HTTP_STATUS_OK, "{\"success\":true}", response);
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

  // 解析请求体获取 value
  cJSON *body = cJSON_Parse(hp->body);
  if (!body) {
    RETURN_ERROR_BODY_PARSE(response);
  }

  cJSON *value_item = cJSON_GetObjectItemCaseSensitive(body, "value");
  if (!value_item || !cJSON_IsString(value_item)) {
    cJSON_Delete(body);
    RETURN_ERROR_BODY_PARSE(response);
  }

  const char *value_str = value_item->valuestring;
  size_t value_len = strlen(value_str);

  // 转换指针
  lmjcore_ptr set_ptr;
  if (lmjcore_ptr_from_string(ptr_str, set_ptr) != LMJCORE_SUCCESS) {
    cJSON_Delete(body);
    RETURN_ERROR_INVALID_PTR(response);
  }

  // 检查是否已有事务（批量操作场景）
  lmjcore_txn *txn = NULL;
  int auto_commit = 1;  // 是否自动提交事务

  if (hp->txn && !hp->auto_manage_txn) {
    // 使用已有事务（批量操作场景）
    txn = hp->txn;
    auto_commit = 0;
  } else {
    // 开启写事务
    int rc = lmjcore_txn_begin(hp->env, NULL, 0, &txn);
    if (rc != LMJCORE_SUCCESS || !txn) {
      cJSON_Delete(body);
      RETURN_ERROR_TXN_FAILED("begin", response);
    }
  }

  // 检查事务超时
  CHECK_TXN_TIMEOUT(hp, response, txn);

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, set_ptr);
  if (exists != 1) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    cJSON_Delete(body);
    RETURN_ERROR_NOT_FOUND("Set", response);
  }

  // 编码值
  size_t encoded_size = 1 + LMJCORE_PTR_LEN + value_len + 16;
  uint8_t *encoded_value = (uint8_t *)malloc(encoded_size);
  if (!encoded_value) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    cJSON_Delete(body);
    RETURN_ERROR_NO_MEMORY(response);
  }

  size_t encoded_len = 0;
  int rc = lmjcore_encode_value(value_str, value_len, encoded_value, encoded_size,
                            &encoded_len);

  if (rc != LMJCORE_SUCCESS) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    free(encoded_value);
    cJSON_Delete(body);
    build_lmjcore_error_response(rc, response);
    return -1;
  }

  // 删除元素
  rc = lmjcore_set_remove(txn, set_ptr, encoded_value, encoded_len);
  free(encoded_value);

  if (rc != LMJCORE_SUCCESS) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    cJSON_Delete(body);
    build_lmjcore_error_response(rc, response);
    return -1;
  }

  // 提交事务（仅当自动管理时）
  if (auto_commit) {
    rc = lmjcore_txn_commit(txn);
    if (rc != LMJCORE_SUCCESS) {
      lmjcore_txn_abort(txn);
      cJSON_Delete(body);
      RETURN_ERROR_TXN_FAILED("commit", response);
    }
  }

  cJSON_Delete(body);
  return build_success_response(HTTP_STATUS_OK, "{\"success\":true}", response);
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

  // 检查是否已有事务（批量操作场景）
  lmjcore_txn *txn = NULL;
  int auto_commit = 1;  // 是否自动提交事务

  if (hp->txn && !hp->auto_manage_txn) {
    // 使用已有事务（批量操作场景）
    txn = hp->txn;
    auto_commit = 0;
  } else {
    // 开启写事务
    int rc = lmjcore_txn_begin(hp->env, NULL, 0, &txn);
    if (rc != LMJCORE_SUCCESS || !txn) {
      RETURN_ERROR_TXN_FAILED("begin", response);
    }
  }

  // 检查事务超时
  CHECK_TXN_TIMEOUT(hp, response, txn);

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, set_ptr);
  if (exists != 1) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    RETURN_ERROR_NOT_FOUND("Set", response);
  }

  // 删除集合
  int rc = lmjcore_set_del(txn, set_ptr);
  if (rc != LMJCORE_SUCCESS) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    build_lmjcore_error_response(rc, response);
    return -1;
  }

  // 提交事务（仅当自动管理时）
  if (auto_commit) {
    rc = lmjcore_txn_commit(txn);
    if (rc != LMJCORE_SUCCESS) {
      lmjcore_txn_abort(txn);
      RETURN_ERROR_TXN_FAILED("commit", response);
    }
  }

  return build_success_response(HTTP_STATUS_OK, "{\"success\":true}", response);
}

// ==================== 集合初始化处理器（原子创建 + 嵌套元素） ====================

/**
 * @brief POST /set/init - 在同一事务内创建集合并添加元素值（支持嵌套创建）
 *
 * 请求体直接为 JSON 数组：[ <任意 JSON 值>, ... ]
 * 元素值由嵌套工具统一处理：
 *   - JSON object -> 同一事务内创建嵌套 obj（存 REF）
 *   - JSON array  -> 同一事务内创建嵌套 set（存 REF）
 *   - JSON string -> 自动识别：01/02 开头 34 位 -> REF；"" 或 "null" -> null；其余 -> raw
 *   - JSON null   -> null；number/bool -> raw（其 JSON 文本）
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
    return build_error_response(HTTP_STATUS_BAD_REQUEST,
                                "Invalid JSON in request body", response);
  }

  if (!cJSON_IsArray(root)) {
    cJSON_Delete(root);
    return build_error_response(
        HTTP_STATUS_BAD_REQUEST,
        "Request body must be a JSON array (element list)", response);
  }

  // 检查是否已有事务（批量操作场景）
  lmjcore_txn *txn = NULL;
  int auto_commit = 1;  // 是否自动提交事务

  if (hp->txn && !hp->auto_manage_txn) {
    // 使用已有事务（批量操作场景），回滚由调用方负责
    txn = hp->txn;
    auto_commit = 0;
  } else {
    // 开启写事务
    int rc = lmjcore_txn_begin(hp->env, NULL, 0, &txn);
    if (rc != LMJCORE_SUCCESS || !txn) {
      cJSON_Delete(root);
      RETURN_ERROR_TXN_FAILED("begin", response);
    }
  }

  // 检查事务超时
  CHECK_TXN_TIMEOUT(hp, response, txn);

  // 1. 创建根集合
  lmjcore_ptr set_ptr;
  int rc = lmjcore_set_create(txn, set_ptr);
  if (rc != LMJCORE_SUCCESS) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    cJSON_Delete(root);
    build_lmjcore_error_response(rc, response);
    return -1;
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
      if (auto_commit) {
        lmjcore_txn_abort(txn);
      }
      cJSON_Delete(root);
      return build_error_response(lmjcore_error_to_http_status(rc), reason,
                                  response);
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
      if (auto_commit) {
        lmjcore_txn_abort(txn);
      }
      cJSON_Delete(root);
      return build_error_response(lmjcore_error_to_http_status(rc), reason,
                                  response);
    }
    index++;
  }

  // 3. 统计去重后的真实元素数量（提交前在同一事务内读取）
  size_t distinct_count = 0;
  size_t total_value_len = 0;
  rc = lmjcore_set_stat(txn, set_ptr, &total_value_len, &distinct_count);
  if (rc != LMJCORE_SUCCESS) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    cJSON_Delete(root);
    build_lmjcore_error_response(rc, response);
    return -1;
  }

  // 4. 提交事务（仅当自动管理时）
  if (auto_commit) {
    rc = lmjcore_txn_commit(txn);
    if (rc != LMJCORE_SUCCESS) {
      lmjcore_txn_abort(txn);
      cJSON_Delete(root);
      RETURN_ERROR_TXN_FAILED("commit", response);
    }
  }

  cJSON_Delete(root);

  // 将指针转换为字符串并构建响应
  char ptr_str[LMJCORE_PTR_STRING_LEN + 1];
  lmjcore_ptr_to_string(set_ptr, ptr_str, sizeof(ptr_str));

  char json_buf[512];
  snprintf(json_buf, sizeof(json_buf), "{\"ptr\":\"%s\",\"element_count\":%zu}",
           ptr_str, distinct_count);

  return build_success_response(HTTP_STATUS_CREATED, json_buf, response);
}
