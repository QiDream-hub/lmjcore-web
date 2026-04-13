/**
 * @file lmjcore_handle.c
 * @brief LMJCore HTTP API 处理器实现
 *
 * 每个操作都封装为独立的事务，不考虑阻塞问题。
 */

#include "../include/lmjcore_handle.h"
#include "../include/http_parser.h"
#include "../thirdparty/LMJCore/core/include/lmjcore.h"
#include "../thirdparty/URLRouer/router.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==================== 路由参数辅助函数 ====================

/**
 * @brief 从 route_params_t 中获取指定索引的参数（null 结尾字符串）
 *
 * @param params 参数列表
 * @param index 参数索引
 * @return const char* 参数字符串（需调用方复制），失败返回 NULL
 */
static const char *route_params_get(route_params_t *params, size_t index) {
  static __thread char param_buf[1024];

  if (!params || index >= params->count) {
    return NULL;
  }

  route_param_t param = params->params[index];
  if (param.len >= sizeof(param_buf) - 1) {
    return NULL; // 参数太长
  }

  memcpy(param_buf, param.ptr, param.len);
  param_buf[param.len] = '\0';

  return param_buf;
}

// ==================== 简单的 JSON 解析辅助函数 ====================

/**
 * @brief 从 JSON 对象中提取字符串值
 *
 * @param json JSON 字符串
 * @param key 键名
 * @param out_value 输出值（需调用方释放）
 * @param out_len 输出长度
 * @return int 错误码（0 表示成功）
 */
static int json_get_string(const char *json, size_t json_len, const char *key,
                           char **out_value, size_t *out_len) {
  if (!json || !key || !out_value) {
    return -1;
  }

  // 查找 "key":
  char search_pattern[256];
  snprintf(search_pattern, sizeof(search_pattern), "\"%s\"", key);

  const char *key_pos = strstr(json, search_pattern);
  if (!key_pos) {
    return -1;
  }

  // 跳过 key 和冒号
  const char *p = key_pos + strlen(search_pattern);
  while (*p && (*p == ':' || *p == ' ' || *p == '\t')) {
    p++;
  }

  if (*p != '"') {
    return -1; // 不是字符串值
  }
  p++; // 跳过开引号

  // 找到闭引号
  const char *start = p;
  while (*p && *p != '"') {
    if (*p == '\\' && *(p + 1)) {
      p += 2; // 跳过转义字符
    } else {
      p++;
    }
  }

  size_t len = p - start;
  *out_value = (char *)malloc(len + 1);
  if (!*out_value) {
    return -1;
  }

  memcpy(*out_value, start, len);
  (*out_value)[len] = '\0';
  *out_len = len;

  return 0;
}

/**
 * @brief 构建 JSON 响应
 *
 * @param status_code 状态码
 * @param json_body JSON 响应体
 * @param cbdata 回调数据（http_response_t）
 * @return int 错误码
 */
static int build_json_response(int status_code, const char *json_body,
                               void *cbdata) {
  http_response_t *response = (http_response_t *)cbdata;
  response->status_code = status_code;
  response->body = strdup(json_body);
  response->body_len = strlen(json_body);
  return 0;
}

// ==================== 工具函数实现 ====================

int lmjcore_ptr_from_hex(const char *str, uint8_t *ptr_out) {
  if (!str || !ptr_out) {
    return -1;
  }

  // 检查长度（34 字符 hex = 17 字节）
  size_t len = strlen(str);
  if (len != LMJCORE_PTR_STRING_LEN) {
    return -1;
  }

  for (size_t i = 0; i < LMJCORE_PTR_LEN; i++) {
    unsigned int byte;
    if (sscanf(str + i * 2, "%2x", &byte) != 1) {
      return -1;
    }
    ptr_out[i] = (uint8_t)byte;
  }

  return 0;
}

int lmjcore_ptr_to_hex(const uint8_t *ptr, char *str_out) {
  if (!ptr || !str_out) {
    return -1;
  }

  for (size_t i = 0; i < LMJCORE_PTR_LEN; i++) {
    sprintf(str_out + i * 2, "%02x", ptr[i]);
  }
  str_out[LMJCORE_PTR_STRING_LEN] = '\0';

  return 0;
}

int lmjcore_encode_value(const char *value_str, size_t value_len,
                         uint8_t *out_buf, size_t out_buf_size,
                         size_t *out_len) {
  if (!value_str || !out_buf || !out_len) {
    return LMJCORE_ERROR_NULL_POINTER;
  }

  if (out_buf_size < 1 + LMJCORE_PTR_LEN) {
    return LMJCORE_ERROR_BUFFER_TOO_SMALL;
  }

  // 检查是否为空值
  if (value_len == 0 || (value_len == 4 && strcmp(value_str, "null") == 0)) {
    out_buf[0] = LMJCORE_VALUE_TYPE_NULL;
    *out_len = 1;
    return LMJCORE_SUCCESS;
  }

  // 检查是否为指针引用（34 位十六进制，以 01 或 02 开头）
  if (value_len == LMJCORE_PTR_STRING_LEN) {
    uint8_t ptr[LMJCORE_PTR_LEN];
    if (lmjcore_ptr_from_hex(value_str, ptr) == 0) {
      if (ptr[0] == LMJCORE_OBJ || ptr[0] == LMJCORE_SET) {
        out_buf[0] = LMJCORE_VALUE_TYPE_PTR;
        memcpy(out_buf + 1, ptr, LMJCORE_PTR_LEN);
        *out_len = 1 + LMJCORE_PTR_LEN;
        return LMJCORE_SUCCESS;
      }
    }
  }

  // 默认为原始数据
  if (out_buf_size < 1 + value_len) {
    return LMJCORE_ERROR_BUFFER_TOO_SMALL;
  }

  out_buf[0] = LMJCORE_VALUE_TYPE_RAW;
  memcpy(out_buf + 1, value_str, value_len);
  *out_len = 1 + value_len;

  return LMJCORE_SUCCESS;
}

