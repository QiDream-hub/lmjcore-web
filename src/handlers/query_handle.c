// src/handlers/query_handle.c - 链式查询 HTTP 处理器
#include "cJSON.h"
#include "error_response.h"
#include "handle_utils.h"
#include "json_response.h"
#include "lmjcore.h"
#include "router.h"
#include "zlog.h"

#include <stddef.h>

// ==================== 链式查询处理器 ====================

int handle_obj_query(void *params, void *cbdata) {
  handle_params_t *hp = (handle_params_t *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // TODO: 链式查询尚未实现，当前返回占位结果
  cJSON *root = cJSON_CreateObject();
  if (!root || !cJSON_AddStringToObject(root, "path", "test") ||
      !cJSON_AddStringToObject(root, "value", "test") ||
      !cJSON_AddStringToObject(root, "type", "test")) {
    cJSON_Delete(root);
    RETURN_ERROR_NO_MEMORY(response);
  }

  return json_response_set(response, HTTP_STATUS_OK, root);
}
