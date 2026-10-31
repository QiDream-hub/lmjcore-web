// src/handlers/batch_handle.c - 批量操作处理器 (事务复用架构)
#include "cJSON.h"
#include "error_response.h"
#include "handle_utils.h"
#include "json_response.h"
#include "lmjcore.h"
#include "lmjcore_handle.h"
#include "router.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==================== 常量定义 ====================

#define MAX_OPERATIONS 1000

// ==================== 内部工具 ====================

/**
 * @brief 记录单个操作的错误结果（{"error":"..."}）
 *
 * 结果直接复用 http_response_t，避免子处理器与批处理之间来回序列化/解析。
 */
static void set_result_error(http_response_t *result, int status_code,
                             const char *message) {
  json_response_error(result, status_code, "%s", message);
}

/**
 * @brief 释放结果区间内所有操作的响应节点
 */
static void free_results(http_response_t *results, int count) {
  if (!results) {
    return;
  }
  for (int i = 0; i < count; i++) {
    cJSON_Delete(results[i].json);
    results[i].json = NULL;
  }
}

// ==================== vars 变量替换 ====================

/** 单条变量绑定：token 在本次操作的 path/body 中被替换为 value */
typedef struct {
  const char *token; // 指向请求 JSON 中的键名（生命周期同 root）
  size_t token_len;
  char *value; // 已解析的替换值（由 resolve_vars 分配）
} var_binding_t;

/**
 * @brief 解析引用表达式 "{N:key}"（N 为 0-based 操作下标）
 * @return 0 成功，-1 格式非法
 */
static int parse_var_ref(const char *expr, long *out_index,
                         const char **out_key, size_t *out_key_len) {
  size_t len = expr ? strlen(expr) : 0;
  if (len < 5 || expr[0] != '{' || expr[len - 1] != '}') {
    return -1;
  }

  const char *colon = strchr(expr + 1, ':');
  if (!colon || colon == expr + 1) {
    return -1;
  }

  long index = 0;
  for (const char *p = expr + 1; p < colon; p++) {
    if (*p < '0' || *p > '9') {
      return -1;
    }
    index = index * 10 + (*p - '0');
    if (index > MAX_OPERATIONS) {
      return -1;
    }
  }

  const char *key = colon + 1;
  size_t key_len = (size_t)((expr + len - 1) - key);
  if (key_len == 0) {
    return -1;
  }

  *out_index = index;
  *out_key = key;
  *out_key_len = key_len;
  return 0;
}

/**
 * @brief 在指定位置匹配最长 token，返回匹配长度（0 表示不匹配）
 */
static size_t match_binding(const char *src, size_t pos,
                            const var_binding_t *bindings, size_t count,
                            size_t *out_binding) {
  size_t best_len = 0;
  size_t best_index = 0;
  for (size_t i = 0; i < count; i++) {
    size_t len = bindings[i].token_len;
    if (len > best_len && strncmp(src + pos, bindings[i].token, len) == 0) {
      best_len = len;
      best_index = i;
    }
  }
  if (best_len > 0) {
    *out_binding = best_index;
  }
  return best_len;
}

/**
 * @brief 单遍替换（非级联）：按“最早位置 + 最长匹配”展开 token
 * @return 新字符串（调用方释放），失败返回 NULL
 */
static char *substitute_string(const char *src, const var_binding_t *bindings,
                               size_t count) {
  size_t src_len = strlen(src);
  size_t out_len = 0;

  for (size_t pos = 0; pos < src_len;) {
    size_t binding = 0;
    size_t matched = match_binding(src, pos, bindings, count, &binding);
    if (matched > 0) {
      out_len += strlen(bindings[binding].value);
      pos += matched;
    } else {
      out_len++;
      pos++;
    }
  }

  char *out = (char *)malloc(out_len + 1);
  if (!out) {
    return NULL;
  }

  size_t written = 0;
  for (size_t pos = 0; pos < src_len;) {
    size_t binding = 0;
    size_t matched = match_binding(src, pos, bindings, count, &binding);
    if (matched > 0) {
      size_t value_len = strlen(bindings[binding].value);
      memcpy(out + written, bindings[binding].value, value_len);
      written += value_len;
      pos += matched;
    } else {
      out[written++] = src[pos++];
    }
  }
  out[written] = '\0';
  return out;
}

