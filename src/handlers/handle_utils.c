// src/handlers/handle_utils.c - 处理器通用工具函数实现
#include "handle_utils.h"
#include "lmjcore.h"
#include "error_codes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ==================== 路由参数辅助函数 ====================

const char *route_params_get(route_params_t *params, size_t index) {
  // 使用线程局部存储的多个缓冲区，支持最多 4 个参数同时使用
  static __thread char param_bufs[4][1024];
  static __thread int current_buf = 0;  // 显式初始化为 0

  if (!params || index >= params->count) {
    return NULL;
  }

  route_param_t param = params->params[index];
  // 限制拷贝长度，防止缓冲区溢出
  size_t copy_len = param.len;
  if (copy_len >= sizeof(param_bufs[0]) - 1) {
    copy_len = sizeof(param_bufs[0]) - 1;
  }

  // 循环使用 4 个缓冲区
  int buf_index = current_buf % 4;
  current_buf++;

  memcpy(param_bufs[buf_index], param.ptr, copy_len);
  param_bufs[buf_index][copy_len] = '\0';

  return param_bufs[buf_index];
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

int encode_type_from_string(const char *type_str) {
  if (!type_str) {
    return -1;
  }
  if (strcmp(type_str, "raw") == 0) {
    return LMJCORE_VALUE_TYPE_RAW;
  }
  if (strcmp(type_str, "ref") == 0) {
    return LMJCORE_VALUE_TYPE_PTR;
  }
  if (strcmp(type_str, "null") == 0) {
    return LMJCORE_VALUE_TYPE_NULL;
  }
  return -1;
}

int handle_parse_typed_value(const cJSON *body, const char **out_value,
                             size_t *out_value_len, int *out_type) {
  if (!body || !out_value || !out_value_len || !out_type) {
    return LMJCORE_ERROR_INVALID_PARAM;
  }

  const cJSON *value_item = cJSON_GetObjectItemCaseSensitive(body, "value");
  const cJSON *type_item = cJSON_GetObjectItemCaseSensitive(body, "type");
  if (!cJSON_IsString(value_item) || !cJSON_IsString(type_item)) {
    return LMJCORE_ERROR_INVALID_PARAM;
  }

  int type = encode_type_from_string(type_item->valuestring);
  if (type < 0) {
    return LMJCORE_ERROR_INVALID_PARAM;
  }

  *out_value = value_item->valuestring;
  *out_value_len = strlen(value_item->valuestring);
  *out_type = type;
  return LMJCORE_SUCCESS;
}

int lmjcore_encode_value(const char *value_str, size_t value_len, int type,
                         uint8_t *out_buf, size_t out_buf_size,
                         size_t *out_len) {
  if (!value_str || !out_buf || !out_len) {
    return LMJCORE_ERROR_NULL_POINTER;
  }

  if (out_buf_size < 1 + LMJCORE_PTR_LEN) {
    return LMJCORE_ERROR_BUFFER_TOO_SMALL;
  }

  switch (type) {
  case LMJCORE_VALUE_TYPE_NULL:
    out_buf[0] = LMJCORE_VALUE_TYPE_NULL;
    *out_len = 1;
    return LMJCORE_SUCCESS;

  case LMJCORE_VALUE_TYPE_PTR: {
    if (value_len != LMJCORE_PTR_STRING_LEN) {
      return LMJCORE_ERROR_INVALID_PARAM;
    }
    uint8_t ptr[LMJCORE_PTR_LEN];
    if (lmjcore_ptr_from_hex(value_str, ptr) != 0 ||
        (ptr[0] != LMJCORE_OBJ && ptr[0] != LMJCORE_SET)) {
      return LMJCORE_ERROR_INVALID_PARAM;
    }
    out_buf[0] = LMJCORE_VALUE_TYPE_PTR;
    memcpy(out_buf + 1, ptr, LMJCORE_PTR_LEN);
    *out_len = 1 + LMJCORE_PTR_LEN;
    return LMJCORE_SUCCESS;
  }

  case LMJCORE_VALUE_TYPE_RAW:
    if (value_len > out_buf_size - 1) {
      return LMJCORE_ERROR_BUFFER_TOO_SMALL;
    }
    out_buf[0] = LMJCORE_VALUE_TYPE_RAW;
    memcpy(out_buf + 1, value_str, value_len);
    *out_len = 1 + value_len;
    return LMJCORE_SUCCESS;

  default:
    return LMJCORE_ERROR_INVALID_PARAM;
  }
}

// ==================== 值编解码工具 ====================

const char *value_type_to_string(api_value_type_t type) {
  switch (type) {
  case VALUE_TYPE_RAW:
    return "raw";
  case VALUE_TYPE_SET:
    return "set";
  case VALUE_TYPE_OBJECT:
    return "object";
  case VALUE_TYPE_NULL:
    return "null";
  default:
    return "unknown";
  }
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
    if (!*out_str) {
      return LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED;
    }
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
    // 根据指针第 1 字节区分对象/集合
    uint8_t ptr_type = data[1];
    if (ptr_type == LMJCORE_OBJ) {
      *out_type = VALUE_TYPE_OBJECT;
    } else if (ptr_type == LMJCORE_SET) {
      *out_type = VALUE_TYPE_SET;
    } else {
      free(*out_str);
      *out_str = NULL;
      return LMJCORE_ERROR_INVALID_TYPE;
    }
    return LMJCORE_SUCCESS;

  case LMJCORE_VALUE_TYPE_RAW: {
    // 允许零长度原始数据（如空字符串）：[0x00]（仅类型标记）
    size_t alloc_size = data_len; // 多分配 1 字节给 '\0'
    *out_str = (char *)malloc(alloc_size);
    if (!*out_str) {
      return LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED;
    }
    memcpy(*out_str, data + 1, data_len - 1);
    (*out_str)[data_len - 1] = '\0';
    *out_type = VALUE_TYPE_RAW;
    return LMJCORE_SUCCESS;
  }

  default:
    return LMJCORE_ERROR_INVALID_PARAM;
  }
}