int lmjcore_decode_value(const uint8_t *data, size_t data_len, char **out_str,
                         api_value_type_t *out_type) {
  if (!data || !out_str || !out_type) {
    return LMJCORE_ERROR_NULL_POINTER;
  }

  if (data_len < 1) {
    return LMJCORE_ERROR_INVALID_PARAM;
  }

  uint8_t type_flag = data[0];

  switch (type_flag) {
  case LMJCORE_VALUE_TYPE_NULL:
    *out_str = strdup("null");
    *out_type = VALUE_TYPE_NULL;
    return LMJCORE_SUCCESS;

  case LMJCORE_VALUE_TYPE_PTR:
    if (data_len < 1 + LMJCORE_PTR_LEN) {
      return LMJCORE_ERROR_INVALID_PARAM;
    }
    *out_str = (char *)malloc(LMJCORE_PTR_STRING_LEN + 1);
    if (!*out_str) {
      return LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED;
    }
    lmjcore_ptr_to_hex(data + 1, *out_str);
    // 根据指针首字节判断类型
    lmjcore_entity_type etype = (lmjcore_entity_type)data[1];
    *out_type = (etype == LMJCORE_OBJ) ? VALUE_TYPE_REF : VALUE_TYPE_SET;
    return LMJCORE_SUCCESS;

  case LMJCORE_VALUE_TYPE_RAW:
    *out_str = (char *)malloc(data_len);
    if (!*out_str) {
      return LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED;
    }
    memcpy(*out_str, data + 1, data_len - 1);
    (*out_str)[data_len - 1] = '\0';
    *out_type = VALUE_TYPE_RAW;
    return LMJCORE_SUCCESS;

  default:
    return LMJCORE_ERROR_INVALID_PARAM;
  }
}

int lmjcore_parse_query_path(const char *path_str, char **start_ptr_out,
                             char ***segments_out, size_t *segment_count_out) {
  if (!path_str || !start_ptr_out || !segments_out || !segment_count_out) {
    return LMJCORE_ERROR_NULL_POINTER;
  }

  // 复制路径字符串以便修改
  char *path_copy = strdup(path_str);
  if (!path_copy) {
    return LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED;
  }

  // 找到第一个点号，分离指针和路径段
  char *first_dot = strchr(path_copy, '.');
  if (!first_dot) {
    free(path_copy);
    return LMJCORE_ERROR_INVALID_PARAM;
  }

  // 提取指针部分
  size_t ptr_len = first_dot - path_copy;
  if (ptr_len != LMJCORE_PTR_STRING_LEN) {
    free(path_copy);
    return LMJCORE_ERROR_INVALID_PARAM;
  }

  *start_ptr_out = (char *)malloc(ptr_len + 1);
  if (!*start_ptr_out) {
    free(path_copy);
    return LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED;
  }
  memcpy(*start_ptr_out, path_copy, ptr_len);
  (*start_ptr_out)[ptr_len] = '\0';

  // 解析路径段
  char **segments = NULL;
  size_t segment_count = 0;
  size_t segment_capacity = 8;

  segments = (char **)malloc(segment_capacity * sizeof(char *));
  if (!segments) {
    free(*start_ptr_out);
    free(path_copy);
    return LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED;
  }

  char *segment_start = first_dot + 1;
  char *p = segment_start;

  while (*p) {
    if (*p == '.') {
      // 提取一个路径段
      size_t seg_len = p - segment_start;
      if (seg_len > 0) {
        if (segment_count >= segment_capacity) {
          segment_capacity *= 2;
          char **new_segments =
              (char **)realloc(segments, segment_capacity * sizeof(char *));
          if (!new_segments) {
            // 清理已分配的内存
            for (size_t i = 0; i < segment_count; i++) {
              free(segments[i]);
            }
            free(segments);
            free(*start_ptr_out);
            free(path_copy);
            return LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED;
          }
          segments = new_segments;
        }

        segments[segment_count] = (char *)malloc(seg_len + 1);
        if (!segments[segment_count]) {
          // 清理
          for (size_t i = 0; i < segment_count; i++) {
            free(segments[i]);
          }
          free(segments);
          free(*start_ptr_out);
          free(path_copy);
          return LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED;
        }
        memcpy(segments[segment_count], segment_start, seg_len);
        segments[segment_count][seg_len] = '\0';
        segment_count++;
      }
      segment_start = p + 1;
    }
    p++;
  }

  // 处理最后一个路径段
  size_t seg_len = p - segment_start;
  if (seg_len > 0) {
    if (segment_count >= segment_capacity) {
      segment_capacity *= 2;
      char **new_segments =
          (char **)realloc(segments, segment_capacity * sizeof(char *));
      if (!new_segments) {
        for (size_t i = 0; i < segment_count; i++) {
          free(segments[i]);
        }
        free(segments);
        free(*start_ptr_out);
        free(path_copy);
        return LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED;
      }
      segments = new_segments;
    }

    segments[segment_count] = (char *)malloc(seg_len + 1);
    if (!segments[segment_count]) {
      for (size_t i = 0; i < segment_count; i++) {
        free(segments[i]);
      }
      free(segments);
      free(*start_ptr_out);
      free(path_copy);
      return LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED;
    }
    memcpy(segments[segment_count], segment_start, seg_len);
    segments[segment_count][seg_len] = '\0';
    segment_count++;
  }

  *segments_out = segments;
  *segment_count_out = segment_count;
  free(path_copy);

  return LMJCORE_SUCCESS;
}

