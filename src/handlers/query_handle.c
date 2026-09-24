// src/handlers/query_handle.c - 链式查询（深度访问）HTTP 处理器
#include "cJSON.h"
#include "error_response.h"
#include "handle_utils.h"
#include "json_response.h"
#include "lmjcore.h"
#include "query_path.h"
#include "router.h"

#include <stddef.h>
#include <stdlib.h>

// ==================== 错误信息 ====================

/**
 * @brief 查询错误码 -> 对用户可读的消息
 */
static const char *query_error_message(int rc) {
  switch (rc) {
  case LMJCORE_ERROR_PATH_PARSE:
    return "Invalid query path";
  case LMJCORE_ERROR_PATH_INVALID_PTR:
    return "Invalid pointer format";
  case LMJCORE_ERROR_PATH_TOO_DEEP:
    return "Query path too deep";
  case LMJCORE_ERROR_PATH_URL_DECODE:
    return "Failed to decode query path segment";
  case LMJCORE_ERROR_SET_NOT_SUPPORTED:
    return "Set does not support member access";
  case LMJCORE_ERROR_TYPE_MISMATCH:
    return "Intermediate value is not a reference";
  case LMJCORE_ERROR_VALUE_TOO_LARGE:
    return "Value too large";
  case LMJCORE_ERROR_ENTITY_NOT_FOUND:
    return "Object not found";
  case LMJCORE_ERROR_MEMBER_NOT_FOUND:
    return "Member not found";
  default:
    return lmjcore_strerror(rc);
  }
}

/**
 * @brief 构建错误响应体（失败返回 NULL）
 *
 * 有失败位置时附带 "at"/"segment"，中间值类型可判定时附带 "found"，
 * 便于在 batch 的 failed_at 之外定位到具体哪一段出错。
 */
static cJSON *build_query_error(int rc, const query_error_t *err,
                                size_t max_value_bytes) {
  cJSON *root = cJSON_CreateObject();
  if (!root) {
    return NULL;
  }

  int ok = cJSON_AddStringToObject(root, "error", query_error_message(rc)) != NULL;

  if (ok && err && err->at) {
    ok = cJSON_AddStringToObject(root, "at", err->at) != NULL &&
         cJSON_AddNumberToObject(root, "segment", (double)err->segment) != NULL;
  }
  if (ok && err && err->has_found_type) {
    ok = cJSON_AddStringToObject(root, "found",
                                 value_type_to_string(err->found_type)) != NULL;
  }
  if (ok && rc == LMJCORE_ERROR_VALUE_TOO_LARGE) {
    ok = cJSON_AddNumberToObject(root, "limit", (double)max_value_bytes) != NULL;
  }

  if (!ok) {
    cJSON_Delete(root);
    return NULL;
  }
  return root;
}

// ==================== 链式查询处理器 ====================

/**
 * @brief GET /obj/query?path=<34位指针>[.<成员名>...] - 深度访问
 *
 * 沿对象图走一条确定路径并返回叶子值。只读、单值、O(深度)：
 *   - 根段必须是对象指针（01 开头）
 *   - 中间值必须是指针引用，且指向对象（集合不支持成员访问）
 *   - 成员名中的字面 '.' 写作 %2E
 *
 * 与 GET /batch 组合即可一次读取多条路径（共享同一只读事务快照）。
 */
int handle_obj_query(void *params, void *cbdata) {
  handle_params_t *hp = (handle_params_t *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  const char *path_str = route_params_get(hp->params, 0);
  if (!path_str) {
    RETURN_ERROR_MISSING_PARAM("path", response);
  }

  // 只读事务（批量操作时复用调用方的共享事务，保证同一 MVCC 快照）
  lmjcore_txn *txn = NULL;
  int rc = handle_txn_begin(hp, &txn, LMJCORE_TXN_READONLY);
  if (rc != LMJCORE_SUCCESS || !txn) {
    RETURN_ERROR_TXN_FAILED("begin", response);
  }

  // 检查事务超时
  CHECK_TXN_TIMEOUT(hp, response, txn);

  query_options_t options;
  size_t max_depth = (hp->query_max_depth > 0) ? (size_t)hp->query_max_depth : 0;
  query_options_init(&options, max_depth, hp->max_value_bytes);

  char *value = NULL;
  api_value_type_t type = VALUE_TYPE_NULL;
  query_error_t err = {0};
  rc = query_path_execute(txn, path_str, &options, &value, &type, &err);

  // 只读事务结束（共享事务由调用方统一管理）
  handle_txn_end(hp, txn, false);

  if (rc != LMJCORE_SUCCESS) {
    cJSON *root = build_query_error(rc, &err, options.max_value_bytes);
    query_error_free(&err);
    if (!root) {
      RETURN_ERROR_NO_MEMORY(response);
    }
    json_response_set(response, lmjcore_error_to_http_status(rc), root);
    return -1;
  }

  // 成功：{"path":"<原路径>","value":"...","type":"..."}
  cJSON *root = json_new_value_entry("path", path_str, value,
                                     value_type_to_string(type));
  free(value);
  query_error_free(&err);

  return json_response_set(response, HTTP_STATUS_OK, root);
}
