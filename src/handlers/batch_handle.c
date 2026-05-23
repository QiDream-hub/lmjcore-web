// src/handlers/batch_handle.c - 批量操作处理器 (事务复用架构)
#include "cJSON.h"
#include "error_response.h"
#include "handle_utils.h"
#include "lmjcore.h"
#include "lmjcore_handle.h"
#include "router.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==================== 常量定义 ====================

#define MAX_OPERATIONS 1000

// ==================== 操作结果结构 ====================

typedef struct {
  int status_code;
  char *body;
  size_t body_len;
} batch_result_t;

// ==================== 内部调用包装器 ====================

/**
 * @brief 内部调用处理器，复用现有路由处理器逻辑
 * @param handler 处理器函数指针
 * @param params 参数结构
 * @param response 响应结构
 * @return 0=成功，-1=失败
 */
typedef int (*handler_fn)(void *, void *);

static int invoke_handler(handler_fn handler, handle_params_t *params,
                          http_response_t *response) {
  // 清空响应
  if (response->body) {
    free(response->body);
    response->body = NULL;
    response->body_len = 0;
  }

  // 调用处理器
  return handler(params, response);
}

// ==================== 解析操作并调用处理器 ====================

/**
 * @brief 解析单个操作，构建参数，调用对应处理器
 */
static int execute_operation(handle_params_t *hp, const cJSON *op_obj,
                             batch_result_t *result) {
  // 解析 method 和 path
  cJSON *method_item = cJSON_GetObjectItemCaseSensitive(op_obj, "method");
  cJSON *path_item = cJSON_GetObjectItemCaseSensitive(op_obj, "path");

  if (!method_item || !cJSON_IsString(method_item) ||
      !path_item || !cJSON_IsString(path_item)) {
    result->status_code = HTTP_STATUS_BAD_REQUEST;
    result->body = strdup("{\"error\":\"Missing method or path\"}");
    result->body_len = strlen(result->body);
    return -1;
  }

  const char *method_str = method_item->valuestring;
  const char *path = path_item->valuestring;

  // 解析 HTTP 方法
  http_method_t method;
  if (strcmp(method_str, "GET") == 0) {
    method = HTTP_GET;
  } else if (strcmp(method_str, "POST") == 0) {
    method = HTTP_POST;
  } else if (strcmp(method_str, "PUT") == 0) {
    method = HTTP_PUT;
  } else if (strcmp(method_str, "DELETE") == 0) {
    method = HTTP_DELETE;
  } else {
    result->status_code = HTTP_STATUS_BAD_REQUEST;
    result->body = strdup("{\"error\":\"Invalid HTTP method\"}");
    result->body_len = strlen(result->body);
    return -1;
  }

  // 使用路由器匹配路径（复用 routes.c 中注册的路由）
  if (!hp->router) {
    result->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
    result->body = strdup("{\"error\":\"Router not configured\"}");
    result->body_len = strlen(result->body);
    return -1;
  }

  route_node_t *node = router_match(hp->router, method, path);
  if (!node) {
    result->status_code = HTTP_STATUS_NOT_FOUND;
    result->body = strdup("{\"error\":\"Route not found\"}");
    result->body_len = strlen(result->body);
    return -1;
  }

  // 提取路由参数
  route_param_t param_storage[2];
  route_params_t params = {0};
  size_t param_count = 0;

  if (router_extract(node, path, param_storage, 2, &param_count) != 0) {
    result->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
    result->body = strdup("{\"error\":\"Failed to extract route params\"}");
    result->body_len = strlen(result->body);
    return -1;
  }

  params.params = param_storage;
  params.count = param_count;

  // 解析 body（如果有）
  char *body_str = NULL;
  cJSON *body_item = cJSON_GetObjectItemCaseSensitive(op_obj, "body");
  if (body_item && cJSON_IsObject(body_item)) {
    cJSON *value_item = cJSON_GetObjectItemCaseSensitive(body_item, "value");
    if (value_item && cJSON_IsString(value_item)) {
      cJSON *body_obj = cJSON_CreateObject();
      cJSON_AddStringToObject(body_obj, "value", value_item->valuestring);
      body_str = cJSON_PrintUnformatted(body_obj);
      cJSON_Delete(body_obj);
    }
  }

  // 构建 handle_params
  handle_params_t local_params = {
    .params = &params,
    .env = hp->env,
    .txn = hp->txn,           // 共享事务
    .router = hp->router,     // 传递路由器（嵌套批量操作时使用）
    .body = body_str,
    .body_len = body_str ? strlen(body_str) : 0,
    .txn_timeout = hp->txn_timeout,
    .txn_start_time = hp->txn_start_time,
    .auto_manage_txn = false  // 批量操作中不自动管理事务
  };

  // 响应
  http_response_t response = {0};

  // 获取并调用处理器
  handler_fn handler = (handler_fn)router_get_callback(node);
  if (!handler) {
    result->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
    result->body = strdup("{\"error\":\"Handler not found\"}");
    result->body_len = strlen(result->body);
    if (body_str) free(body_str);
    return -1;
  }

  // 调用处理器
  int rc = invoke_handler(handler, &local_params, &response);

  // 复制结果
  result->status_code = response.status_code;
  if (response.body) {
    result->body = strdup(response.body);
    result->body_len = response.body_len;
    free(response.body);
  }

  // 清理
  if (body_str) free(body_str);
  if (rc != 0 && result->status_code >= 400) {
    return -1;
  }
  return 0;
}