void lmjcore_free_path_parse_result(char *start_ptr, char **segments,
                                    size_t segment_count) {
  free(start_ptr);
  for (size_t i = 0; i < segment_count; i++) {
    free(segments[i]);
  }
  free(segments);
}

// ==================== 响应释放函数 ====================

void lmjcore_free_member_responses(member_response_t *members, size_t count) {
  if (!members) {
    return;
  }

  for (size_t i = 0; i < count; i++) {
    free(members[i].member_name);
    free(members[i].value);
  }
  free(members);
}

void lmjcore_free_element_responses(element_response_t *elements,
                                    size_t count) {
  if (!elements) {
    return;
  }

  for (size_t i = 0; i < count; i++) {
    free(elements[i].value);
  }
  free(elements);
}

void lmjcore_free_object_response(object_response_t *response) {
  if (!response) {
    return;
  }

  free(response->ptr);
  lmjcore_free_member_responses(response->members, response->member_count);
  memset(response, 0, sizeof(object_response_t));
}

void lmjcore_free_set_response(set_response_t *response) {
  if (!response) {
    return;
  }

  free(response->ptr);
  lmjcore_free_element_responses(response->elements, response->element_count);
  memset(response, 0, sizeof(set_response_t));
}

// ==================== 对象处理器 ====================

int handle_obj_create(void *params, void *cbdata) {
  handle_params *hp = (handle_params *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env) {
    return build_json_response(500, "{\"error\":\"Invalid parameters\"}",
                               response);
  }

  // 开启写事务
  lmjcore_txn *txn = NULL;
  int rc = lmjcore_txn_begin(hp->env, NULL, 0, &txn);
  if (rc != LMJCORE_SUCCESS || !txn) {
    return build_json_response(
        500, "{\"error\":\"Failed to begin transaction\"}", response);
  }

  // 创建对象
  lmjcore_ptr obj_ptr;
  rc = lmjcore_obj_create(txn, obj_ptr);
  if (rc != LMJCORE_SUCCESS) {
    lmjcore_txn_abort(txn);
    char err_buf[256];
    snprintf(err_buf, sizeof(err_buf), "{\"error\":\"%s\"}",
             lmjcore_strerror(rc));
    return build_json_response(500, err_buf, response);
  }

  // 提交事务
  rc = lmjcore_txn_commit(txn);
  if (rc != LMJCORE_SUCCESS) {
    lmjcore_txn_abort(txn);
    return build_json_response(
        500, "{\"error\":\"Failed to commit transaction\"}", response);
  }

  // 将指针转换为字符串
  char ptr_str[LMJCORE_PTR_STRING_LEN + 1];
  lmjcore_ptr_to_string(obj_ptr, ptr_str, sizeof(ptr_str));

  // 构建响应
  char json_buf[512];
  snprintf(json_buf, sizeof(json_buf), "{\"ptr\":\"%s\"}", ptr_str);

  return build_json_response(201, json_buf, response);
}