// ==================== 成员值读取（受限） ====================

int lmjcore_obj_member_get_capped(lmjcore_txn *txn, const lmjcore_ptr obj_ptr,
                                  const char *member_name,
                                  size_t member_name_len, size_t max_bytes,
                                  uint8_t **out_buf, size_t *out_len) {
  if (!txn || !obj_ptr || !member_name || !out_buf || !out_len) {
    return LMJCORE_ERROR_NULL_POINTER;
  }
  if (max_bytes == 0) {
    max_bytes = HANDLE_DEFAULT_MAX_VALUE_BYTES;
  }
  if (max_bytes < 1) {
    return LMJCORE_ERROR_VALUE_TOO_LARGE;
  }

  // 初始容量：小值一次命中；大值按倍增重试
  size_t capacity = 1024;
  if (capacity > max_bytes) {
    capacity = max_bytes;
  }

  uint8_t *buf = (uint8_t *)malloc(capacity);
  if (!buf) {
    return LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED;
  }

  for (;;) {
    size_t got = 0;
    int rc = lmjcore_obj_member_get(txn, obj_ptr, (const uint8_t *)member_name,
                                    member_name_len, buf, capacity, &got);
    if (rc == LMJCORE_SUCCESS) {
      *out_buf = buf;
      *out_len = got;
      return LMJCORE_SUCCESS;
    }
    if (rc != LMJCORE_ERROR_BUFFER_TOO_SMALL) {
      free(buf);
      return rc;
    }
    if (capacity >= max_bytes) {
      free(buf);
      return LMJCORE_ERROR_VALUE_TOO_LARGE;
    }

    size_t next = capacity * 2;
    if (next > max_bytes) {
      next = max_bytes;
    }
    uint8_t *grown = (uint8_t *)realloc(buf, next);
    if (!grown) {
      free(buf);
      return LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED;
    }
    buf = grown;
    capacity = next;
  }
}

// ==================== 事务超时检查工具 ====================

time_t lmjcore_txn_get_start_time(void) {
  return time(NULL);
}

bool lmjcore_txn_check_timeout(time_t start_time, int timeout) {
  if (timeout <= 0) {
    return false; // 超时禁用
  }

  time_t current_time = time(NULL);
  return difftime(current_time, start_time) >= timeout;
}

// ==================== URL 解码工具 ====================

static int hex_char_to_int(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

int url_decode(const char *src, size_t src_len, char *out_buf,
               size_t out_buf_size) {
  if (!src || !out_buf || out_buf_size == 0) {
    return -1;
  }

  size_t j = 0; // 输出缓冲区索引

  for (size_t i = 0; i < src_len; i++) {
    if (src[i] == '%' && i + 2 < src_len) {
      // 解析 %XX 编码
      int high = hex_char_to_int(src[i + 1]);
      int low = hex_char_to_int(src[i + 2]);

      if (high >= 0 && low >= 0) {
        // 缓冲区检查（预留 1 字节给'\0'）
        if (j >= out_buf_size - 1) {
          return -1;
        }
        out_buf[j++] = (char)((high << 4) | low);
        i += 2; // 跳过两个十六进制字符
        continue;
      }
    }

    // 普通字符或无效的%编码，直接复制
    if (j >= out_buf_size - 1) {
      return -1;
    }
    out_buf[j++] = src[i];
  }

  out_buf[j] = '\0';
  return (int)j;
}