/**
 * @brief 递归替换 body 中所有字符串值（不替换对象键）
 * @return 0 成功，-1 内存分配失败
 */
static int substitute_walk(cJSON *value, const var_binding_t *bindings,
                           size_t count) {
  if (cJSON_IsString(value)) {
    char *replaced = substitute_string(value->valuestring, bindings, count);
    if (!replaced) {
      return -1;
    }
    free(value->valuestring);
    value->valuestring = replaced;
    return 0;
  }

  if (cJSON_IsObject(value) || cJSON_IsArray(value)) {
    for (cJSON *child = value->child; child; child = child->next) {
      if (substitute_walk(child, bindings, count) != 0) {
        return -1;
      }
    }
  }
  return 0;
}

/**
 * @brief 解析并应用单个操作的 vars
 *
 * vars 形如 {"token": "{N:key}"}：N 为已执行操作的 0-based 下标，key 为该
 * 操作响应体（body）的顶层字段。token 在本操作 path 与 body 字符串值中被
 * 替换为该字段的值。任一环节失败都视为该操作失败。
 *
 * @return 0 成功，-1 失败（result 中已写入错误响应）
 */
static int resolve_vars(cJSON *op, http_response_t *results, int index,
                        http_response_t *result) {
  cJSON *vars = cJSON_GetObjectItemCaseSensitive(op, "vars");
  if (!vars) {
    return 0;
  }

  if (!cJSON_IsObject(vars)) {
    set_result_error(result, HTTP_STATUS_BAD_REQUEST,
                     "Invalid 'vars' (must be a JSON object)");
    return -1;
  }

  int binding_count = cJSON_GetArraySize(vars);
  if (binding_count <= 0) {
    return 0;
  }

  var_binding_t *bindings =
      (var_binding_t *)calloc((size_t)binding_count, sizeof(*bindings));
  if (!bindings) {
    set_result_error(result, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                     "Failed to allocate memory");
    return -1;
  }

  int rc = 0;
  int filled = 0;
  char msg[1024];
  cJSON *binding = NULL;

  cJSON_ArrayForEach(binding, vars) {
    const char *token = binding->string;
    long ref_index = -1;
    const char *key = NULL;
    size_t key_len = 0;

    if (!token || !*token || !cJSON_IsString(binding) ||
        parse_var_ref(binding->valuestring, &ref_index, &key, &key_len) != 0) {
      snprintf(msg, sizeof(msg),
               "vars: invalid reference for token '%s' (expect \"{N:key}\")",
               token ? token : "");
      set_result_error(result, HTTP_STATUS_BAD_REQUEST, msg);
      rc = -1;
      goto cleanup;
    }

    if (ref_index >= (long)index) {
      snprintf(msg, sizeof(msg),
               "vars: operation %ld referenced by token '%s' has not been "
               "executed",
               ref_index, token);
      set_result_error(result, HTTP_STATUS_BAD_REQUEST, msg);
      rc = -1;
      goto cleanup;
    }

    cJSON *ref_body = results[ref_index].json;
    if (!ref_body || !cJSON_IsObject(ref_body)) {
      snprintf(msg, sizeof(msg),
               "vars: result of operation %ld has no body object", ref_index);
      set_result_error(result, HTTP_STATUS_BAD_REQUEST, msg);
      rc = -1;
      goto cleanup;
    }

    char key_buf[512];
    if (key_len >= sizeof(key_buf)) {
      snprintf(msg, sizeof(msg),
               "vars: reference key for token '%s' is too long", token);
      set_result_error(result, HTTP_STATUS_BAD_REQUEST, msg);
      rc = -1;
      goto cleanup;
    }
    memcpy(key_buf, key, key_len);
    key_buf[key_len] = '\0';

    cJSON *ref_value = cJSON_GetObjectItemCaseSensitive(ref_body, key_buf);
    if (!ref_value) {
      snprintf(msg, sizeof(msg),
               "vars: property '%.*s' not found in result of operation %ld",
               (int)key_len, key, ref_index);
      set_result_error(result, HTTP_STATUS_BAD_REQUEST, msg);
      rc = -1;
      goto cleanup;
    }

    char *value = NULL;
    if (cJSON_IsString(ref_value)) {
      value = strdup(ref_value->valuestring);
    } else if (cJSON_IsNumber(ref_value)) {
      value = cJSON_PrintUnformatted(ref_value);
    }

    if (!value) {
      if (!cJSON_IsString(ref_value) && !cJSON_IsNumber(ref_value)) {
        snprintf(msg, sizeof(msg),
                 "vars: value of '{%ld:%.*s}' is not a string or number",
                 ref_index, (int)key_len, key);
        set_result_error(result, HTTP_STATUS_BAD_REQUEST, msg);
      } else {
        set_result_error(result, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                         "Failed to allocate memory");
      }
      rc = -1;
      goto cleanup;
    }

    bindings[filled].token = token;
    bindings[filled].token_len = strlen(token);
    bindings[filled].value = value;
    filled++;
  }

  // 替换 path
  cJSON *path_item = cJSON_GetObjectItemCaseSensitive(op, "path");
  if (path_item && cJSON_IsString(path_item)) {
    char *replaced =
        substitute_string(path_item->valuestring, bindings, (size_t)filled);
    if (!replaced) {
      set_result_error(result, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                       "Failed to allocate memory");
      rc = -1;
      goto cleanup;
    }
    free(path_item->valuestring);
    path_item->valuestring = replaced;
  }

  // 替换 body（递归处理字符串值）
  cJSON *body_item = cJSON_GetObjectItemCaseSensitive(op, "body");
  if (body_item && substitute_walk(body_item, bindings, (size_t)filled) != 0) {
    set_result_error(result, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                     "Failed to allocate memory");
    rc = -1;
    goto cleanup;
  }

cleanup:
  for (int i = 0; i < filled; i++) {
    free(bindings[i].value);
  }
  free(bindings);
  return rc;
}

