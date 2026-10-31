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
  int txn_owned = 0;
  int rc = -1;
  cJSON *root = NULL;

  TXN_BEGIN(txn, hp, LMJCORE_TXN_READONLY, txn_owned, response, cleanup);
  TXN_CHECK_TIMEOUT(hp, txn, txn_owned, response, cleanup);

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, ptr);
  if (exists < 0) {
    rc = json_response_lmjcore_error(response, exists);
    goto cleanup;
  }

  if (exists == 0) {
    root = json_new_exist(false, NULL);
  } else {
    // 通过指针首字节判断实体类型
    lmjcore_entity_type etype = (lmjcore_entity_type)ptr[0];
    const char *type_str = (etype == LMJCORE_OBJ) ? "object" : "set";
    root = json_new_exist(true, type_str);
  }

  rc = json_response_set(response, HTTP_STATUS_OK, root);
  root = NULL; // 所有权已转移给响应

cleanup:
  TXN_ABORT(txn, txn_owned);
  cJSON_Delete(root);
  return rc;
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
