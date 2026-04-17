// src/handlers/obj_handle.c - 对象相关 HTTP 处理器
#include "error_response.h"
#include "handle_utils.h"
#include "lmjcore.h"
#include "router.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==================== 事务超时检查宏 ====================

#define CHECK_TXN_TIMEOUT(hp, response, txn)                                 \
  do {                                                                       \
    if (lmjcore_txn_check_timeout((hp)->txn_start_time, (hp)->txn_timeout)) {\
      if (txn) lmjcore_txn_abort(txn);                                       \
      RETURN_ERROR_TXN_TIMEOUT(response);                                    \
    }                                                                        \
  } while (0)

// ==================== 对象处理器 ====================

int handle_obj_create(void *params, void *cbdata) {
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

  // 检查事务超时
  CHECK_TXN_TIMEOUT(hp, response, txn);

  // 创建对象
  lmjcore_ptr obj_ptr;
  rc = lmjcore_obj_create(txn, obj_ptr);
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
  lmjcore_ptr_to_string(obj_ptr, ptr_str, sizeof(ptr_str));

  // 构建响应
  char json_buf[512];
  snprintf(json_buf, sizeof(json_buf), "{\"ptr\":\"%s\"}", ptr_str);

  return build_success_response(HTTP_STATUS_CREATED, json_buf, response);
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

  // 开启读事务
  lmjcore_txn *txn = NULL;
  int rc = lmjcore_txn_begin(hp->env, NULL, LMJCORE_TXN_READONLY, &txn);
  if (rc != LMJCORE_SUCCESS || !txn) {
    RETURN_ERROR_TXN_FAILED("begin", response);
  }

  // 检查事务超时
  CHECK_TXN_TIMEOUT(hp, response, txn);

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, obj_ptr);
  if (exists != 1) {
    lmjcore_txn_abort(txn);
    RETURN_ERROR_NOT_FOUND("Object", response);
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

  uint8_t *result_buf = (uint8_t *)malloc(buf_size);
  if (!result_buf) {
    lmjcore_txn_abort(txn);
    RETURN_ERROR_NO_MEMORY(response);
  }

  lmjcore_result_obj *result_head = NULL;
  rc = lmjcore_obj_get(txn, obj_ptr, result_buf, buf_size, &result_head);

  lmjcore_txn_abort(txn); // 读事务完成，中止

  if (rc != LMJCORE_SUCCESS) {
    free(result_buf);
    build_lmjcore_error_response(rc, response);
    return -1;
  }

  // 构建 JSON 响应
  size_t json_size = 64 + LMJCORE_PTR_STRING_LEN + member_count * 256;
  char *json_buf = (char *)malloc(json_size);
  if (!json_buf) {
    free(result_buf);
    RETURN_ERROR_NO_MEMORY(response);
  }

  char ptr_out[LMJCORE_PTR_STRING_LEN + 1];
  lmjcore_ptr_to_string(obj_ptr, ptr_out, sizeof(ptr_out));

  int offset =
      snprintf(json_buf, json_size, "{\"ptr\":\"%s\",\"members\":[", ptr_out);

  // 遍历成员
  for (size_t i = 0; i < result_head->member_count; i++) {
    lmjcore_member_descriptor *desc = &result_head->members[i];

    // 获取成员名
    char *member_name = (char *)(result_buf + desc->member_name.value_offset);
    // 获取成员值
    uint8_t *value_data =
        (uint8_t *)(result_buf + desc->member_value.value_offset);
    size_t value_len = desc->member_value.value_len;

    char *value_str = NULL;
    api_value_type_t value_type;
    lmjcore_decode_value(value_data, value_len, &value_str, &value_type);

    const char *type_str = (value_type == VALUE_TYPE_RAW)    ? "raw"
                           : (value_type == VALUE_TYPE_REF)  ? "ref"
                           : (value_type == VALUE_TYPE_SET)  ? "set"
                           : (value_type == VALUE_TYPE_NULL) ? "null"
                                                             : "unknown";

    offset += snprintf(json_buf + offset, json_size - offset,
                       "%s{\"name\":\"%.*s\",\"value\":\"%s\",\"type\":\"%s\"}",
                       i > 0 ? "," : "", (int)desc->member_name.value_len,
                       member_name, value_str ? value_str : "", type_str);

    free(value_str);
  }

  offset += snprintf(json_buf + offset, json_size - offset, "],\"count\":%zu}",
                     result_head->member_count);

  free(result_buf);

  response->status_code = 200;
  response->body = json_buf;
  response->body_len = strlen(json_buf);

  return 0;
}