int handle_obj_get(void *params, void *cbdata) {
  handle_params *hp = (handle_params *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    return build_json_response(500, "{\"error\":\"Invalid parameters\"}",
                               response);
  }

  // 获取指针参数
  const char *ptr_str = route_params_get(hp->params, 0);
  if (!ptr_str) {
    return build_json_response(400, "{\"error\":\"Missing ptr parameter\"}",
                               response);
  }

  // 转换指针
  lmjcore_ptr obj_ptr;
  if (lmjcore_ptr_from_string(ptr_str, obj_ptr) != LMJCORE_SUCCESS) {
    return build_json_response(400, "{\"error\":\"Invalid pointer format\"}",
                               response);
  }

  // 开启读事务
  lmjcore_txn *txn = NULL;
  int rc = lmjcore_txn_begin(hp->env, NULL, LMJCORE_TXN_READONLY, &txn);
  if (rc != LMJCORE_SUCCESS || !txn) {
    return build_json_response(
        500, "{\"error\":\"Failed to begin transaction\"}", response);
  }

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, obj_ptr);
  if (exists != 1) {
    lmjcore_txn_abort(txn);
    return build_json_response(404, "{\"error\":\"Object not found\"}",
                               response);
  }

  // 分配缓冲区读取对象内容
  // 先统计大小
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
    return build_json_response(500, "{\"error\":\"Failed to allocate memory\"}",
                               response);
  }

  lmjcore_result_obj *result_head = NULL;
  rc = lmjcore_obj_get(txn, obj_ptr, result_buf, buf_size, &result_head);

  lmjcore_txn_abort(txn); // 读事务完成，中止

  if (rc != LMJCORE_SUCCESS) {
    free(result_buf);
    char err_buf[256];
    snprintf(err_buf, sizeof(err_buf), "{\"error\":\"%s\"}",
             lmjcore_strerror(rc));
    return build_json_response(500, err_buf, response);
  }

  // 构建 JSON 响应
  // 先计算所需大小
  size_t json_size = 64 + LMJCORE_PTR_STRING_LEN + member_count * 256; // 估算
  char *json_buf = (char *)malloc(json_size);
  if (!json_buf) {
    free(result_buf);
    return build_json_response(500, "{\"error\":\"Failed to allocate memory\"}",
                               response);
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
                           : (value_type == VALUE_TYPE_NULL) ? "null"
                                                             : "unknown";

    // 转义成员名和值中的特殊字符
    // 简单处理：如果包含引号或反斜杠，需要转义
    // 这里简化处理，假设成员名和值不包含特殊字符

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
  handle_params *hp = (handle_params *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    return build_json_response(500, "{\"error\":\"Invalid parameters\"}",
                               response);
  }

  // 获取参数：ptr 和 member_name
  const char *ptr_str = route_params_get(hp->params, 0);
  const char *member_name = route_params_get(hp->params, 1);

  if (!ptr_str || !member_name) {
    return build_json_response(400, "{\"error\":\"Missing parameters\"}",
                               response);
  }

  // 转换指针
  lmjcore_ptr obj_ptr;
  if (lmjcore_ptr_from_string(ptr_str, obj_ptr) != LMJCORE_SUCCESS) {
    return build_json_response(400, "{\"error\":\"Invalid pointer format\"}",
                               response);
  }

  // 开启读事务
  lmjcore_txn *txn = NULL;
  int rc = lmjcore_txn_begin(hp->env, NULL, LMJCORE_TXN_READONLY, &txn);
  if (rc != LMJCORE_SUCCESS || !txn) {
    return build_json_response(
        500, "{\"error\":\"Failed to begin transaction\"}", response);
  }

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, obj_ptr);
  if (exists != 1) {
    lmjcore_txn_abort(txn);
    return build_json_response(404, "{\"error\":\"Object not found\"}",
                               response);
  }

  // 分配缓冲区读取成员值
  size_t value_buf_size = 4096;
  uint8_t *value_buf = (uint8_t *)malloc(value_buf_size);
  if (!value_buf) {
    lmjcore_txn_abort(txn);
    return build_json_response(500, "{\"error\":\"Failed to allocate memory\"}",
                               response);
  }

  size_t value_len = 0;
  rc = lmjcore_obj_member_get(txn, obj_ptr, (const uint8_t *)member_name,
                              strlen(member_name), value_buf, value_buf_size,
                              &value_len);

  lmjcore_txn_abort(txn);

  if (rc == LMJCORE_ERROR_MEMBER_NOT_FOUND) {
    free(value_buf);
    return build_json_response(404, "{\"error\":\"Member not found\"}",
                               response);
  }

  if (rc != LMJCORE_SUCCESS) {
    free(value_buf);
    char err_buf[256];
    snprintf(err_buf, sizeof(err_buf), "{\"error\":\"%s\"}",
             lmjcore_strerror(rc));
    return build_json_response(500, err_buf, response);
  }

  // 解码值
  char *value_str = NULL;
  api_value_type_t value_type;
  lmjcore_decode_value(value_buf, value_len, &value_str, &value_type);
  free(value_buf);

  const char *type_str = (value_type == VALUE_TYPE_RAW)    ? "raw"
                         : (value_type == VALUE_TYPE_REF)  ? "ref"
                         : (value_type == VALUE_TYPE_NULL) ? "null"
                                                           : "unknown";

  // 构建响应
  char json_buf[4096];
  snprintf(json_buf, sizeof(json_buf),
           "{\"member\":\"%s\",\"value\":\"%s\",\"type\":\"%s\"}", member_name,
           value_str ? value_str : "", type_str);

  free(value_str);

  return build_json_response(200, json_buf, response);
}

