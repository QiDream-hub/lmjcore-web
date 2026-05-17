// src/handlers/batch_handle.c - 批量操作处理器 (事务复用架构)
#include "cJSON.h"
#include "error_response.h"
#include "handle_utils.h"
#include "lmjcore.h"
#include "lmjcore_handle.h"
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

// ==================== 事务超时检查宏 ====================

#define CHECK_TXN_TIMEOUT(hp, response)                                      \
  do {                                                                       \
    if (lmjcore_txn_check_timeout((hp)->txn_start_time, (hp)->txn_timeout)) {\
      RETURN_ERROR_TXN_TIMEOUT(response);                                    \
    }                                                                        \
  } while (0)

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

  // 解析路径，确定处理器
  // 格式：/obj/{ptr}、/obj/{ptr}/{member}、/set/{ptr}、/set/{ptr}/elements
  const char *entity_type = NULL;
  const char *ptr_str = NULL;
  const char *sub_path = NULL;

  if (strncmp(path, "/obj/", 5) == 0) {
    entity_type = "obj";
    ptr_str = path + 5;
  } else if (strncmp(path, "/set/", 5) == 0) {
    entity_type = "set";
    ptr_str = path + 5;
  } else {
    result->status_code = HTTP_STATUS_BAD_REQUEST;
    result->body = strdup("{\"error\":\"Invalid path prefix\"}");
    result->body_len = strlen(result->body);
    return -1;
  }

  // 查找第二个斜杠
  const char *slash = strchr(ptr_str, '/');
  if (slash) {
    sub_path = slash + 1;
  }

  // 构建 route_params（模拟路由解析结果）
  route_param_t params_arr[2] = {{0}};
  route_params_t route_params = {0};

  char ptr_buf[64] = {0};
  char member_buf[512] = {0};

  // 提取 ptr
  size_t ptr_len = slash ? (size_t)(slash - ptr_str) : strlen(ptr_str);
  if (ptr_len != LMJCORE_PTR_STRING_LEN) {
    result->status_code = HTTP_STATUS_BAD_REQUEST;
    result->body = strdup("{\"error\":\"Invalid pointer length\"}");
    result->body_len = strlen(result->body);
    return -1;
  }
  memcpy(ptr_buf, ptr_str, ptr_len);
  params_arr[0].ptr = ptr_buf;
  params_arr[0].len = ptr_len;

  // 提取 member（如果有）
  if (sub_path) {
    size_t member_len = strlen(sub_path);
    if (member_len >= sizeof(member_buf)) {
      result->status_code = HTTP_STATUS_BAD_REQUEST;
      result->body = strdup("{\"error\":\"Member name too long\"}");
      result->body_len = strlen(result->body);
      return -1;
    }
    // URL 解码
    if (url_decode(sub_path, member_len, member_buf, sizeof(member_buf)) < 0) {
      result->status_code = HTTP_STATUS_BAD_REQUEST;
      result->body = strdup("{\"error\":\"Invalid member name\"}");
      result->body_len = strlen(result->body);
      return -1;
    }
    params_arr[1].ptr = member_buf;
    params_arr[1].len = strlen(member_buf);
    route_params.count = 2;
  } else {
    route_params.count = 1;
  }

  route_params.params = params_arr;

  // 构建 body（如果有）
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
    .params = &route_params,
    .env = hp->env,
    .txn = hp->txn,           // 共享事务
    .body = body_str,
    .body_len = body_str ? strlen(body_str) : 0,
    .txn_timeout = hp->txn_timeout,
    .txn_start_time = hp->txn_start_time,
    .auto_manage_txn = false  // 批量操作中不自动管理事务
  };

  // 响应
  http_response_t response = {0};

  // 确定并调用处理器
  handler_fn handler = NULL;

  if (strcmp(entity_type, "obj") == 0) {
    // 对象操作
    if (strcmp(method_str, "GET") == 0) {
      if (sub_path) {
        handler = handle_obj_member_get;
      } else {
        handler = handle_obj_get;
      }
    } else if (strcmp(method_str, "PUT") == 0) {
      if (sub_path) {
        handler = handle_obj_member_put;
      }
    } else if (strcmp(method_str, "POST") == 0) {
      // 对象不支持 POST
      result->status_code = HTTP_STATUS_BAD_REQUEST;
      result->body = strdup("{\"error\":\"POST not supported for objects\"}");
      result->body_len = strlen(result->body);
      if (body_str) free(body_str);
      return -1;
    } else if (strcmp(method_str, "DELETE") == 0) {
      if (sub_path) {
        handler = handle_obj_member_del;
      } else {
        handler = handle_obj_del;
      }
    }
  } else {
    // 集合操作
    if (strcmp(method_str, "GET") == 0) {
      if (strcmp(sub_path ? sub_path : "", "elements") == 0) {
        // GET /set/{ptr}/elements 不支持
        result->status_code = HTTP_STATUS_BAD_REQUEST;
        result->body = strdup("{\"error\":\"GET /set/{ptr}/elements not supported\"}");
        result->body_len = strlen(result->body);
        if (body_str) free(body_str);
        return -1;
      } else {
        handler = handle_set_get;
      }
    } else if (strcmp(method_str, "POST") == 0) {
      if (sub_path && strcmp(sub_path, "elements") == 0) {
        handler = handle_set_add;
      }
    } else if (strcmp(method_str, "DELETE") == 0) {
      if (sub_path && strcmp(sub_path, "elements") == 0) {
        handler = handle_set_remove;
      } else {
        handler = handle_set_del;
      }
    }
  }

  if (!handler) {
    result->status_code = HTTP_STATUS_BAD_REQUEST;
    result->body = strdup("{\"error\":\"Invalid method for this path\"}");
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

// ==================== 主处理器 ====================

int handle_batch_operations(void *params, void *cbdata) {
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

  // 获取 operations 数组
  cJSON *operations = cJSON_GetObjectItemCaseSensitive(root, "operations");
  if (!operations || !cJSON_IsArray(operations)) {
    cJSON_Delete(root);
    build_error_response(HTTP_STATUS_BAD_REQUEST,
                         "Missing or invalid 'operations' array", response);
    return -1;
  }

  int op_count = cJSON_GetArraySize(operations);
  if (op_count <= 0) {
    cJSON_Delete(root);
    build_error_response(HTTP_STATUS_BAD_REQUEST,
                         "Operations array is empty", response);
    return -1;
  }

  if (op_count > MAX_OPERATIONS) {
    cJSON_Delete(root);
    build_error_response(HTTP_STATUS_BAD_REQUEST,
                         "Too many operations (max 1000)", response);
    return -1;
  }

  // 检查 readonly 标志
  int readonly = 0;
  cJSON *readonly_item = cJSON_GetObjectItemCaseSensitive(root, "readonly");
  if (readonly_item && cJSON_IsBool(readonly_item)) {
    readonly = cJSON_IsTrue(readonly_item);
  }

  // 只读事务检查
  if (readonly) {
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, operations) {
      if (!cJSON_IsObject(item)) continue;
      cJSON *method_item = cJSON_GetObjectItemCaseSensitive(item, "method");
      if (method_item && cJSON_IsString(method_item)) {
        const char *method = method_item->valuestring;
        if (strcmp(method, "PUT") == 0 || strcmp(method, "POST") == 0 ||
            strcmp(method, "DELETE") == 0) {
          cJSON_Delete(root);
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
    CHECK_TXN_TIMEOUT(hp, response);

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

    char *error_json = cJSON_PrintUnformatted(error);
    build_error_response(HTTP_STATUS_BAD_REQUEST, error_json, response);
    free(error_json);
    cJSON_Delete(error);

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
