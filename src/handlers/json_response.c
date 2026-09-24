// src/handlers/json_response.c - 统一 JSON 响应构建层实现（基于 cJSON）
#include "json_response.h"

#include "lmjcore.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==================== 内部工具 ====================

/** 固定的内存分配失败响应体 */
#define JSON_RESPONSE_OOM_BODY "{\"error\":\"Memory allocation failed\"}"

/** 写入内存分配失败响应（response 必须非 NULL，且旧 body 已释放） */
static int write_oom_body(http_response_t *response) {
  response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
  response->body = strdup(JSON_RESPONSE_OOM_BODY);
  response->body_len = response->body ? strlen(response->body) : 0;
  return -1;
}

/** 释放旧响应体并写入内存分配失败响应 */
static int oom_response(http_response_t *response) {
  if (!response) {
    return -1;
  }
  if (response->body) {
    free(response->body);
    response->body = NULL;
    response->body_len = 0;
  }
  return write_oom_body(response);
}

/**
 * @brief 用已分配的字符串设置响应体（接管所有权）
 * @param body malloc 分配的字符串（可为 NULL，此时写入内存错误响应）
 */
static int set_owned_body(http_response_t *response, int status_code,
                          char *body) {
  if (!response) {
    free(body);
    return -1;
  }

  if (response->body) {
    free(response->body);
    response->body = NULL;
    response->body_len = 0;
  }

  if (!body) {
    return write_oom_body(response);
  }

  response->status_code = status_code;
  response->body = body;
  response->body_len = strlen(body);
  return 0;
}

/** 追加字符串字段（NULL 视为空串），失败返回 false */
static bool add_string(cJSON *obj, const char *key, const char *value) {
  return cJSON_AddStringToObject(obj, key, value ? value : "") != NULL;
}

/** 追加数字字段，失败返回 false */
static bool add_number(cJSON *obj, const char *key, double value) {
  return cJSON_AddNumberToObject(obj, key, value) != NULL;
}

// ==================== 响应输出 ====================

int json_response_set(http_response_t *response, int status_code, cJSON *root) {
  if (!root) {
    return oom_response(response);
  }

  // cJSON_PrintUnformatted 返回 malloc 字符串，可直接作为响应体
  char *body = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);

  return set_owned_body(response, status_code, body);
}

int json_response_error(http_response_t *response, int status_code,
                        const char *fmt, ...) {
  if (!response) {
    return -1;
  }
  if (!fmt) {
    fmt = "";
  }

  char stack_msg[512];
  char *heap_msg = NULL;
  const char *msg = stack_msg;

  va_list args;
  va_start(args, fmt);
  int needed = vsnprintf(stack_msg, sizeof(stack_msg), fmt, args);
  va_end(args);

  if (needed < 0) {
    stack_msg[0] = '\0';
  } else if ((size_t)needed >= sizeof(stack_msg)) {
    // 超长错误信息（如嵌套路径）动态分配，避免静默截断
    va_start(args, fmt);
    heap_msg = (char *)malloc((size_t)needed + 1);
    if (heap_msg) {
      vsnprintf(heap_msg, (size_t)needed + 1, fmt, args);
      msg = heap_msg;
    }
    va_end(args);
  }

  cJSON *root = cJSON_CreateObject();
  if (!root || !add_string(root, "error", msg)) {
    cJSON_Delete(root);
    free(heap_msg);
    return oom_response(response);
  }

  free(heap_msg);
  return json_response_set(response, status_code, root);
}

int json_response_lmjcore_error(http_response_t *response, int error_code) {
  return json_response_error(response, lmjcore_error_to_http_status(error_code),
                             "%s", lmjcore_strerror(error_code));
}

int json_response_success(http_response_t *response, int status_code) {
  cJSON *root = cJSON_CreateObject();
  if (!root || !cJSON_AddBoolToObject(root, "success", true)) {
    cJSON_Delete(root);
    return oom_response(response);
  }
  return json_response_set(response, status_code, root);
}

// ==================== 响应模型构建 ====================

cJSON *json_new_ptr(const char *ptr_str) {
  if (!ptr_str) {
    return NULL;
  }

  cJSON *root = cJSON_CreateObject();
  if (!root || !add_string(root, "ptr", ptr_str)) {
    cJSON_Delete(root);
    return NULL;
  }
  return root;
}

cJSON *json_new_counted_ptr(const char *ptr_str, const char *count_key,
                            size_t count) {
  if (!ptr_str || !count_key) {
    return NULL;
  }

  cJSON *root = cJSON_CreateObject();
  if (!root || !add_string(root, "ptr", ptr_str) ||
      !add_number(root, count_key, (double)count)) {
    cJSON_Delete(root);
    return NULL;
  }
  return root;
}

cJSON *json_new_entity(const char *ptr_str, const char *array_key,
                       cJSON **out_array) {
  if (!ptr_str || !array_key || !out_array) {
    return NULL;
  }
  *out_array = NULL;

  cJSON *root = cJSON_CreateObject();
  if (!root || !add_string(root, "ptr", ptr_str)) {
    cJSON_Delete(root);
    return NULL;
  }

  cJSON *array = cJSON_AddArrayToObject(root, array_key);
  if (!array) {
    cJSON_Delete(root);
    return NULL;
  }

  *out_array = array;
  return root;
}

int json_entity_set_count(cJSON *root, size_t count) {
  if (!root || !add_number(root, "count", (double)count)) {
    return -1;
  }
  return 0;
}

bool json_array_append(cJSON *array, cJSON *item) {
  if (!array || !item) {
    cJSON_Delete(item);
    return false;
  }
  if (!cJSON_AddItemToArray(array, item)) {
    cJSON_Delete(item);
    return false;
  }
  return true;
}

cJSON *json_new_value_entry(const char *key, const char *key_value,
                            const char *value, const char *type) {
  if (!key) {
    return NULL;
  }

  cJSON *item = cJSON_CreateObject();
  if (!item) {
    return NULL;
  }

  // key 为 "value" 时退化为 {"value","type"}，避免写入重复键
  bool ok = (strcmp(key, "value") == 0)
                ? (add_string(item, "value", value) &&
                   add_string(item, "type", type))
                : (add_string(item, key, key_value) &&
                   add_string(item, "value", value) &&
                   add_string(item, "type", type));

  if (!ok) {
    cJSON_Delete(item);
    return NULL;
  }
  return item;
}

cJSON *json_new_value(const char *value, const char *type) {
  return json_new_value_entry("value", value, value, type);
}

cJSON *json_new_exist(bool exists, const char *type) {
  cJSON *root = cJSON_CreateObject();
  if (!root || !cJSON_AddBoolToObject(root, "exist", exists)) {
    cJSON_Delete(root);
    return NULL;
  }

  if (exists && type && !add_string(root, "type", type)) {
    cJSON_Delete(root);
    return NULL;
  }
  return root;
}

int json_response_value_too_large(http_response_t *response, size_t limit) {
  cJSON *root = cJSON_CreateObject();
  if (!root || !add_string(root, "error", "Value too large") ||
      !add_number(root, "limit", (double)limit)) {
    cJSON_Delete(root);
    return oom_response(response);
  }
  return json_response_set(response, HTTP_STATUS_PAYLOAD_TOO_LARGE, root);
}

cJSON *json_new_health(const char *status, long uptime) {
  cJSON *root = cJSON_CreateObject();
  if (!root || !add_string(root, "status", status) ||
      !add_number(root, "uptime", (double)uptime)) {
    cJSON_Delete(root);
    return NULL;
  }
  return root;
}