int handle_obj_member_put(void *params, void *cbdata) {
  handle_params *hp = (handle_params *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    return build_json_response(500, "{\"error\":\"Invalid parameters\"}",
                               response);
  }

  // 获取参数：ptr 和 member_name
  const char *ptr_str = route_params_get(hp->params, 0);
  const char *member_name = route_params_get(hp->params, 1);

  if (!ptr_str || !member_name) {
    return build_json_response(400, "{\"error\":\"Missing parameters\"}",
                               response);
  }

  // 解析请求体获取 value
  char *value_str = NULL;
  size_t value_len = 0;
  if (json_get_string(hp->body, hp->body_len, "value", &value_str,
                      &value_len) != 0) {
    return build_json_response(
        400, "{\"error\":\"Missing value in request body\"}", response);
  }

  // 转换指针
  lmjcore_ptr obj_ptr;
  if (lmjcore_ptr_from_string(ptr_str, obj_ptr) != LMJCORE_SUCCESS) {
    free(value_str);
    return build_json_response(400, "{\"error\":\"Invalid pointer format\"}",
                               response);
  }

  // 开启写事务
  lmjcore_txn *txn = NULL;
  int rc = lmjcore_txn_begin(hp->env, NULL, 0, &txn);
  if (rc != LMJCORE_SUCCESS || !txn) {
    free(value_str);
    return build_json_response(
        500, "{\"error\":\"Failed to begin transaction\"}", response);
  }

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, obj_ptr);
  if (exists != 1) {
    lmjcore_txn_abort(txn);
    free(value_str);
    return build_json_response(404, "{\"error\":\"Object not found\"}",
                               response);
  }

  // 编码值
  size_t encoded_size = 1 + LMJCORE_PTR_LEN + value_len + 16;
  uint8_t *encoded_value = (uint8_t *)malloc(encoded_size);
  if (!encoded_value) {
    lmjcore_txn_abort(txn);
    free(value_str);
    return build_json_response(500, "{\"error\":\"Failed to allocate memory\"}",
                               response);
  }

  size_t encoded_len = 0;
  rc = lmjcore_encode_value(value_str, value_len, encoded_value, encoded_size,
                            &encoded_len);
  free(value_str);

  if (rc != LMJCORE_SUCCESS) {
    lmjcore_txn_abort(txn);
    free(encoded_value);
    char err_buf[256];
    snprintf(err_buf, sizeof(err_buf), "{\"error\":\"%s\"}",
             lmjcore_strerror(rc));
    return build_json_response(500, err_buf, response);
  }

  // 设置成员值
  rc = lmjcore_obj_member_put(txn, obj_ptr, (const uint8_t *)member_name,
                              strlen(member_name), encoded_value, encoded_len);
  free(encoded_value);

  if (rc != LMJCORE_SUCCESS) {
    lmjcore_txn_abort(txn);
    char err_buf[256];
    snprintf(err_buf, sizeof(err_buf), "{\"error\":\"%s\"}",
             lmjcore_strerror(rc));
    return build_json_response(500, err_buf, response);
  }

  // 提交事务
  rc = lmjcore_txn_commit(txn);
  if (rc != LMJCORE_SUCCESS) {
    lmjcore_txn_abort(txn);
    return build_json_response(
        500, "{\"error\":\"Failed to commit transaction\"}", response);
  }

  return build_json_response(200, "{\"success\":true}", response);
}

int handle_obj_member_del(void *params, void *cbdata) {
  handle_params *hp = (handle_params *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    return build_json_response(500, "{\"error\":\"Invalid parameters\"}",
                               response);
  }

  // 获取参数：ptr 和 member_name
  const char *ptr_str = route_params_get(hp->params, 0);
  const char *member_name = route_params_get(hp->params, 1);

  if (!ptr_str || !member_name) {
    return build_json_response(400, "{\"error\":\"Missing parameters\"}",
                               response);
  }

  // 转换指针
  lmjcore_ptr obj_ptr;
  if (lmjcore_ptr_from_string(ptr_str, obj_ptr) != LMJCORE_SUCCESS) {
    return build_json_response(400, "{\"error\":\"Invalid pointer format\"}",
                               response);
  }

  // 开启写事务
  lmjcore_txn *txn = NULL;
  int rc = lmjcore_txn_begin(hp->env, NULL, 0, &txn);
  if (rc != LMJCORE_SUCCESS || !txn) {
    return build_json_response(
        500, "{\"error\":\"Failed to begin transaction\"}", response);
  }

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, obj_ptr);
  if (exists != 1) {
    lmjcore_txn_abort(txn);
    return build_json_response(404, "{\"error\":\"Object not found\"}",
                               response);
  }

  // 删除成员
  rc = lmjcore_obj_member_del(txn, obj_ptr, (const uint8_t *)member_name,
                              strlen(member_name));

  if (rc == LMJCORE_ERROR_MEMBER_NOT_FOUND) {
    lmjcore_txn_abort(txn);
    return build_json_response(404, "{\"error\":\"Member not found\"}",
                               response);
  }

  if (rc != LMJCORE_SUCCESS) {
    lmjcore_txn_abort(txn);
    char err_buf[256];
    snprintf(err_buf, sizeof(err_buf), "{\"error\":\"%s\"}",
             lmjcore_strerror(rc));
    return build_json_response(500, err_buf, response);
  }

  // 提交事务
  rc = lmjcore_txn_commit(txn);
  if (rc != LMJCORE_SUCCESS) {
    lmjcore_txn_abort(txn);
    return build_json_response(
        500, "{\"error\":\"Failed to commit transaction\"}", response);
  }

  return build_json_response(200, "{\"success\":true}", response);
}

