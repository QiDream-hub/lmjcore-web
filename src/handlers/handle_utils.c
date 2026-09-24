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

// ==================== 事务管理辅助函数 ====================

/**
 * @brief 根据参数开启事务（支持外部传入或自动创建）
 * @param hp 处理器参数
 * @param txn_out 输出事务指针
 * @param flags 事务标志
 * @return LMJCORE_SUCCESS 成功，否则失败
 */
int handle_txn_begin(handle_params_t *hp, lmjcore_txn **txn_out, int flags) {
  if (!hp || !txn_out) {
    return LMJCORE_ERROR_NULL_POINTER;
  }

  // 如果已有外部事务，直接使用
  if (hp->txn) {
    *txn_out = hp->txn;
    return LMJCORE_SUCCESS;
  }

  // 否则创建新事务
  int rc = lmjcore_txn_begin(hp->env, NULL, flags, txn_out);
  return rc;
}

/**
 * @brief 根据参数提交/回滚事务（仅当自动管理时）
 * @param hp 处理器参数
 * @param txn 事务指针
 * @param success 是否成功
 * @return LMJCORE_SUCCESS 成功，否则失败
 */
int handle_txn_end(handle_params_t *hp, lmjcore_txn *txn, int success) {
  if (!hp || !txn) {
    return LMJCORE_ERROR_NULL_POINTER;
  }

  // 如果是外部事务，不自动管理
  if (hp->txn) {
    return LMJCORE_SUCCESS;
  }

  // 自动管理事务
  if (success) {
    return lmjcore_txn_commit(txn);
  } else {
    return lmjcore_txn_abort(txn);
  }
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
  // 检查整数溢出：value_len 不能太大
  if (value_len > out_buf_size - 1) {
    return LMJCORE_ERROR_BUFFER_TOO_SMALL;
  }

  out_buf[0] = LMJCORE_VALUE_TYPE_RAW;
  memcpy(out_buf + 1, value_str, value_len);
  *out_len = 1 + value_len;

  return LMJCORE_SUCCESS;
}

// ==================== 值编解码工具 ====================

const char *value_type_to_string(api_value_type_t type) {
  switch (type) {
  case VALUE_TYPE_RAW:
    return "raw";
  case VALUE_TYPE_REF:
    return "ref";
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
      *out_type = VALUE_TYPE_REF;
    }
    return LMJCORE_SUCCESS;

  case LMJCORE_VALUE_TYPE_RAW: {
    // 检查整数溢出：data_len 必须至少为 2（1 字节类型标记 + 至少 1 字节数据）
    if (data_len < 2) {
      return LMJCORE_ERROR_INVALID_PARAM;
    }
    // 防止溢出：检查 data_len - 1 是否合理
    size_t alloc_size = data_len;  // 多分配 1 字节给 '\0'
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

