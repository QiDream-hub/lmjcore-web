// src/handlers/batch_handle.c - 批量操作处理器
#include "error_response.h"
#include "handle_utils.h"
#include "lmjcore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==================== 操作类型定义 ====================

typedef enum {
  BATCH_OP_GET = 0,
  BATCH_OP_PUT = 1,
  BATCH_OP_POST = 2,
  BATCH_OP_DELETE = 3
} batch_op_method_t;

typedef struct {
  batch_op_method_t method;
  const char *path;           // 完整路径（如 /obj/01abc.../member）
  const char *body_value;     // PUT/POST 请求的值
  size_t body_value_len;
  int result_status;    // 执行结果状态码
  char *result_body;    // 执行结果 body
  size_t result_body_len;
} batch_operation_t;

// ==================== 事务超时检查宏 ====================

#define CHECK_TXN_TIMEOUT(hp, response, txn)                                 \
  do {                                                                       \
    if (lmjcore_txn_check_timeout((hp)->txn_start_time, (hp)->txn_timeout)) {\
      if (txn) lmjcore_txn_abort(txn);                                       \
      RETURN_ERROR_TXN_TIMEOUT(response);                                    \
    }                                                                        \
  } while (0)

// ==================== JSON 解析辅助 ====================

/**
 * @brief 从 JSON 中提取布尔值
 */
static int json_get_bool(const char *json, size_t json_len, const char *key,
                         bool *out_value) {
  (void)json_len;
  if (!json || !key || !out_value) {
    return -1;
  }

  char search_pattern[256];
  snprintf(search_pattern, sizeof(search_pattern), "\"%s\"", key);

  const char *key_pos = strstr(json, search_pattern);
  if (!key_pos) {
    return -1;
  }

  const char *p = key_pos + strlen(search_pattern);
  while (*p && (*p == ':' || *p == ' ' || *p == '\t')) {
    p++;
  }

  if (strncmp(p, "true", 4) == 0) {
    *out_value = true;
    return 0;
  } else if (strncmp(p, "false", 5) == 0) {
    *out_value = false;
    return 0;
  }

  return -1;
}

/**
 * @brief 从 JSON 中提取字符串值（不分配内存，使用静态缓冲区）
 * @return 成功返回 0，失败返回 -1
 */
static int json_get_string_static(const char *json, size_t json_len,
                                   const char *key, const char **out_value,
                                   size_t *out_len) {
  (void)json_len;
  static __thread char value_buf[8192];

  if (!json || !key || !out_value || !out_len) {
    return -1;
  }

  char search_pattern[256];
  snprintf(search_pattern, sizeof(search_pattern), "\"%s\"", key);

  const char *key_pos = strstr(json, search_pattern);
  if (!key_pos) {
    return -1;
  }

  const char *p = key_pos + strlen(search_pattern);
  while (*p && (*p == ':' || *p == ' ' || *p == '\t')) {
    p++;
  }

  if (*p != '"') {
    return -1;
  }
  p++;

  const char *start = p;
  while (*p && *p != '"') {
    if (*p == '\\' && *(p + 1)) {
      p += 2;
    } else {
      p++;
    }
  }

  size_t len = p - start;
  if (len >= sizeof(value_buf) - 1) {
    return -1;
  }

  memcpy(value_buf, start, len);
  value_buf[len] = '\0';

  *out_value = value_buf;
  *out_len = len;

  return 0;
}

/**
 * @brief 解析单个操作
 */
static int parse_operation(const char *json, size_t json_len,
                           batch_operation_t *op) {
  (void)json_len;
  const char *method_str = NULL;
  size_t method_len = 0;

  // 解析 method
  if (json_get_string_static(json, json_len, "method", &method_str,
                             &method_len) != 0) {
    return -1;
  }

  if (strncmp(method_str, "GET", method_len) == 0) {
    op->method = BATCH_OP_GET;
  } else if (strncmp(method_str, "PUT", method_len) == 0) {
    op->method = BATCH_OP_PUT;
  } else if (strncmp(method_str, "POST", method_len) == 0) {
    op->method = BATCH_OP_POST;
  } else if (strncmp(method_str, "DELETE", method_len) == 0) {
    op->method = BATCH_OP_DELETE;
  } else {
    return -1;
  }

  // 解析 path
  if (json_get_string_static(json, json_len, "path", &op->path,
                             &method_len) != 0) {
    return -1;
  }

  // 解析 body（可选，仅 PUT/POST 需要）
  if (op->method == BATCH_OP_PUT || op->method == BATCH_OP_POST) {
    // 查找 body 对象
    const char *body_key = "\"body\"";
    const char *body_pos = strstr(json, body_key);
    if (body_pos) {
      const char *p = body_pos + strlen(body_key);
      while (*p && (*p == ':' || *p == ' ' || *p == '\t')) {
        p++;
      }

      if (*p == '{') {
        // 查找 value 字段
        const char *value_str = NULL;
        size_t value_len = 0;
        if (json_get_string_static(json, json_len, "value", &value_str,
                                   &value_len) == 0) {
          op->body_value = value_str;
          op->body_value_len = value_len;
        }
      }
    }
  }

  return 0;
}

