// src/handlers/set_handle.c - 集合相关 HTTP 处理器
#include "error_response.h"
#include "handle_utils.h"
#include "lmjcore.h"
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

  // 开启写事务
  lmjcore_txn *txn = NULL;
  int rc = lmjcore_txn_begin(hp->env, NULL, 0, &txn);
  if (rc != LMJCORE_SUCCESS || !txn) {
    RETURN_ERROR_TXN_FAILED("begin", response);
  }

  // 创建集合
  lmjcore_ptr set_ptr;
  rc = lmjcore_set_create(txn, set_ptr);
  if (rc != LMJCORE_SUCCESS) {
    lmjcore_txn_abort(txn);
    build_lmjcore_error_response(rc, response);
    return -1;
  }

  // 提交事务
  rc = lmjcore_txn_commit(txn);
  if (rc != LMJCORE_SUCCESS) {
    lmjcore_txn_abort(txn);
    RETURN_ERROR_TXN_FAILED("commit", response);
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

  // 开启读事务
  lmjcore_txn *txn = NULL;
  int rc = lmjcore_txn_begin(hp->env, NULL, LMJCORE_TXN_READONLY, &txn);
  if (rc != LMJCORE_SUCCESS || !txn) {
    RETURN_ERROR_TXN_FAILED("begin", response);
  }

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, set_ptr);
  if (exists != 1) {
    lmjcore_txn_abort(txn);
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
    lmjcore_txn_abort(txn);
    RETURN_ERROR_NO_MEMORY(response);
  }

  lmjcore_result_set *result_head = NULL;
  rc = lmjcore_set_get(txn, set_ptr, result_buf, buf_size, &result_head);

  lmjcore_txn_abort(txn);

  if (rc != LMJCORE_SUCCESS) {
    free(result_buf);
    build_lmjcore_error_response(rc, response);
    return -1;
  }

  // 构建 JSON 响应
  size_t json_size = 64 + LMJCORE_PTR_STRING_LEN + element_count * 128;
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

    offset += snprintf(json_buf + offset, json_size - offset,
                       "%s{\"value\":\"%s\",\"type\":\"%s\"}", i > 0 ? "," : "",
                       value_str ? value_str : "", type_str);

    free(value_str);
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
  char *value_str = NULL;
  size_t value_len = 0;
  if (json_get_string(hp->body, hp->body_len, "value", &value_str,
                      &value_len) != 0) {
    RETURN_ERROR_BODY_PARSE(response);
  }

  // 转换指针
  lmjcore_ptr set_ptr;
  if (lmjcore_ptr_from_string(ptr_str, set_ptr) != LMJCORE_SUCCESS) {
    free(value_str);
    RETURN_ERROR_INVALID_PTR(response);
  }

  // 开启写事务
  lmjcore_txn *txn = NULL;
  int rc = lmjcore_txn_begin(hp->env, NULL, 0, &txn);
  if (rc != LMJCORE_SUCCESS || !txn) {
    free(value_str);
    RETURN_ERROR_TXN_FAILED("begin", response);
  }

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, set_ptr);
  if (exists != 1) {
    lmjcore_txn_abort(txn);
    free(value_str);
    RETURN_ERROR_NOT_FOUND("Set", response);
  }

  // 编码值
  size_t encoded_size = 1 + LMJCORE_PTR_LEN + value_len + 16;
  uint8_t *encoded_value = (uint8_t *)malloc(encoded_size);
  if (!encoded_value) {
    lmjcore_txn_abort(txn);
    free(value_str);
    RETURN_ERROR_NO_MEMORY(response);
  }

  size_t encoded_len = 0;
  rc = lmjcore_encode_value(value_str, value_len, encoded_value, encoded_size,
                            &encoded_len);
  free(value_str);

  if (rc != LMJCORE_SUCCESS) {
    lmjcore_txn_abort(txn);
    free(encoded_value);
    build_lmjcore_error_response(rc, response);
    return -1;
  }

  // 添加元素
  rc = lmjcore_set_add(txn, set_ptr, encoded_value, encoded_len);
  free(encoded_value);

  if (rc == LMJCORE_ERROR_MEMBER_EXISTS) {
    lmjcore_txn_abort(txn);
    return build_error_response(HTTP_STATUS_CONFLICT,
                                "Element already exists", response);
  }

  if (rc != LMJCORE_SUCCESS) {
    lmjcore_txn_abort(txn);
    build_lmjcore_error_response(rc, response);
    return -1;
  }

  // 提交事务
  rc = lmjcore_txn_commit(txn);
  if (rc != LMJCORE_SUCCESS) {
    lmjcore_txn_abort(txn);
    RETURN_ERROR_TXN_FAILED("commit", response);
  }

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
  char *value_str = NULL;
  size_t value_len = 0;
  if (json_get_string(hp->body, hp->body_len, "value", &value_str,
                      &value_len) != 0) {
    RETURN_ERROR_BODY_PARSE(response);
  }

  // 转换指针
  lmjcore_ptr set_ptr;
  if (lmjcore_ptr_from_string(ptr_str, set_ptr) != LMJCORE_SUCCESS) {
    free(value_str);
    RETURN_ERROR_INVALID_PTR(response);
  }

  // 开启写事务
  lmjcore_txn *txn = NULL;
  int rc = lmjcore_txn_begin(hp->env, NULL, 0, &txn);
  if (rc != LMJCORE_SUCCESS || !txn) {
    free(value_str);
    RETURN_ERROR_TXN_FAILED("begin", response);
  }

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, set_ptr);
  if (exists != 1) {
    lmjcore_txn_abort(txn);
    free(value_str);
    RETURN_ERROR_NOT_FOUND("Set", response);
  }

  // 编码值
  size_t encoded_size = 1 + LMJCORE_PTR_LEN + value_len + 16;
  uint8_t *encoded_value = (uint8_t *)malloc(encoded_size);
  if (!encoded_value) {
    lmjcore_txn_abort(txn);
    free(value_str);
    RETURN_ERROR_NO_MEMORY(response);
  }

  size_t encoded_len = 0;
  rc = lmjcore_encode_value(value_str, value_len, encoded_value, encoded_size,
                            &encoded_len);
  free(value_str);

  if (rc != LMJCORE_SUCCESS) {
    lmjcore_txn_abort(txn);
    free(encoded_value);
    build_lmjcore_error_response(rc, response);
    return -1;
  }

  // 删除元素
  rc = lmjcore_set_remove(txn, set_ptr, encoded_value, encoded_len);
  free(encoded_value);

  if (rc != LMJCORE_SUCCESS) {
    lmjcore_txn_abort(txn);
    build_lmjcore_error_response(rc, response);
    return -1;
  }

  // 提交事务
  rc = lmjcore_txn_commit(txn);
  if (rc != LMJCORE_SUCCESS) {
    lmjcore_txn_abort(txn);
    RETURN_ERROR_TXN_FAILED("commit", response);
  }

  return build_success_response(HTTP_STATUS_OK, "{\"success\":true}", response);
}
