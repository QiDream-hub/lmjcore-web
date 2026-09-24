// src/handlers/utils_handle.c - 工具相关 HTTP 处理器
#include "error_response.h"
#include "handle_utils.h"
#include "json_response.h"
#include "lmjcore.h"

#include <stddef.h>
#include <time.h>

// 全局启动时间
static time_t g_start_time = 0;

// ==================== 工具处理器 ====================

int handle_ptr_exist(void *params, void *cbdata) {
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
  lmjcore_ptr ptr;
  if (lmjcore_ptr_from_string(ptr_str, ptr) != LMJCORE_SUCCESS) {
    RETURN_ERROR_INVALID_PTR(response);
  }

  // 开启读事务（批量操作时复用调用方的共享事务）
  lmjcore_txn *txn = NULL;
  int rc = handle_txn_begin(hp, &txn, LMJCORE_TXN_READONLY);
  if (rc != LMJCORE_SUCCESS || !txn) {
    RETURN_ERROR_TXN_FAILED("begin", response);
  }

  // 检查事务超时
  CHECK_TXN_TIMEOUT(hp, response, txn);

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, ptr);

  // 只读事务结束（共享事务由调用方统一管理）
  handle_txn_end(hp, txn, false);

  if (exists < 0) {
    json_response_lmjcore_error(response, exists);
    return -1;
  }

  if (exists == 0) {
    return json_response_set(response, HTTP_STATUS_OK,
                             json_new_exist(false, NULL));
  }

  // 检查实体类型
  // 通过指针首字节判断
  lmjcore_entity_type etype = (lmjcore_entity_type)ptr[0];
  const char *type_str = (etype == LMJCORE_OBJ) ? "object" : "set";

  return json_response_set(response, HTTP_STATUS_OK,
                           json_new_exist(true, type_str));
}

int handle_health(void *params, void *cbdata) {
  (void)params;  // 未使用参数

  http_response_t *response = (http_response_t *)cbdata;

  if (g_start_time == 0) {
    g_start_time = time(NULL);
  }

  long uptime = (long)(time(NULL) - g_start_time);

  return json_response_set(response, HTTP_STATUS_OK,
                           json_new_health("ok", uptime));
}