// ==================== 解析操作并调用处理器 ====================

/**
 * @brief 解析单个操作，构建参数，调用对应处理器
 */
static int execute_operation(handle_params_t *hp, const cJSON *op_obj,
                             http_response_t *result) {
  // 解析 method 和 path
  cJSON *method_item = cJSON_GetObjectItemCaseSensitive(op_obj, "method");
  cJSON *path_item = cJSON_GetObjectItemCaseSensitive(op_obj, "path");

  if (!method_item || !cJSON_IsString(method_item) || !path_item ||
      !cJSON_IsString(path_item)) {
    set_result_error(result, HTTP_STATUS_BAD_REQUEST, "Missing method or path");
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
    set_result_error(result, HTTP_STATUS_BAD_REQUEST, "Invalid HTTP method");
    return -1;
  }

  // 使用路由器匹配路径（复用 routes.c 中注册的路由）
  if (!hp->router) {
    set_result_error(result, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                     "Router not configured");
    return -1;
  }

  route_node_t *node = router_match(hp->router, method, path);
  if (!node) {
    set_result_error(result, HTTP_STATUS_NOT_FOUND, "Route not found");
    return -1;
  }

  // 提取路由参数
  route_param_t param_storage[2];
  route_params_t params = {0};
  size_t param_count = 0;

  if (router_extract(node, path, param_storage, 2, &param_count) != 0) {
    set_result_error(result, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                     "Failed to extract route params");
    return -1;
  }

  params.params = param_storage;
  params.count = param_count;

  // 原样转发 body（PUT/DELETE 需含 value+type，init 可传任意 JSON）
  char *body_str = NULL;
  cJSON *body_item = cJSON_GetObjectItemCaseSensitive(op_obj, "body");
  if (body_item && !cJSON_IsNull(body_item)) {
    body_str = cJSON_PrintUnformatted(body_item);
  }

  // 构建 handle_params
  handle_params_t local_params = {
      .params = &params,
      .env = hp->env,
      .txn = hp->txn,       // 共享事务
      .router = hp->router, // 传递路由器（嵌套批量操作时使用）
      .body = body_str,
      .body_len = body_str ? strlen(body_str) : 0,
      .txn_timeout = hp->txn_timeout,
      .txn_start_time = hp->txn_start_time,
      .auto_manage_txn = false, // 批量操作中不自动管理事务
      .query_max_depth = hp->query_max_depth,
      .max_value_bytes = hp->max_value_bytes};

  // 响应
  http_response_t response = {0};

  // 获取并调用处理器
  route_callback_t handler = router_get_callback(node);
  if (!handler) {
    set_result_error(result, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                     "Handler not found");
    free(body_str);
    return -1;
  }

  // 调用处理器（失败与否以响应状态码为准）
  handler(&local_params, &response);

  // 直接接管子处理器的 cJSON 响应节点，避免字符串与 JSON 之间的反复转换
  result->status_code = response.status_code;
  result->json = response.json;
  response.json = NULL;

  // 清理
  free(body_str);

  // 子处理器返回 4xx/5xx 即视为该操作失败：RETURN_ERROR_* 宏返回 0，
  // 因此不能只依赖返回值判断，否则批量会误报 success
  if (result->status_code >= 400) {
    return -1;
  }
  return 0;
}