int handle_obj_query(void *params, void *cbdata) {
  handle_params *hp = (handle_params *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    return build_json_response(500, "{\"error\":\"Invalid parameters\"}",
                               response);
  }

  // 获取 path 参数（从 URL 中提取）
  // URL 格式：/obj/query?path=xxx
  // 这里需要从 params 中获取查询参数
  const char *path_str = route_params_get(hp->params, 0);
  if (!path_str) {
    return build_json_response(400, "{\"error\":\"Missing path parameter\"}",
                               response);
  }

  // 解析路径
  char *start_ptr = NULL;
  char **segments = NULL;
  size_t segment_count = 0;

  int rc =
      lmjcore_parse_query_path(path_str, &start_ptr, &segments, &segment_count);
  if (rc != LMJCORE_SUCCESS || segment_count == 0) {
    lmjcore_free_path_parse_result(start_ptr, segments, segment_count);
    return build_json_response(400, "{\"error\":\"Invalid path format\"}",
                               response);
  }

  // 转换起始指针
  lmjcore_ptr current_ptr;
  if (lmjcore_ptr_from_string(start_ptr, current_ptr) != LMJCORE_SUCCESS) {
    lmjcore_free_path_parse_result(start_ptr, segments, segment_count);
    return build_json_response(400, "{\"error\":\"Invalid pointer format\"}",
                               response);
  }

  // 开启读事务
  lmjcore_txn *txn = NULL;
  rc = lmjcore_txn_begin(hp->env, NULL, LMJCORE_TXN_READONLY, &txn);
  if (rc != LMJCORE_SUCCESS || !txn) {
    lmjcore_free_path_parse_result(start_ptr, segments, segment_count);
    return build_json_response(
        500, "{\"error\":\"Failed to begin transaction\"}", response);
  }

  // 遍历路径
  lmjcore_ptr obj_ptr;
  memcpy(obj_ptr, current_ptr, LMJCORE_PTR_LEN);
  char *current_value = NULL;
  api_value_type_t current_type = VALUE_TYPE_RAW;

  for (size_t i = 0; i < segment_count; i++) {
    // 检查当前指针是否有效
    int exists = lmjcore_entity_exist(txn, obj_ptr);
    if (exists != 1) {
      lmjcore_txn_abort(txn);
      lmjcore_free_path_parse_result(start_ptr, segments, segment_count);
      free(current_value);
      return build_json_response(404, "{\"error\":\"Entity not found\"}",
                                 response);
    }

    // 读取当前对象的成员
    size_t value_buf_size = 4096;
    uint8_t *value_buf = (uint8_t *)malloc(value_buf_size);
    if (!value_buf) {
      lmjcore_txn_abort(txn);
      lmjcore_free_path_parse_result(start_ptr, segments, segment_count);
      free(current_value);
      return build_json_response(
          500, "{\"error\":\"Failed to allocate memory\"}", response);
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
      return build_json_response(404, "{\"error\":\"Member not found\"}",
                                 response);
    }

    if (rc != LMJCORE_SUCCESS) {
      free(value_buf);
      lmjcore_txn_abort(txn);
      lmjcore_free_path_parse_result(start_ptr, segments, segment_count);
      free(current_value);
      char err_buf[256];
      snprintf(err_buf, sizeof(err_buf), "{\"error\":\"%s\"}",
               lmjcore_strerror(rc));
      return build_json_response(500, err_buf, response);
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
      return build_json_response(
          400, "{\"error\":\"Intermediate value is not a reference\"}",
          response);
    }

    // 转换指针字符串为二进制
    if (lmjcore_ptr_from_string(current_value, obj_ptr) != LMJCORE_SUCCESS) {
      lmjcore_txn_abort(txn);
      lmjcore_free_path_parse_result(start_ptr, segments, segment_count);
      free(current_value);
      return build_json_response(
          400, "{\"error\":\"Invalid reference format\"}", response);
    }
  }

  lmjcore_txn_abort(txn);
  lmjcore_free_path_parse_result(start_ptr, segments, segment_count);

  const char *type_str = (current_type == VALUE_TYPE_RAW)    ? "raw"
                         : (current_type == VALUE_TYPE_REF)  ? "ref"
                         : (current_type == VALUE_TYPE_NULL) ? "null"
                                                             : "unknown";

  // 构建响应
  char json_buf[4096];
  snprintf(json_buf, sizeof(json_buf),
           "{\"path\":\"%s\",\"value\":\"%s\",\"type\":\"%s\"}", path_str,
           current_value ? current_value : "", type_str);

  free(current_value);

  return build_json_response(200, json_buf, response);
}

// ==================== 集合处理器 ====================

int handle_set_create(void *params, void *cbdata) {
  handle_params *hp = (handle_params *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env) {
    return build_json_response(500, "{\"error\":\"Invalid parameters\"}",
                               response);
  }

  // 开启写事务
  lmjcore_txn *txn = NULL;
  int rc = lmjcore_txn_begin(hp->env, NULL, 0, &txn);
  if (rc != LMJCORE_SUCCESS || !txn) {
    return build_json_response(
        500, "{\"error\":\"Failed to begin transaction\"}", response);
  }

  // 创建集合
  lmjcore_ptr set_ptr;
  rc = lmjcore_set_create(txn, set_ptr);
  if (rc != LMJCORE_SUCCESS) {
    lmjcore_txn_abort(txn);
    char err_buf[256];
    snprintf(err_buf, sizeof(err_buf), "{\"error\":\"%s\"}",
             lmjcore_strerror(rc));
    return build_json_response(500, err_buf, response);
  }

  // 提交事务
  rc = lmjcore_txn_commit(txn);
  if (rc != LMJCORE_SUCCESS) {
    lmjcore_txn_abort(txn);
    return build_json_response(
        500, "{\"error\":\"Failed to commit transaction\"}", response);
  }

  // 将指针转换为字符串
  char ptr_str[LMJCORE_PTR_STRING_LEN + 1];
  lmjcore_ptr_to_string(set_ptr, ptr_str, sizeof(ptr_str));

  // 构建响应
  char json_buf[512];
  snprintf(json_buf, sizeof(json_buf), "{\"ptr\":\"%s\"}", ptr_str);

  return build_json_response(201, json_buf, response);
}