/**
 * @brief 解析 operations 数组
 * @return 成功返回操作数量，失败返回 -1
 */
static int parse_operations_array(const char *json, size_t json_len,
                                  batch_operation_t **ops_out,
                                  size_t *count_out) {
  (void)json_len;
  // 查找 "operations" 数组
  const char *ops_key = "\"operations\"";
  const char *ops_pos = strstr(json, ops_key);
  if (!ops_pos) {
    return -1;
  }

  const char *p = ops_pos + strlen(ops_key);
  while (*p && (*p == ':' || *p == ' ' || *p == '\t')) {
    p++;
  }

  if (*p != '[') {
    return -1;
  }
  p++;

  // 计算操作数量（数左花括号）
  size_t capacity = 16;
  batch_operation_t *ops =
      (batch_operation_t *)malloc(capacity * sizeof(batch_operation_t));
  if (!ops) {
    return -1;
  }

  size_t count = 0;
  int brace_depth = 1;  // 数组深度
  const char *obj_start = NULL;

  while (*p && brace_depth > 0) {
    if (*p == '[') {
      brace_depth++;
    } else if (*p == ']') {
      brace_depth--;
    } else if (*p == '{') {
      if (brace_depth == 1) {
        // 开始一个新的操作对象
        obj_start = p;
      }
      if (brace_depth == 2 && obj_start) {
        // 在操作对象内部
      }
    } else if (*p == '}') {
      if (brace_depth == 2 && obj_start) {
        // 结束一个操作对象
        if (count >= capacity) {
          capacity *= 2;
          batch_operation_t *new_ops = (batch_operation_t *)realloc(
              ops, capacity * sizeof(batch_operation_t));
          if (!new_ops) {
            free(ops);
            return -1;
          }
          ops = new_ops;
        }

        memset(&ops[count], 0, sizeof(batch_operation_t));
        // 解析这个操作对象
        // 注意：这里需要复制 JSON 片段以便解析
        size_t obj_len = p - obj_start + 1;
        char *obj_json = (char *)malloc(obj_len + 1);
        if (!obj_json) {
          free(ops);
          return -1;
        }
        memcpy(obj_json, obj_start, obj_len);
        obj_json[obj_len] = '\0';

        if (parse_operation(obj_json, obj_len, &ops[count]) == 0) {
          count++;
        }

        free(obj_json);
        obj_start = NULL;
      }
    }
    p++;
  }

  *ops_out = ops;
  *count_out = count;
  return (int)count;
}

// ==================== 路径路由分发 ====================

/**
 * @brief 根据路径判断是对象操作还是集合操作
 * @return 0=对象，1=集合，-1=无效
 */
static int get_entity_type_from_path(const char *path) {
  if (!path) return -1;

  if (strncmp(path, "/obj/", 5) == 0 || strncmp(path, "/obj'", 4) == 0) {
    return 0;
  }
  if (strncmp(path, "/set/", 5) == 0 || strncmp(path, "/set'", 4) == 0) {
    return 1;
  }
  return -1;
}

/**
 * @brief 执行单个操作
 */