// ==================== 内部核心处理器 ====================

/**
 * @brief 批量操作核心处理器（内部使用）
 * @param hp 处理参数
 * @param root 请求体 JSON 对象（本函数接管所有权并负责释放）
 * @param readonly 是否只读事务
 * @param response 响应结构
 * @return 0=成功，-1=失败
 */
static int handle_batch_core(handle_params_t *hp, cJSON *root, int readonly,
                             http_response_t *response) {
  // 获取 operations 数组
  cJSON *operations = cJSON_GetObjectItemCaseSensitive(root, "operations");
  if (!operations || !cJSON_IsArray(operations)) {
    cJSON_Delete(root);
    return json_response_error(response, HTTP_STATUS_BAD_REQUEST,
                               "Missing or invalid 'operations' array");
  }

  int op_count = cJSON_GetArraySize(operations);
  if (op_count <= 0) {
    cJSON_Delete(root);
    return json_response_error(response, HTTP_STATUS_BAD_REQUEST,
                               "Operations array is empty");
  }

  if (op_count > MAX_OPERATIONS) {
    cJSON_Delete(root);
    return json_response_error(response, HTTP_STATUS_BAD_REQUEST,
                               "Too many operations (max 1000)");
  }

  // 写事务检查：检查操作中是否包含写操作（只读路由使用）
  if (readonly) {
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, operations) {
      if (!cJSON_IsObject(item)) {
        continue;
      }
      cJSON *method_item = cJSON_GetObjectItemCaseSensitive(item, "method");
      if (method_item && cJSON_IsString(method_item)) {
        const char *method = method_item->valuestring;
        if (strcmp(method, "PUT") == 0 || strcmp(method, "POST") == 0 ||
            strcmp(method, "DELETE") == 0) {
          cJSON_Delete(root);
          return json_response_error(
              response, HTTP_STATUS_BAD_REQUEST,
              "Readonly transaction cannot contain write operations");
        }
      }
    }
  }

  // 开启事务（hp->txn 为空时由本处理器创建并负责提交/回滚）
  lmjcore_txn *txn = NULL;
  int txn_owned = 0;
  int txn_flags = readonly ? LMJCORE_TXN_READONLY : 0;
  int rc = -1;
  http_response_t *results = NULL;
  cJSON *error = NULL;
  cJSON *result_obj = NULL;

  TXN_BEGIN(txn, hp, txn_flags, txn_owned, response, cleanup);

  // 更新 hp->txn 供子处理器使用
  hp->txn = txn;
  hp->auto_manage_txn = false;

  // 分配结果数组
  results = (http_response_t *)calloc((size_t)op_count, sizeof(http_response_t));
  if (!results) {
    rc = json_response_error(response, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                             "Failed to allocate memory");
    goto cleanup;
  }

  // 执行所有操作
  int failed_index = -1;
  cJSON *item = NULL;
  int index = 0;

  cJSON_ArrayForEach(item, operations) {
    if (index >= op_count) {
      break;
    }

    // 检查事务超时
    if (lmjcore_txn_check_timeout(hp->txn_start_time, hp->txn_timeout)) {
      rc = json_response_error(response, HTTP_STATUS_REQUEST_TIMEOUT,
                               "Transaction timeout");
      goto cleanup;
    }

    if (!cJSON_IsObject(item)) {
      set_result_error(&results[index], HTTP_STATUS_BAD_REQUEST,
                       "Invalid operation format");
      failed_index = index;
      break;
    }

    // 应用本操作的 vars：引用已执行操作的结果，替换 path/body 中的 token
    if (resolve_vars(item, results, index, &results[index]) != 0) {
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
    // 构建错误响应 {"success":false,"failed_at":N,"details":{...}}
    error = cJSON_CreateObject();
    if (!error || !cJSON_AddBoolToObject(error, "success", false) ||
        !cJSON_AddNumberToObject(error, "failed_at", failed_index)) {
      rc = json_response_error(response, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                               "Failed to allocate memory");
      goto cleanup;
    }

    if (results[failed_index].json) {
      cJSON *details = results[failed_index].json;
      results[failed_index].json = NULL; // 所有权转移
      if (!cJSON_AddItemToObject(error, "details", details)) {
        cJSON_Delete(details);
      }
    }

    // cleanup 中回滚事务
    rc = json_response_set(response, HTTP_STATUS_BAD_REQUEST, error);
    error = NULL; // 所有权已转移给响应
    goto cleanup;
  }

  // 只读事务直接结束；写事务提交
  if (readonly) {
    TXN_ABORT(txn, txn_owned);
  } else {
    TXN_COMMIT(txn, txn_owned, response, cleanup);
  }

  // 构建成功响应 {"success":true,"results":[{"status":N,"body":{...}}...]}
  result_obj = cJSON_CreateObject();
  if (!result_obj || !cJSON_AddBoolToObject(result_obj, "success", true)) {
    rc = json_response_error(response, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                             "Failed to allocate memory");
    goto cleanup;
  }

  cJSON *results_array = cJSON_AddArrayToObject(result_obj, "results");
  if (!results_array) {
    rc = json_response_error(response, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                             "Failed to allocate memory");
    goto cleanup;
  }

  for (int i = 0; i < op_count; i++) {
    cJSON *result_item = cJSON_CreateObject();
    if (!result_item || !cJSON_AddNumberToObject(result_item, "status",
                                                 results[i].status_code)) {
      cJSON_Delete(result_item);
      rc = json_response_error(response, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                               "Failed to allocate memory");
      goto cleanup;
    }

    cJSON *body_json = results[i].json;
    results[i].json = NULL; // 所有权转移给 result_item
    if (body_json) {
      if (!cJSON_AddItemToObject(result_item, "body", body_json)) {
        cJSON_Delete(body_json);
        cJSON_Delete(result_item);
        rc = json_response_error(response, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "Failed to allocate memory");
        goto cleanup;
      }
    } else if (!cJSON_AddNullToObject(result_item, "body")) {
      cJSON_Delete(result_item);
      rc = json_response_error(response, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                               "Failed to allocate memory");
      goto cleanup;
    }

    if (!cJSON_AddItemToArray(results_array, result_item)) {
      cJSON_Delete(result_item);
      rc = json_response_error(response, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                               "Failed to allocate memory");
      goto cleanup;
    }
  }

  rc = json_response_set(response, HTTP_STATUS_OK, result_obj);
  result_obj = NULL; // 所有权已转移给响应

cleanup:
  TXN_ABORT(txn, txn_owned);
  free_results(results, op_count);
  free(results);
  cJSON_Delete(error);
  cJSON_Delete(result_obj);
  cJSON_Delete(root);
  return rc;
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
    return json_response_error(response, HTTP_STATUS_BAD_REQUEST,
                               "Invalid JSON in request body");
  }

  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return json_response_error(response, HTTP_STATUS_BAD_REQUEST,
                               "Request body must be a JSON object");
  }

  return handle_batch_core(hp, root, 1, response);
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
    return json_response_error(response, HTTP_STATUS_BAD_REQUEST,
                               "Invalid JSON in request body");
  }

  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return json_response_error(response, HTTP_STATUS_BAD_REQUEST,
                               "Request body must be a JSON object");
  }

  return handle_batch_core(hp, root, 0, response);
}
