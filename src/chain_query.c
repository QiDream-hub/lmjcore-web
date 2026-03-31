#include "../include/chain_query.h"
#include "../include/errors.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 路径分段
int chain_query_split_path(const char *path, char **segments,
                           int max_segments) {
  if (!path || !segments || max_segments <= 0) {
    return -1;
  }

  char *path_copy = strdup(path);
  if (!path_copy) {
    return -1;
  }

  int count = 0;
  char *token = strtok(path_copy, ".");
  while (token && count < max_segments) {
    segments[count] = strdup(token);
    count++;
    token = strtok(NULL, ".");
  }

  free(path_copy);
  return count;
}

bool chain_query_is_valid_path(const char *path) {
  if (!path || strlen(path) == 0) {
    return false;
  }

  // 检查路径字符
  for (const char *p = path; *p; p++) {
    if (!isalnum(*p) && *p != '.' && *p != '_' && *p != '-') {
      return false;
    }
  }

  // 不能以点开头或结尾
  if (path[0] == '.' || path[strlen(path) - 1] == '.') {
    return false;
  }

  // 不能有连续的点
  if (strstr(path, "..")) {
    return false;
  }

  return true;
}

// 获取集合所有元素
int chain_query_get_set_all(lmjcore_txn *txn, const lmjcore_ptr set_ptr,
                            uint8_t **out_data, size_t *out_len) {
  if (!txn || !set_ptr || !out_data || !out_len) {
    return LMJCORE_ERROR_INVALID_PARAM;
  }

  // 获取集合统计信息
  size_t total_len = 0;
  size_t element_count = 0;
  int rc = lmjcore_set_stat(txn, set_ptr, &total_len, &element_count);
  if (rc != LMJCORE_SUCCESS) {
    return rc;
  }

  if (element_count == 0) {
    *out_data = NULL;
    *out_len = 0;
    return LMJCORE_SUCCESS;
  }

  // 分配缓冲区
  size_t buf_size = sizeof(lmjcore_result_set) +
                    element_count * sizeof(lmjcore_descriptor) + total_len;
  uint8_t *buf = malloc(buf_size);
  if (!buf) {
    return LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED;
  }

  lmjcore_result_set *result;
  rc = lmjcore_set_get(txn, set_ptr, buf, buf_size, &result);
  if (rc != LMJCORE_SUCCESS) {
    free(buf);
    return rc;
  }

  // 构造输出
  // 格式: [4B count][4B total_len][data...]
  size_t output_len = 8 + total_len;
  uint8_t *output = malloc(output_len);
  if (!output) {
    free(buf);
    return LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED;
  }

  uint32_t count_be = htobe32(element_count);
  uint32_t total_len_be = htobe32(total_len);
  memcpy(output, &count_be, 4);
  memcpy(output + 4, &total_len_be, 4);

  // 复制所有元素数据
  size_t offset = 8;
  for (size_t i = 0; i < result->element_count; i++) {
    lmjcore_descriptor *desc = &result->elements[i];
    memcpy(output + offset, buf + desc->value_offset, desc->value_len);
    offset += desc->value_len;
  }

  *out_data = output;
  *out_len = output_len;

  free(buf);
  return LMJCORE_SUCCESS;
}

// 解析链式查询
int chain_query_parse(lmjcore_txn *txn, const lmjcore_ptr root_ptr,
                      const char *path, query_result_t *result) {
  if (!txn || !root_ptr || !path || !result) {
    return LMJCORE_ERROR_INVALID_PARAM;
  }

  if (!chain_query_is_valid_path(path)) {
    return LMJCORE_WEB_ERROR_INVALID_PATH;
  }

  memset(result, 0, sizeof(query_result_t));

  // 分段路径
  char *segments[32];
  int segment_count = chain_query_split_path(path, segments, 32);
  if (segment_count <= 0) {
    return LMJCORE_WEB_ERROR_INVALID_PATH;
  }

  lmjcore_ptr current_ptr;
  memcpy(current_ptr, root_ptr, LMJCORE_PTR_LEN);
  uint8_t *current_value = NULL;
  size_t current_len = 0;

  int rc = LMJCORE_SUCCESS;

  for (int i = 0; i < segment_count; i++) {
    // 检查当前实体是否存在
    rc = lmjcore_entity_exist(txn, current_ptr);
    if (rc <= 0) {
      rc = LMJCORE_ERROR_ENTITY_NOT_FOUND;
      break;
    }

    // 获取实体类型
    int entity_type = current_ptr[0];

    if (entity_type == LMJCORE_OBJ) {
      // 对象: 获取成员
      if (current_value) {
        free(current_value);
        current_value = NULL;
      }

      // 分配临时缓冲区
      uint8_t temp_buf[4096];
      rc = lmjcore_obj_member_get(
          txn, current_ptr, (const uint8_t *)segments[i], strlen(segments[i]),
          temp_buf, sizeof(temp_buf), &current_len);

      if (rc != LMJCORE_SUCCESS) {
        break;
      }

      // 复制值
      current_value = malloc(current_len);
      if (!current_value) {
        rc = LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED;
        break;
      }
      memcpy(current_value, temp_buf, current_len);

      // 检查是否是指针
      if (current_len == LMJCORE_PTR_LEN) {
        memcpy(current_ptr, current_value, LMJCORE_PTR_LEN);
        free(current_value);
        current_value = NULL;
        continue;
      }

      // 如果不是最后一层，报错
      if (i < segment_count - 1) {
        rc = LMJCORE_ERROR_ENTITY_TYPE_MISMATCH;
        break;
      }

    } else if (entity_type == LMJCORE_SET) {
      // 集合: 只支持最后一层
      if (i != segment_count - 1) {
        rc = LMJCORE_ERROR_ENTITY_TYPE_MISMATCH;
        break;
      }

      // 特殊处理 "all" 关键字
      if (strcmp(segments[i], "all") == 0) {
        uint8_t *set_data;
        size_t set_len;
        rc = chain_query_get_set_all(txn, current_ptr, &set_data, &set_len);
        if (rc == LMJCORE_SUCCESS) {
          result->type = QUERY_RESULT_SET_ALL;
          result->data = set_data;
          result->data_len = set_len;
        }
        break;
      }

      // 检查元素是否存在
      rc = lmjcore_set_contains(txn, current_ptr, (const uint8_t *)segments[i],
                                strlen(segments[i]));
      if (rc == 1) {
        result->type = QUERY_RESULT_VALUE;
        result->data = (uint8_t *)strdup(segments[i]);
        result->data_len = strlen(segments[i]);
        rc = LMJCORE_SUCCESS;
      } else if (rc == 0) {
        rc = LMJCORE_ERROR_MEMBER_NOT_FOUND;
      }
      break;
    } else {
      rc = LMJCORE_ERROR_ENTITY_TYPE_MISMATCH;
      break;
    }
  }

  // 清理
  for (int i = 0; i < segment_count; i++) {
    if (segments[i]) {
      free(segments[i]);
    }
  }

  if (rc == LMJCORE_SUCCESS) {
    // 普通值结果
    if (current_value) {
      result->type = QUERY_RESULT_VALUE;
      result->data = current_value;
      result->data_len = current_len;
    }
  } else if (rc != LMJCORE_SUCCESS) {
    if (current_value) {
      free(current_value);
    }
  }

  return rc;
}

void chain_query_result_free(query_result_t *result) {
  if (result && result->data) {
    free(result->data);
    result->data = NULL;
  }
}