// ==================== 内部核心处理器 ====================

/**
 * @brief 批量操作核心处理器（内部使用）
 * @param hp 处理参数
 * @param root 请求体 JSON 对象
 * @param readonly 是否只读事务
 * @param response 响应结构
 * @return 0=成功，-1=失败
 */
static int handle_batch_core(handle_params_t *hp, cJSON *root, int readonly,
                             http_response_t *response) {
  // 获取 operations 数组
  cJSON *operations = cJSON_GetObjectItemCaseSensitive(root, "operations");
  if (!operations || !cJSON_IsArray(operations)) {
    build_error_response(HTTP_STATUS_BAD_REQUEST,
                         "Missing or invalid 'operations' array", response);
    return -1;
  }

  int op_count = cJSON_GetArraySize(operations);
  if (op_count <= 0) {
    build_error_response(HTTP_STATUS_BAD_REQUEST,
                         "Operations array is empty", response);
    return -1;
  }

  if (op_count > MAX_OPERATIONS) {
    build_error_response(HTTP_STATUS_BAD_REQUEST,
                         "Too many operations (max 1000)", response);
    return -1;
  }

  // 写事务检查：检查操作中是否包含写操作（只读路由使用）
  if (readonly) {
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, operations) {
      if (!cJSON_IsObject(item)) continue;
      cJSON *method_item = cJSON_GetObjectItemCaseSensitive(item, "method");
      if (method_item && cJSON_IsString(method_item)) {
        const char *method = method_item->valuestring;
        if (strcmp(method, "PUT") == 0 || strcmp(method, "POST") == 0 ||
            strcmp(method, "DELETE") == 0) {
          build_error_response(HTTP_STATUS_BAD_REQUEST,
                               "Readonly transaction cannot contain write operations",
                               response);
          return -1;
        }
      }
    }
  }

  // 开启事务
  lmjcore_txn *txn = NULL;
  int txn_flags = readonly ? LMJCORE_TXN_READONLY : 0;
  int rc = lmjcore_txn_begin(hp->env, NULL, txn_flags, &txn);
  if (rc != LMJCORE_SUCCESS || !txn) {
    cJSON_Delete(root);
    RETURN_ERROR_TXN_FAILED("begin", response);
  }

  // 更新 hp->txn 供子处理器使用
  hp->txn = txn;
  hp->auto_manage_txn = false;

  // 分配结果数组
  batch_result_t *results = (batch_result_t *)calloc((size_t)op_count, sizeof(batch_result_t));
  if (!results) {
    lmjcore_txn_abort(txn);
    cJSON_Delete(root);
    RETURN_ERROR_NO_MEMORY(response);
  }

  // 执行所有操作
  int failed_index = -1;
  cJSON *item = NULL;
  int index = 0;

  cJSON_ArrayForEach(item, operations) {
    if (index >= op_count) break;

    // 检查事务超时
    if (lmjcore_txn_check_timeout(hp->txn_start_time, hp->txn_timeout)) {
      // 释放之前成功的操作结果
      for (int i = 0; i < index; i++) {
        if (results[i].body) {
          free(results[i].body);
        }
      }
      free(results);
      lmjcore_txn_abort(txn);
      cJSON_Delete(root);
      RETURN_ERROR_TXN_TIMEOUT(response);
    }

    if (!cJSON_IsObject(item)) {
      results[index].status_code = HTTP_STATUS_BAD_REQUEST;
      results[index].body = strdup("{\"error\":\"Invalid operation format\"}");
      results[index].body_len = strlen(results[index].body);
      failed_index = index;
      break;
    }

    if (execute_operation(hp, item, &results[index]) != 0) {
      failed_index = index;
      break;
    }

    index++;
  }

  // 处理结果
  if (failed_index >= 0) {
    // 回滚事务
    lmjcore_txn_abort(txn);

    // 释放之前成功的操作结果
    for (int i = 0; i < failed_index; i++) {
      if (results[i].body) {
        free(results[i].body);
      }
    }

    // 构建错误响应
    cJSON *error = cJSON_CreateObject();
    cJSON_AddBoolToObject(error, "success", false);
    cJSON_AddNumberToObject(error, "failed_at", failed_index);

    if (results[failed_index].body) {
      cJSON *details = cJSON_Parse(results[failed_index].body);
      if (details) {
        cJSON_AddItemToObject(error, "details", details);
      } else {
        cJSON_AddStringToObject(error, "details", results[failed_index].body);
      }
    }

    // 直接设置响应，避免 build_error_response 再次转义 JSON
    response->status_code = HTTP_STATUS_BAD_REQUEST;
    response->body = cJSON_PrintUnformatted(error);
    response->body_len = strlen(response->body);
    cJSON_Delete(error);

    // 清理
    free(results);
    cJSON_Delete(root);
    return -1;
  } else {
    // 提交事务
    if (readonly) {
      lmjcore_txn_abort(txn);
    } else {
      rc = lmjcore_txn_commit(txn);
      if (rc != LMJCORE_SUCCESS) {
        lmjcore_txn_abort(txn);
        build_error_response(HTTP_STATUS_INTERNAL_SERVER_ERROR,
                             "Failed to commit transaction", response);
        cJSON_Delete(root);
        free(results);
        return -1;
      }
    }

    // 构建成功响应
    cJSON *result_obj = cJSON_CreateObject();
    cJSON_AddBoolToObject(result_obj, "success", true);
    cJSON *results_array = cJSON_AddArrayToObject(result_obj, "results");

    for (int i = 0; i < op_count; i++) {
      cJSON *result_item = cJSON_CreateObject();
      cJSON_AddNumberToObject(result_item, "status", results[i].status_code);

      cJSON *body_json = cJSON_Parse(results[i].body);
      if (body_json) {
        cJSON_AddItemToObject(result_item, "body", body_json);
      } else {
        cJSON_AddStringToObject(result_item, "body", results[i].body);
      }

      cJSON_AddItemToArray(results_array, result_item);
      free(results[i].body);
    }

    response->status_code = HTTP_STATUS_OK;
    response->body = cJSON_PrintUnformatted(result_obj);
    response->body_len = strlen(response->body);

    cJSON_Delete(result_obj);
  }

  // 清理
  free(results);
  cJSON_Delete(root);

  return 0;
}