int handle_set_get(void *params, void *cbdata) {
  handle_params *hp = (handle_params *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    return build_json_response(500, "{\"error\":\"Invalid parameters\"}",
                               response);
  }

  // 获取指针参数
  const char *ptr_str = route_params_get(hp->params, 0);
  if (!ptr_str) {
    return build_json_response(400, "{\"error\":\"Missing ptr parameter\"}",
                               response);
  }

  // 转换指针
  lmjcore_ptr set_ptr;
  if (lmjcore_ptr_from_string(ptr_str, set_ptr) != LMJCORE_SUCCESS) {
    return build_json_response(400, "{\"error\":\"Invalid pointer format\"}",
                               response);
  }

  // 开启读事务
  lmjcore_txn *txn = NULL;
  int rc = lmjcore_txn_begin(hp->env, NULL, LMJCORE_TXN_READONLY, &txn);
  if (rc != LMJCORE_SUCCESS || !txn) {
    return build_json_response(
        500, "{\"error\":\"Failed to begin transaction\"}", response);
  }

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, set_ptr);
  if (exists != 1) {
    lmjcore_txn_abort(txn);
    return build_json_response(404, "{\"error\":\"Set not found\"}", response);
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
    return build_json_response(500, "{\"error\":\"Failed to allocate memory\"}",
                               response);
  }

  lmjcore_result_set *result_head = NULL;
  rc = lmjcore_set_get(txn, set_ptr, result_buf, buf_size, &result_head);

  lmjcore_txn_abort(txn);

  if (rc != LMJCORE_SUCCESS) {
    free(result_buf);
    char err_buf[256];
    snprintf(err_buf, sizeof(err_buf), "{\"error\":\"%s\"}",
             lmjcore_strerror(rc));
    return build_json_response(500, err_buf, response);
  }

  // 构建 JSON 响应
  size_t json_size = 64 + LMJCORE_PTR_STRING_LEN + element_count * 128;
  char *json_buf = (char *)malloc(json_size);
  if (!json_buf) {
    free(result_buf);
    return build_json_response(500, "{\"error\":\"Failed to allocate memory\"}",
                               response);
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
  handle_params *hp = (handle_params *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    return build_json_response(500, "{\"error\":\"Invalid parameters\"}",
                               response);
  }

  // 获取参数
  const char *ptr_str = route_params_get(hp->params, 0);
  if (!ptr_str) {
    return build_json_response(400, "{\"error\":\"Missing ptr parameter\"}",
                               response);
  }

  // 解析请求体获取 value
  char *value_str = NULL;
  size_t value_len = 0;
  if (json_get_string(hp->body, hp->body_len, "value", &value_str,
                      &value_len) != 0) {
    return build_json_response(
        400, "{\"error\":\"Missing value in request body\"}", response);
  }

  // 转换指针
  lmjcore_ptr set_ptr;
  if (lmjcore_ptr_from_string(ptr_str, set_ptr) != LMJCORE_SUCCESS) {
    free(value_str);
    return build_json_response(400, "{\"error\":\"Invalid pointer format\"}",
                               response);
  }

  // 开启写事务
  lmjcore_txn *txn = NULL;
  int rc = lmjcore_txn_begin(hp->env, NULL, 0, &txn);
  if (rc != LMJCORE_SUCCESS || !txn) {
    free(value_str);
    return build_json_response(
        500, "{\"error\":\"Failed to begin transaction\"}", response);
  }

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, set_ptr);
  if (exists != 1) {
    lmjcore_txn_abort(txn);
    free(value_str);
    return build_json_response(404, "{\"error\":\"Set not found\"}", response);
  }

  // 编码值
  size_t encoded_size = 1 + LMJCORE_PTR_LEN + value_len + 16;
  uint8_t *encoded_value = (uint8_t *)malloc(encoded_size);
  if (!encoded_value) {
    lmjcore_txn_abort(txn);
    free(value_str);
    return build_json_response(500, "{\"error\":\"Failed to allocate memory\"}",
                               response);
  }

  size_t encoded_len = 0;
  rc = lmjcore_encode_value(value_str, value_len, encoded_value, encoded_size,
                            &encoded_len);
  free(value_str);

  if (rc != LMJCORE_SUCCESS) {
    lmjcore_txn_abort(txn);
    free(encoded_value);
    char err_buf[256];
    snprintf(err_buf, sizeof(err_buf), "{\"error\":\"%s\"}",
             lmjcore_strerror(rc));
    return build_json_response(500, err_buf, response);
  }

  // 添加元素
  rc = lmjcore_set_add(txn, set_ptr, encoded_value, encoded_len);
  free(encoded_value);

  if (rc == LMJCORE_ERROR_MEMBER_EXISTS) {
    lmjcore_txn_abort(txn);
    return build_json_response(409, "{\"error\":\"Element already exists\"}",
                               response);
  }

  if (rc != LMJCORE_SUCCESS) {
    lmjcore_txn_abort(txn);
    char err_buf[256];
    snprintf(err_buf, sizeof(err_buf), "{\"error\":\"%s\"}",
             lmjcore_strerror(rc));
    return build_json_response(500, err_buf, response);
  }

  // 提交事务
  rc = lmjcore_txn_commit(txn);
  if (rc != LMJCORE_SUCCESS) {
    lmjcore_txn_abort(txn);
    return build_json_response(
        500, "{\"error\":\"Failed to commit transaction\"}", response);
  }

  return build_json_response(200, "{\"success\":true}", response);
}