static int execute_operation(lmjcore_txn *txn,
                             batch_operation_t *op, http_response_t *response) {
  if (!op || !op->path) {
    return -1;
  }

  // 简化路径解析：提取实体类型和参数
  // 支持的路径格式：
  // /obj/{ptr}
  // /obj/{ptr}/{member}
  // /set/{ptr}
  // /set/{ptr}/elements

  const char *ptr_str = NULL;
  const char *member_name = NULL;
  char decoded_member[512] = {0};

  // 跳过前缀
  const char *path = op->path;
  int entity_type = get_entity_type_from_path(path);
  if (entity_type < 0) {
    build_error_response(HTTP_STATUS_BAD_REQUEST,
                         "Invalid path: must start with /obj/ or /set/",
                         response);
    return -1;
  }

  // 提取 ptr 和 member
  // 格式：/obj/{ptr} 或 /obj/{ptr}/{member}
  const char *first_slash = strchr(path + 1, '/');
  if (!first_slash) {
    build_error_response(HTTP_STATUS_BAD_REQUEST, "Invalid path format",
                         response);
    return -1;
  }

  ptr_str = first_slash + 1;

  // 查找第二个斜杠（成员名）
  const char *second_slash = strchr(ptr_str, '/');
  if (second_slash) {
    size_t ptr_len = second_slash - ptr_str;
    if (ptr_len != LMJCORE_PTR_STRING_LEN) {
      build_error_response(HTTP_STATUS_BAD_REQUEST, "Invalid pointer length",
                           response);
      return -1;
    }

    member_name = second_slash + 1;
    size_t member_len = strlen(member_name);

    // URL 解码成员名
    if (url_decode(member_name, member_len, decoded_member,
                   sizeof(decoded_member)) < 0) {
      build_error_response(HTTP_STATUS_BAD_REQUEST, "Invalid member name",
                           response);
      return -1;
    }
    member_name = decoded_member;
  }

  // 转换指针
  lmjcore_ptr obj_ptr;
  if (lmjcore_ptr_from_hex(ptr_str, obj_ptr) != 0) {
    build_error_response(HTTP_STATUS_BAD_REQUEST, "Invalid pointer format",
                         response);
    return -1;
  }

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, obj_ptr);
  if (exists != 1) {
    RETURN_ERROR_NOT_FOUND("Entity", response);
  }

  // 根据操作类型执行
  switch (op->method) {
    case BATCH_OP_GET: {
      if (entity_type == 0) {
        // 对象 GET
        if (member_name) {
          // 获取成员值
          size_t value_buf_size = 4096;
          uint8_t *value_buf = (uint8_t *)malloc(value_buf_size);
          if (!value_buf) {
            RETURN_ERROR_NO_MEMORY(response);
          }

          size_t value_len = 0;
          int rc = lmjcore_obj_member_get(
              txn, obj_ptr, (const uint8_t *)member_name, strlen(member_name),
              value_buf, value_buf_size, &value_len);

          if (rc == LMJCORE_ERROR_MEMBER_NOT_FOUND) {
            free(value_buf);
            RETURN_ERROR_MEMBER_NOT_FOUND(response);
          }
          if (rc != LMJCORE_SUCCESS) {
            free(value_buf);
            build_lmjcore_error_response(rc, response);
            return -1;
          }

          // 解码值
          char *value_str = NULL;
          api_value_type_t value_type;
          lmjcore_decode_value(value_buf, value_len, &value_str, &value_type);
          free(value_buf);

          const char *type_str =
              (value_type == VALUE_TYPE_RAW)    ? "raw"
              : (value_type == VALUE_TYPE_REF)  ? "ref"
              : (value_type == VALUE_TYPE_SET)  ? "set"
              : (value_type == VALUE_TYPE_NULL) ? "null"
                                                : "unknown";

          char json_buf[4096];
          snprintf(json_buf, sizeof(json_buf),
                   "{\"member\":\"%s\",\"value\":\"%s\",\"type\":\"%s\"}",
                   member_name, value_str ? value_str : "", type_str);
          free(value_str);

          op->result_body = strdup(json_buf);
          op->result_body_len = strlen(json_buf);
          op->result_status = HTTP_STATUS_OK;
        } else {
          // 获取完整对象（简化版本）
          // TODO: 实现完整对象获取
          char json_buf[256];
          snprintf(json_buf, sizeof(json_buf),
                   "{\"ptr\":\"%.*s\",\"type\":\"object\"}",
                   LMJCORE_PTR_STRING_LEN, ptr_str);
          op->result_body = strdup(json_buf);
          op->result_body_len = strlen(json_buf);
          op->result_status = HTTP_STATUS_OK;
        }
      } else {
        // 集合 GET（简化版本）
        char json_buf[256];
        snprintf(json_buf, sizeof(json_buf),
                 "{\"ptr\":\"%.*s\",\"type\":\"set\"}",
                 LMJCORE_PTR_STRING_LEN, ptr_str);
        op->result_body = strdup(json_buf);
        op->result_body_len = strlen(json_buf);
        op->result_status = HTTP_STATUS_OK;
      }
      break;
    }

    case BATCH_OP_PUT: {
      if (entity_type != 0 || !member_name) {
        build_error_response(HTTP_STATUS_BAD_REQUEST,
                             "PUT requires /obj/{ptr}/{member} path",
                             response);
        return -1;
      }

      // 编码值
      size_t encoded_size = 1 + LMJCORE_PTR_LEN + op->body_value_len + 16;
      uint8_t *encoded_value = (uint8_t *)malloc(encoded_size);
      if (!encoded_value) {
        RETURN_ERROR_NO_MEMORY(response);
      }

      size_t encoded_len = 0;
      int rc = lmjcore_encode_value(op->body_value, op->body_value_len,
                                    encoded_value, encoded_size, &encoded_len);
      if (rc != LMJCORE_SUCCESS) {
        free(encoded_value);
        build_lmjcore_error_response(rc, response);
        return -1;
      }

      // 设置成员值
      rc = lmjcore_obj_member_put(txn, obj_ptr, (const uint8_t *)member_name,
                                  strlen(member_name), encoded_value,
                                  encoded_len);
      free(encoded_value);

      if (rc != LMJCORE_SUCCESS) {
        build_lmjcore_error_response(rc, response);
        return -1;
      }

      op->result_body = strdup("{\"success\":true}");
      op->result_body_len = strlen(op->result_body);
      op->result_status = HTTP_STATUS_OK;
      break;
    }

    case BATCH_OP_POST: {
      if (entity_type == 0) {
        // 对象不支持 POST（应该用 PUT）
        build_error_response(HTTP_STATUS_BAD_REQUEST,
                             "POST not supported for objects, use PUT",
                             response);
        return -1;
      } else {
        // 集合添加元素
        if (!op->body_value) {
          build_error_response(HTTP_STATUS_BAD_REQUEST,
                               "POST requires value in body",
                               response);
          return -1;
        }

        // 编码值
        size_t encoded_size = 1 + LMJCORE_PTR_LEN + op->body_value_len + 16;
        uint8_t *encoded_value = (uint8_t *)malloc(encoded_size);
        if (!encoded_value) {
          RETURN_ERROR_NO_MEMORY(response);
        }

        size_t encoded_len = 0;
        int rc = lmjcore_encode_value(op->body_value, op->body_value_len,
                                      encoded_value, encoded_size, &encoded_len);
        if (rc != LMJCORE_SUCCESS) {
          free(encoded_value);
          build_lmjcore_error_response(rc, response);
          return -1;
        }

        // 添加元素到集合
        rc = lmjcore_set_add(txn, obj_ptr, encoded_value, encoded_len);
        free(encoded_value);

        if (rc != LMJCORE_SUCCESS) {
          build_lmjcore_error_response(rc, response);
          return -1;
        }

        op->result_body = strdup("{\"success\":true}");
        op->result_body_len = strlen(op->result_body);
        op->result_status = HTTP_STATUS_OK;
      }
      break;
    }

    case BATCH_OP_DELETE: {
      if (entity_type == 0) {
        // 对象删除成员
        if (!member_name) {
          // 删除整个对象
          int rc = lmjcore_obj_del(txn, obj_ptr);
          if (rc != LMJCORE_SUCCESS) {
            build_lmjcore_error_response(rc, response);
            return -1;
          }
        } else {
          // 删除成员
          int rc = lmjcore_obj_member_del(txn, obj_ptr,
                                          (const uint8_t *)member_name,
                                          strlen(member_name));
          if (rc == LMJCORE_ERROR_MEMBER_NOT_FOUND) {
            RETURN_ERROR_MEMBER_NOT_FOUND(response);
          }
          if (rc != LMJCORE_SUCCESS) {
            build_lmjcore_error_response(rc, response);
            return -1;
          }
        }
      } else {
        // 集合删除（简化）
        build_error_response(HTTP_STATUS_NOT_IMPLEMENTED,
                             "DELETE for set not fully implemented",
                             response);
        return -1;
      }

      op->result_body = strdup("{\"success\":true}");
      op->result_body_len = strlen(op->result_body);
      op->result_status = HTTP_STATUS_OK;
      break;
    }
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
  bool readonly = false;
  json_get_bool(hp->body, hp->body_len, "readonly", &readonly);

  batch_operation_t *operations = NULL;
  size_t op_count = 0;

  if (parse_operations_array(hp->body, hp->body_len, &operations,
                             &op_count) < 0 ||
      op_count == 0) {
    build_error_response(HTTP_STATUS_BAD_REQUEST,
                         "Invalid or empty operations array",
                         response);
    return -1;
  }

  // 只读事务检查：是否包含写操作
  if (readonly) {
    for (size_t i = 0; i < op_count; i++) {
      if (operations[i].method == BATCH_OP_PUT ||
          operations[i].method == BATCH_OP_POST ||
          operations[i].method == BATCH_OP_DELETE) {
        build_error_response(
            HTTP_STATUS_BAD_REQUEST,
            "Readonly transaction cannot contain write operations",
            response);
        free(operations);
        return -1;
      }
    }
  }

  // 开启事务
  lmjcore_txn *txn = NULL;
  int txn_flags = readonly ? LMJCORE_TXN_READONLY : 0;
  int rc = lmjcore_txn_begin(hp->env, NULL, txn_flags, &txn);
  if (rc != LMJCORE_SUCCESS || !txn) {
    free(operations);
    RETURN_ERROR_TXN_FAILED("begin", response);
  }

  // 执行所有操作
  for (size_t i = 0; i < op_count; i++) {
    // 检查事务超时
    CHECK_TXN_TIMEOUT(hp, response, txn);

    http_response_t op_response = {0};

    if (execute_operation(txn, &operations[i], &op_response) != 0) {
      // 操作失败，回滚事务
      lmjcore_txn_abort(txn);

      // 复制错误响应
      operations[i].result_status = op_response.status_code;
      if (op_response.body) {
        operations[i].result_body = strdup(op_response.body);
        operations[i].result_body_len = op_response.body_len;
      }

      // 构建错误响应
      char *error_json = (char *)malloc(4096);
      if (!error_json) {
        free(operations);
        RETURN_ERROR_NO_MEMORY(response);
      }

      int offset = snprintf(error_json, 4096,
                            "{\"success\":false,\"error\":\"Operation %zu failed\",",
                            i);
      if (op_response.body) {
        offset += snprintf(error_json + offset, 4096 - offset,
                           "\"details\":%.*s",
                           (int)op_response.body_len, op_response.body);
      } else {
        offset += snprintf(error_json + offset, 4096 - offset,
                           "\"details\":null");
      }
      snprintf(error_json + offset, 4096 - offset, "}");

      build_error_response(HTTP_STATUS_BAD_REQUEST, error_json, response);
      free(error_json);
      free(operations);
      return -1;
    }

    // 释放临时响应
    if (op_response.body) {
      free(op_response.body);
    }
  }

  // 提交事务（只读事务不需要提交，直接 abort）
  if (readonly) {
    lmjcore_txn_abort(txn);
  } else {
    rc = lmjcore_txn_commit(txn);
    if (rc != LMJCORE_SUCCESS) {
      lmjcore_txn_abort(txn);
      free(operations);
      RETURN_ERROR_TXN_FAILED("commit", response);
    }
  }

  // 构建响应 JSON
  size_t json_size = 8192;
  char *json_buf = (char *)malloc(json_size);
  if (!json_buf) {
    free(operations);
    RETURN_ERROR_NO_MEMORY(response);
  }

  int offset = snprintf(json_buf, json_size,
                        "{\"success\":true,\"results\":[");

  for (size_t i = 0; i < op_count; i++) {
    // 检查缓冲区空间
    while ((size_t)offset + 1024 + operations[i].result_body_len >= json_size) {
      json_size *= 2;
      char *new_buf = (char *)realloc(json_buf, json_size);
      if (!new_buf) {
        free(json_buf);
        free(operations);
        RETURN_ERROR_NO_MEMORY(response);
      }
      json_buf = new_buf;
    }

    offset += snprintf(json_buf + offset, json_size - offset,
                       "%s{\"status\":%d,\"body\":%.*s}",
                       i > 0 ? "," : "",
                       operations[i].result_status,
                       (int)operations[i].result_body_len,
                       operations[i].result_body);
  }

  offset += snprintf(json_buf + offset, json_size - offset, "]}");

  // 清理
  for (size_t i = 0; i < op_count; i++) {
    if (operations[i].result_body) {
      free(operations[i].result_body);
    }
  }
  free(operations);

  response->status_code = HTTP_STATUS_OK;
  response->body = json_buf;
  response->body_len = strlen(json_buf);

  return 0;
}
