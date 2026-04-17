// src/handlers/utils_handle.c - 工具相关 HTTP 处理器
#include "error_response.h"
#include "handle_utils.h"
#include "lmjcore.h"
#include "router.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// 全局启动时间
static time_t g_start_time = 0;

// ==================== 事务超时检查宏 ====================

#define CHECK_TXN_TIMEOUT(hp, response, txn)                                 \
  do {                                                                       \
    if (lmjcore_txn_check_timeout((hp)->txn_start_time, (hp)->txn_timeout)) {\
      if (txn) lmjcore_txn_abort(txn);                                       \
      RETURN_ERROR_TXN_TIMEOUT(response);                                    \
    }                                                                        \
  } while (0)

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

  // 开启读事务
  lmjcore_txn *txn = NULL;
  int rc = lmjcore_txn_begin(hp->env, NULL, LMJCORE_TXN_READONLY, &txn);
  if (rc != LMJCORE_SUCCESS || !txn) {
    RETURN_ERROR_TXN_FAILED("begin", response);
  }

  // 检查事务超时
  CHECK_TXN_TIMEOUT(hp, response, txn);

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, ptr);

  lmjcore_txn_abort(txn);

  if (exists < 0) {
    build_lmjcore_error_response(exists, response);
    return -1;
  }

  if (exists == 0) {
    return build_success_response(HTTP_STATUS_OK, "{\"exist\":false}", response);
  }

  // 检查实体类型
  // 通过指针首字节判断
  lmjcore_entity_type etype = (lmjcore_entity_type)ptr[0];
  const char *type_str = (etype == LMJCORE_OBJ) ? "object" : "set";

  char json_buf[128];
  snprintf(json_buf, sizeof(json_buf), "{\"exist\":true,\"type\":\"%s\"}",
           type_str);

  return build_success_response(HTTP_STATUS_OK, json_buf, response);
}

int handle_health(void *params, void *cbdata) {
  (void)params;  // 未使用参数

  http_response_t *response = (http_response_t *)cbdata;

  if (g_start_time == 0) {
    g_start_time = time(NULL);
  }

  time_t uptime = time(NULL) - g_start_time;

  char json_buf[256];
  snprintf(json_buf, sizeof(json_buf), "{\"status\":\"ok\",\"uptime\":%ld}",
           (long)uptime);

  return build_success_response(HTTP_STATUS_OK, json_buf, response);
}