int handle_set_remove(void *params, void *cbdata) {
  handle_params *hp = (handle_params *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    return build_json_response(500, "{\"error\":\"Invalid parameters\"}",
                               response);
  }

  // 获取参数
  const char *ptr_str = route_params_get(hp->params, 0);
  if (!ptr_str) {
    return build_json_response(400, "{\"error\":\"Missing ptr parameter\"}",
                               response);
  }

  // 解析请求体获取 value
  char *value_str = NULL;
  size_t value_len = 0;
  if (json_get_string(hp->body, hp->body_len, "value", &value_str,
                      &value_len) != 0) {
    return build_json_response(
        400, "{\"error\":\"Missing value in request body\"}", response);
  }

  // 转换指针
  lmjcore_ptr set_ptr;
  if (lmjcore_ptr_from_string(ptr_str, set_ptr) != LMJCORE_SUCCESS) {
    free(value_str);
    return build_json_response(400, "{\"error\":\"Invalid pointer format\"}",
                               response);
  }

  // 开启写事务
  lmjcore_txn *txn = NULL;
  int rc = lmjcore_txn_begin(hp->env, NULL, 0, &txn);
  if (rc != LMJCORE_SUCCESS || !txn) {
    free(value_str);
    return build_json_response(
        500, "{\"error\":\"Failed to begin transaction\"}", response);
  }

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, set_ptr);
  if (exists != 1) {
    lmjcore_txn_abort(txn);
    free(value_str);
    return build_json_response(404, "{\"error\":\"Set not found\"}", response);
  }

  // 编码值
  size_t encoded_size = 1 + LMJCORE_PTR_LEN + value_len + 16;
  uint8_t *encoded_value = (uint8_t *)malloc(encoded_size);
  if (!encoded_value) {
    lmjcore_txn_abort(txn);
    free(value_str);
    return build_json_response(500, "{\"error\":\"Failed to allocate memory\"}",
                               response);
  }

  size_t encoded_len = 0;
  rc = lmjcore_encode_value(value_str, value_len, encoded_value, encoded_size,
                            &encoded_len);
  free(value_str);

  if (rc != LMJCORE_SUCCESS) {
    lmjcore_txn_abort(txn);
    free(encoded_value);
    char err_buf[256];
    snprintf(err_buf, sizeof(err_buf), "{\"error\":\"%s\"}",
             lmjcore_strerror(rc));
    return build_json_response(500, err_buf, response);
  }

  // 删除元素
  rc = lmjcore_set_remove(txn, set_ptr, encoded_value, encoded_len);
  free(encoded_value);

  if (rc != LMJCORE_SUCCESS) {
    lmjcore_txn_abort(txn);
    char err_buf[256];
    snprintf(err_buf, sizeof(err_buf), "{\"error\":\"%s\"}",
             lmjcore_strerror(rc));
    return build_json_response(500, err_buf, response);
  }

  // 提交事务
  rc = lmjcore_txn_commit(txn);
  if (rc != LMJCORE_SUCCESS) {
    lmjcore_txn_abort(txn);
    return build_json_response(
        500, "{\"error\":\"Failed to commit transaction\"}", response);
  }

  return build_json_response(200, "{\"success\":true}", response);
}

// ==================== 工具处理器 ====================

int handle_ptr_exist(void *params, void *cbdata) {
  handle_params *hp = (handle_params *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    return build_json_response(500, "{\"error\":\"Invalid parameters\"}",
                               response);
  }

  // 获取指针参数
  const char *ptr_str = route_params_get(hp->params, 0);
  if (!ptr_str) {
    return build_json_response(400, "{\"error\":\"Missing ptr parameter\"}",
                               response);
  }

  // 转换指针
  lmjcore_ptr ptr;
  if (lmjcore_ptr_from_string(ptr_str, ptr) != LMJCORE_SUCCESS) {
    return build_json_response(400, "{\"error\":\"Invalid pointer format\"}",
                               response);
  }

  // 开启读事务
  lmjcore_txn *txn = NULL;
  int rc = lmjcore_txn_begin(hp->env, NULL, LMJCORE_TXN_READONLY, &txn);
  if (rc != LMJCORE_SUCCESS || !txn) {
    return build_json_response(
        500, "{\"error\":\"Failed to begin transaction\"}", response);
  }

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, ptr);

  lmjcore_txn_abort(txn);

  if (exists < 0) {
    char err_buf[256];
    snprintf(err_buf, sizeof(err_buf), "{\"error\":\"%s\"}",
             lmjcore_strerror(exists));
    return build_json_response(500, err_buf, response);
  }

  if (exists == 0) {
    return build_json_response(404, "{\"exist\":false}", response);
  }

  // 检查实体类型
  // 通过指针首字节判断
  lmjcore_entity_type etype = (lmjcore_entity_type)ptr[0];
  const char *type_str = (etype == LMJCORE_OBJ) ? "object" : "set";

  char json_buf[128];
  snprintf(json_buf, sizeof(json_buf), "{\"exist\":true,\"type\":\"%s\"}",
           type_str);

  return build_json_response(200, json_buf, response);
}

// 全局启动时间
static time_t g_start_time = 0;

int handle_health(void *params, void *cbdata) {
  http_response_t *response = (http_response_t *)cbdata;

  if (g_start_time == 0) {
    g_start_time = time(NULL);
  }

  time_t uptime = time(NULL) - g_start_time;

  char json_buf[256];
  snprintf(json_buf, sizeof(json_buf), "{\"status\":\"ok\",\"uptime\":%ld}",
           (long)uptime);

  return build_json_response(200, json_buf, response);
}
