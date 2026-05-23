// src/handlers/query_handle.c - 链式查询 HTTP 处理器
#include "cJSON.h"
#include "error_response.h"
#include "handle_utils.h"
#include "lmjcore.h"
#include "zlog.h"
#include "router.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==================== 链式查询处理器 ====================

int handle_obj_query(void *params, void *cbdata) {
  handle_params_t *hp = (handle_params_t *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    RETURN_ERROR_INVALID_PARAM(response);
  }
  dzlog_error("开始处理");

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

  // 检查是否已有事务（批量操作场景）
  lmjcore_txn *txn = NULL;
  int auto_commit = 1;  // 是否自动提交事务

  if (hp->txn && !hp->auto_manage_txn) {
    // 使用已有事务（批量操作场景）
    txn = hp->txn;
    auto_commit = 0;
  } else {
    // 开启读事务
    rc = lmjcore_txn_begin(hp->env, NULL, LMJCORE_TXN_READONLY, &txn);
    if (rc != LMJCORE_SUCCESS || !txn) {
      lmjcore_free_path_parse_result(start_ptr, segments, segment_count);
      RETURN_ERROR_TXN_FAILED("begin", response);
    }
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
      if (auto_commit) {
        lmjcore_txn_abort(txn);
      }
      lmjcore_free_path_parse_result(start_ptr, segments, segment_count);
      free(current_value);
      RETURN_ERROR_NOT_FOUND("Entity", response);
    }

    // 读取当前对象的成员
    size_t value_buf_size = 4096;
    uint8_t *value_buf = (uint8_t *)malloc(value_buf_size);
    if (!value_buf) {
      if (auto_commit) {
        lmjcore_txn_abort(txn);
      }
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
      if (auto_commit) {
        lmjcore_txn_abort(txn);
      }
      lmjcore_free_path_parse_result(start_ptr, segments, segment_count);
      free(current_value);
      RETURN_ERROR_MEMBER_NOT_FOUND(response);
    }

    if (rc != LMJCORE_SUCCESS) {
      free(value_buf);
      if (auto_commit) {
        lmjcore_txn_abort(txn);
      }
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
      if (auto_commit) {
        lmjcore_txn_abort(txn);
      }
      lmjcore_free_path_parse_result(start_ptr, segments, segment_count);
      free(current_value);
      build_error_response(HTTP_STATUS_BAD_REQUEST,
                           "Intermediate value is not a reference", response);
      return -1;
    }

    // 转换指针字符串为二进制
    if (lmjcore_ptr_from_string(current_value, obj_ptr) != LMJCORE_SUCCESS) {
      if (auto_commit) {
        lmjcore_txn_abort(txn);
      }
      lmjcore_free_path_parse_result(start_ptr, segments, segment_count);
      free(current_value);
      build_error_response(HTTP_STATUS_BAD_REQUEST,
                           "Invalid reference format", response);
      return -1;
    }
  }

  // 读事务完成，仅在自动管理时中止
  if (auto_commit) {
    lmjcore_txn_abort(txn);
  }
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