int handle_obj_member_get(void *params, void *cbdata) {
  handle_params_t *hp = (handle_params_t *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 获取参数：ptr 和 member_name
  const char *ptr_str = route_params_get(hp->params, 0);
  const char *member_name = route_params_get(hp->params, 1);

  if (!ptr_str || !member_name) {
    RETURN_ERROR_MISSING_PARAM("ptr or member", response);
  }

  // 转换指针
  lmjcore_ptr obj_ptr;
  if (lmjcore_ptr_from_string(ptr_str, obj_ptr) != LMJCORE_SUCCESS) {
    RETURN_ERROR_INVALID_PTR(response);
  }

  // 开启读事务
  lmjcore_txn *txn = NULL;
  int rc = lmjcore_txn_begin(hp->env, NULL, LMJCORE_TXN_READONLY, &txn);
  if (rc != LMJCORE_SUCCESS || !txn) {
    RETURN_ERROR_TXN_FAILED("begin", response);
  }

  // 检查事务超时
  CHECK_TXN_TIMEOUT(hp, response, txn);

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, obj_ptr);
  if (exists != 1) {
    lmjcore_txn_abort(txn);
    RETURN_ERROR_NOT_FOUND("Object", response);
  }

  // 分配缓冲区读取成员值
  size_t value_buf_size = 4096;
  uint8_t *value_buf = (uint8_t *)malloc(value_buf_size);
  if (!value_buf) {
    lmjcore_txn_abort(txn);
    RETURN_ERROR_NO_MEMORY(response);
  }

  size_t value_len = 0;
  rc = lmjcore_obj_member_get(txn, obj_ptr, (const uint8_t *)member_name,
                              strlen(member_name), value_buf, value_buf_size,
                              &value_len);

  lmjcore_txn_abort(txn);

  if (rc == LMJCORE_ERROR_MEMBER_NOT_FOUND) {
    free(value_buf);
    RETURN_ERROR_MEMBER_NOT_FOUND(response);
  }

  if (rc != LMJCORE_SUCCESS) {
    free(value_buf);
    build_lmjcore_error_response(rc, response);
    return -1;
  }

  // 解码值
  char *value_str = NULL;
  api_value_type_t value_type;
  lmjcore_decode_value(value_buf, value_len, &value_str, &value_type);
  free(value_buf);

  const char *type_str = (value_type == VALUE_TYPE_RAW)    ? "raw"
                         : (value_type == VALUE_TYPE_REF)  ? "ref"
                         : (value_type == VALUE_TYPE_SET)  ? "set"
                         : (value_type == VALUE_TYPE_NULL) ? "null"
                                                           : "unknown";

  // 构建响应
  char json_buf[4096];
  snprintf(json_buf, sizeof(json_buf),
           "{\"member\":\"%s\",\"value\":\"%s\",\"type\":\"%s\"}", member_name,
           value_str ? value_str : "", type_str);

  free(value_str);

  return build_success_response(HTTP_STATUS_OK, json_buf, response);
}