// ==================== 公开处理器 ====================

/**
 * @brief GET /batch - 只读批量操作
 */
int handle_batch_get(void *params, void *cbdata) {
  handle_params_t *hp = (handle_params_t *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->body) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 解析请求体
  cJSON *root = cJSON_ParseWithOpts(hp->body, NULL, 0);
  if (!root) {
    build_error_response(HTTP_STATUS_BAD_REQUEST,
                         "Invalid JSON in request body", response);
    return -1;
  }

  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    build_error_response(HTTP_STATUS_BAD_REQUEST,
                         "Request body must be a JSON object", response);
    return -1;
  }

  int ret = handle_batch_core(hp, root, 1, response);
  if (ret != 0) {
    return -1;
  }

  return 0;
}

/**
 * @brief POST /batch - 写操作批量操作
 */
int handle_batch_post(void *params, void *cbdata) {
  handle_params_t *hp = (handle_params_t *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->body) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 解析请求体
  cJSON *root = cJSON_ParseWithOpts(hp->body, NULL, 0);
  if (!root) {
    build_error_response(HTTP_STATUS_BAD_REQUEST,
                         "Invalid JSON in request body", response);
    return -1;
  }

  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    build_error_response(HTTP_STATUS_BAD_REQUEST,
                         "Request body must be a JSON object", response);
    return -1;
  }

  int ret = handle_batch_core(hp, root, 0, response);
  return ret;
}
