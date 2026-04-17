// src/handlers/handle_utils.c - 处理器通用工具函数实现
#include "handle_utils.h"
#include "lmjcore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==================== 路由参数辅助函数 ====================

const char *route_params_get(route_params_t *params, size_t index) {
  // 使用线程局部存储的多个缓冲区，支持最多 4 个参数同时使用
  static __thread char param_bufs[4][1024];
  static __thread int current_buf = 0;

  if (!params || index >= params->count) {
    return NULL;
  }

  route_param_t param = params->params[index];
  if (param.len >= sizeof(param_bufs[0]) - 1) {
    return NULL; // 参数太长
  }

  // 循环使用 4 个缓冲区
  int buf_index = current_buf % 4;
  current_buf++;

  memcpy(param_bufs[buf_index], param.ptr, param.len);
  param_bufs[buf_index][param.len] = '\0';

  return param_bufs[buf_index];
}

// ==================== JSON 解析辅助函数 ====================

int json_get_string(const char *json, size_t json_len, const char *key,
                    char **out_value, size_t *out_len) {
  (void)json_len;

  if (!json || !key || !out_value) {
    return -1;
  }

  char search_pattern[256];
  snprintf(search_pattern, sizeof(search_pattern), "\"%s\"", key);

  const char *key_pos = strstr(json, search_pattern);
  if (!key_pos) {
    return -1;
  }

  const char *p = key_pos + strlen(search_pattern);
  while (*p && (*p == ':' || *p == ' ' || *p == '\t')) {
    p++;
  }

  if (*p != '"') {
    return -1;
  }
  p++;

  const char *start = p;
  while (*p && *p != '"') {
    if (*p == '\\' && *(p + 1)) {
      p += 2;
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

// ==================== 指针转换工具 ====================

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

// ==================== 值编解码工具 ====================

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
    // 所有指针引用都返回 VALUE_TYPE_REF
    *out_type = VALUE_TYPE_REF;
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

// ==================== 路径解析工具 ====================

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