int handle_obj_member_put(void *params, void *cbdata) {
  handle_params_t *hp = (handle_params_t *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 获取参数：ptr 和 member_name
  const char *ptr_str = route_params_get(hp->params, 0);
  const char *member_name = route_params_get(hp->params, 1);

  if (!ptr_str || !member_name) {
    RETURN_ERROR_MISSING_PARAM("ptr or member", response);
  }

  // 解析请求体获取 value
  char *value_str = NULL;
  size_t value_len = 0;
  if (json_get_string(hp->body, hp->body_len, "value", &value_str,
                      &value_len) != 0) {
    RETURN_ERROR_BODY_PARSE(response);
  }

  // 转换指针
  lmjcore_ptr obj_ptr;
  if (lmjcore_ptr_from_string(ptr_str, obj_ptr) != LMJCORE_SUCCESS) {
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

  // 检查事务超时
  CHECK_TXN_TIMEOUT(hp, response, txn);

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, obj_ptr);
  if (exists != 1) {
    lmjcore_txn_abort(txn);
    free(value_str);
    RETURN_ERROR_NOT_FOUND("Object", response);
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

  // 设置成员值
  rc = lmjcore_obj_member_put(txn, obj_ptr, (const uint8_t *)member_name,
                              strlen(member_name), encoded_value, encoded_len);
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

int handle_obj_member_del(void *params, void *cbdata) {
  handle_params_t *hp = (handle_params_t *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 获取参数：ptr 和 member_name
  const char *ptr_str = route_params_get(hp->params, 0);
  const char *member_name = route_params_get(hp->params, 1);

  if (!ptr_str || !member_name) {
    RETURN_ERROR_MISSING_PARAM("ptr or member", response);
  }

  // 转换指针
  lmjcore_ptr obj_ptr;
  if (lmjcore_ptr_from_string(ptr_str, obj_ptr) != LMJCORE_SUCCESS) {
    RETURN_ERROR_INVALID_PTR(response);
  }

  // 开启写事务
  lmjcore_txn *txn = NULL;
  int rc = lmjcore_txn_begin(hp->env, NULL, 0, &txn);
  if (rc != LMJCORE_SUCCESS || !txn) {
    RETURN_ERROR_TXN_FAILED("begin", response);
  }

  // 检查事务超时
  CHECK_TXN_TIMEOUT(hp, response, txn);

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, obj_ptr);
  if (exists != 1) {
    lmjcore_txn_abort(txn);
    RETURN_ERROR_NOT_FOUND("Object", response);
  }

  // 删除成员
  rc = lmjcore_obj_member_del(txn, obj_ptr, (const uint8_t *)member_name,
                              strlen(member_name));

  if (rc == LMJCORE_ERROR_MEMBER_NOT_FOUND) {
    lmjcore_txn_abort(txn);
    RETURN_ERROR_MEMBER_NOT_FOUND(response);
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

int handle_obj_query(void *params, void *cbdata) {
  handle_params_t *hp = (handle_params_t *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 获取 path 参数（路由模式：/$'obj'/$'query'/${}）
  // 参数 0: query (固定)
  // 参数 1: 路径字符串 (例如：01abc...friend.name)
  const char *path_str = route_params_get(hp->params, 1);
  if (!path_str) {
    RETURN_ERROR_MISSING_PARAM("path", response);
  }

  // 解析路径
  char *start_ptr = NULL;
  char **segments = NULL;
  size_t segment_count = 0;

  int rc =
      lmjcore_parse_query_path(path_str, &start_ptr, &segments, &segment_count);
  if (rc != LMJCORE_SUCCESS || segment_count == 0) {
    lmjcore_free_path_parse_result(start_ptr, segments, segment_count);
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 转换起始指针
  lmjcore_ptr current_ptr;
  if (lmjcore_ptr_from_string(path_str, current_ptr) != LMJCORE_SUCCESS) {
    lmjcore_free_path_parse_result(start_ptr, segments, segment_count);
    RETURN_ERROR_INVALID_PTR(response);
  }

  // 开启读事务
  lmjcore_txn *txn = NULL;
  rc = lmjcore_txn_begin(hp->env, NULL, LMJCORE_TXN_READONLY, &txn);
  if (rc != LMJCORE_SUCCESS || !txn) {
    lmjcore_free_path_parse_result(start_ptr, segments, segment_count);
    RETURN_ERROR_TXN_FAILED("begin", response);
  }

  // 检查事务超时
  CHECK_TXN_TIMEOUT(hp, response, txn);

  // 遍历路径
  lmjcore_ptr obj_ptr;
  memcpy(obj_ptr, current_ptr, LMJCORE_PTR_LEN);
  char *current_value = NULL;
  api_value_type_t current_type = VALUE_TYPE_RAW;

  for (size_t i = 0; i < segment_count; i++) {
    // 在循环中每次迭代前也检查超时（长查询场景）
    CHECK_TXN_TIMEOUT(hp, response, txn);
    
    // 检查当前指针是否有效
    int exists = lmjcore_entity_exist(txn, obj_ptr);
    if (exists != 1) {
      lmjcore_txn_abort(txn);
      lmjcore_free_path_parse_result(start_ptr, segments, segment_count);
      free(current_value);
      RETURN_ERROR_NOT_FOUND("Entity", response);
    }

    // 读取当前对象的成员
    size_t value_buf_size = 4096;
    uint8_t *value_buf = (uint8_t *)malloc(value_buf_size);
    if (!value_buf) {
      lmjcore_txn_abort(txn);
      lmjcore_free_path_parse_result(start_ptr, segments, segment_count);
      free(current_value);
      RETURN_ERROR_NO_MEMORY(response);
    }

    size_t value_len = 0;
    rc = lmjcore_obj_member_get(txn, obj_ptr, (const uint8_t *)segments[i],
                                strlen(segments[i]), value_buf, value_buf_size,
                                &value_len);

    if (rc == LMJCORE_ERROR_MEMBER_NOT_FOUND) {
      free(value_buf);
      lmjcore_txn_abort(txn);
      lmjcore_free_path_parse_result(start_ptr, segments, segment_count);
      free(current_value);
      RETURN_ERROR_MEMBER_NOT_FOUND(response);
    }

    if (rc != LMJCORE_SUCCESS) {
      free(value_buf);
      lmjcore_txn_abort(txn);
      lmjcore_free_path_parse_result(start_ptr, segments, segment_count);
      free(current_value);
      build_lmjcore_error_response(rc, response);
      return -1;
    }

    // 解码值
    free(current_value);
    lmjcore_decode_value(value_buf, value_len, &current_value, &current_type);
    free(value_buf);

    // 如果是最后一个路径段，返回结果
    if (i == segment_count - 1) {
      break;
    }

    // 否则，值必须是指针类型，继续下一层
    if (current_type != VALUE_TYPE_REF) {
      lmjcore_txn_abort(txn);
      lmjcore_free_path_parse_result(start_ptr, segments, segment_count);
      free(current_value);
      build_error_response(HTTP_STATUS_BAD_REQUEST,
                           "Intermediate value is not a reference", response);
      return -1;
    }

    // 转换指针字符串为二进制
    if (lmjcore_ptr_from_string(current_value, obj_ptr) != LMJCORE_SUCCESS) {
      lmjcore_txn_abort(txn);
      lmjcore_free_path_parse_result(start_ptr, segments, segment_count);
      free(current_value);
      build_error_response(HTTP_STATUS_BAD_REQUEST,
                           "Invalid reference format", response);
      return -1;
    }
  }

  lmjcore_txn_abort(txn);
  lmjcore_free_path_parse_result(start_ptr, segments, segment_count);

  const char *type_str = (current_type == VALUE_TYPE_RAW)    ? "raw"
                         : (current_type == VALUE_TYPE_REF)  ? "ref"
                         : (current_type == VALUE_TYPE_SET)  ? "set"
                         : (current_type == VALUE_TYPE_NULL) ? "null"
                                                             : "unknown";

  // 构建响应
  char json_buf[4096];
  snprintf(json_buf, sizeof(json_buf),
           "{\"path\":\"%s\",\"value\":\"%s\",\"type\":\"%s\"}", path_str,
           current_value ? current_value : "", type_str);

  free(current_value);

  return build_success_response(HTTP_STATUS_OK, json_buf, response);
}
