#include "../include/lmjcore_web.h"
#include "../include/chain_query.h"
#include "../include/http_server.h"
#include <json-c/json.h>
#include <lmdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// 全局状态
static struct {
  lmjcore_env *env;
  http_server_t *server;
  lmjcore_web_config_t config;
  bool running;
  char *ptr_gen_ctx;
} g_web;

// 前向声明
static void handle_ptr_get(http_request_t *req, http_response_t *res,
                           void *ctx);
static void handle_ptr_put(http_request_t *req, http_response_t *res,
                           void *ctx);
static void handle_ptr_delete(http_request_t *req, http_response_t *res,
                              void *ctx);
static void handle_obj_create(http_request_t *req, http_response_t *res,
                              void *ctx);
static void handle_set_create(http_request_t *req, http_response_t *res,
                              void *ctx);
static void handle_set_add(http_request_t *req, http_response_t *res,
                           void *ctx);
static void handle_set_all(http_request_t *req, http_response_t *res,
                           void *ctx);
static void handle_set_contains(http_request_t *req, http_response_t *res,
                                void *ctx);
static void handle_ptr_generate(http_request_t *req, http_response_t *res,
                                void *ctx);
static void handle_audit(http_request_t *req, http_response_t *res, void *ctx);
static void handle_repair(http_request_t *req, http_response_t *res, void *ctx);

// 工具函数
static char *ptr_to_hex(const lmjcore_ptr ptr, char *buf, size_t buf_size) {
  for (int i = 0; i < LMJCORE_PTR_LEN; i++) {
    sprintf(buf + (i * 2), "%02x", ptr[i]);
  }
  return buf;
}

static int hex_to_ptr(const char *hex, lmjcore_ptr ptr) {
  size_t len = strlen(hex);
  if (len != LMJCORE_PTR_LEN * 2) {
    return LMJCORE_WEB_ERROR_INVALID_PTR;
  }

  for (size_t i = 0; i < LMJCORE_PTR_LEN; i++) {
    char byte_str[3] = {hex[i * 2], hex[i * 2 + 1], 0};
    ptr[i] = (uint8_t)strtol(byte_str, NULL, 16);
  }
  return LMJCORE_WEB_SUCCESS;
}

static json_object *create_error_response(int code, const char *message) {
  json_object *obj = json_object_new_object();
  json_object_object_add(obj, "code", json_object_new_int(code));
  json_object_object_add(obj, "message", json_object_new_string(message));
  return obj;
}

static void send_json_response(http_response_t *res, int code,
                               json_object *data) {
  json_object *wrapper = json_object_new_object();
  json_object_object_add(wrapper, "code", json_object_new_int(code));
  if (data) {
    json_object_object_add(wrapper, "data", data);
  }

  const char *json_str = json_object_to_json_string(wrapper);
  http_response_send_string(res, json_str);
  json_object_put(wrapper);
}

static lmjcore_txn *get_read_txn(void) {
  lmjcore_txn *txn = NULL;
  int rc = lmjcore_txn_begin(g_web.env, NULL, LMJCORE_TXN_READONLY, &txn);
  if (rc != LMJCORE_SUCCESS) {
    return NULL;
  }
  return txn;
}

static lmjcore_txn *get_write_txn(void) {
  lmjcore_txn *txn = NULL;
  int rc = lmjcore_txn_begin(g_web.env, NULL, 0, &txn);
  if (rc != LMJCORE_SUCCESS) {
    return NULL;
  }
  return txn;
}

// 初始化
int lmjcore_web_init(const lmjcore_web_config_t *config) {
  if (!config || !config->db_path) {
    return LMJCORE_ERROR_INVALID_PARAM;
  }

  memcpy(&g_web.config, config, sizeof(lmjcore_web_config_t));

  // 初始化 LMJCore
  int rc = lmjcore_init(config->db_path, config->map_size, config->env_flags,
                        NULL, NULL, &g_web.env);
  if (rc != LMJCORE_SUCCESS) {
    fprintf(stderr, "Failed to initialize LMJCore: %s\n", lmjcore_strerror(rc));
    return rc;
  }

  // 创建 HTTP 服务器
  g_web.server = http_server_create(config->bind_host, config->bind_port);
  if (!g_web.server) {
    lmjcore_cleanup(g_web.env);
    return LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED;
  }

  // 注册路由
  http_server_on(g_web.server, HTTP_GET, "/ptr/*", handle_ptr_get, NULL);
  http_server_on(g_web.server, HTTP_PUT, "/ptr/*", handle_ptr_put, NULL);
  http_server_on(g_web.server, HTTP_DELETE, "/ptr/*", handle_ptr_delete, NULL);
  http_server_on(g_web.server, HTTP_POST, "/obj", handle_obj_create, NULL);
  http_server_on(g_web.server, HTTP_POST, "/set", handle_set_create, NULL);
  http_server_on(g_web.server, HTTP_POST, "/ptr/*/add", handle_set_add, NULL);
  http_server_on(g_web.server, HTTP_GET, "/ptr/*/all", handle_set_all, NULL);
  http_server_on(g_web.server, HTTP_GET, "/ptr/*/contains", handle_set_contains,
                 NULL);
  http_server_on(g_web.server, HTTP_POST, "/ptr/generate", handle_ptr_generate,
                 NULL);
  http_server_on(g_web.server, HTTP_GET, "/audit/*", handle_audit, NULL);
  http_server_on(g_web.server, HTTP_POST, "/repair/*", handle_repair, NULL);

  return LMJCORE_WEB_SUCCESS;
}

int lmjcore_web_start(void) {
  if (!g_web.server) {
    return LMJCORE_ERROR_INVALID_POINTER;
  }

  g_web.running = true;
  printf("LMJCore Web Server starting on %s:%d\n", g_web.config.bind_host,
         g_web.config.bind_port);
  printf("Database path: %s\n", g_web.config.db_path);

  return http_server_start(g_web.server);
}

int lmjcore_web_stop(void) {
  if (g_web.server) {
    http_server_stop(g_web.server);
  }
  g_web.running = false;
  return LMJCORE_WEB_SUCCESS;
}

int lmjcore_web_cleanup(void) {
  lmjcore_web_stop();

  if (g_web.server) {
    http_server_destroy(g_web.server);
    g_web.server = NULL;
  }

  if (g_web.env) {
    lmjcore_cleanup(g_web.env);
    g_web.env = NULL;
  }

  return LMJCORE_WEB_SUCCESS;
}

int lmjcore_web_generate_ptr(uint8_t type, char *out_hex, size_t out_size) {
  if (out_size < LMJCORE_PTR_STRING_BUF_SIZE) {
    return LMJCORE_ERROR_BUFFER_TOO_SMALL;
  }

  lmjcore_ptr ptr;
  memset(ptr, 0, LMJCORE_PTR_LEN);
  ptr[0] = type;

  // 生成随机部分
  FILE *urandom = fopen("/dev/urandom", "rb");
  if (urandom) {
    fread(ptr + 1, 1, LMJCORE_PTR_LEN - 1, urandom);
    fclose(urandom);
  }

  ptr_to_hex(ptr, out_hex, out_size);
  return LMJCORE_WEB_SUCCESS;
}

// 路由处理器实现
static void handle_ptr_get(http_request_t *req, http_response_t *res,
                           void *ctx) {
  const char *path = http_request_path(req);

  // 解析路径: /ptr/01abc.../rest/of/path
  if (strncmp(path, "/ptr/", 5) != 0) {
    send_json_response(
        res, LMJCORE_ERROR_INVALID_PARAM,
        create_error_response(LMJCORE_ERROR_INVALID_PARAM, "Invalid path"));
    return;
  }

  const char *after_ptr = path + 5;
  const char *slash = strchr(after_ptr, '/');

  lmjcore_ptr root_ptr;
  char ptr_hex[LMJCORE_PTR_STRING_BUF_SIZE];
  int hex_len;

  if (slash) {
    hex_len = slash - after_ptr;
    strncpy(ptr_hex, after_ptr, hex_len);
    ptr_hex[hex_len] = '\0';
  } else {
    strcpy(ptr_hex, after_ptr);
    hex_len = strlen(after_ptr);
  }

  int rc = hex_to_ptr(ptr_hex, root_ptr);
  if (rc != LMJCORE_WEB_SUCCESS) {
    send_json_response(res, rc,
                       create_error_response(rc, "Invalid pointer format"));
    return;
  }

  lmjcore_txn *txn = get_read_txn();
  if (!txn) {
    send_json_response(
        res, LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED,
        create_error_response(LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED,
                              "Failed to create transaction"));
    return;
  }

  if (!slash || *(slash + 1) == '\0') {
    // 获取整个对象
    size_t total_len = 0;
    size_t member_count = 0;
    rc = lmjcore_obj_stat_members(txn, root_ptr, &total_len, &member_count);

    if (rc == LMJCORE_SUCCESS) {
      json_object *obj_data = json_object_new_object();
      json_object *members = json_object_new_array();

      // 分配缓冲区获取成员列表
      size_t buf_size = 4096 + total_len;
      uint8_t *buf = malloc(buf_size);
      if (buf) {
        lmjcore_result_set *result;
        rc = lmjcore_obj_member_list(txn, root_ptr, buf, buf_size, &result);
        if (rc == LMJCORE_SUCCESS) {
          for (size_t i = 0; i < result->element_count; i++) {
            lmjcore_descriptor *desc = &result->elements[i];
            char *member_name = (char *)(buf + desc->value_offset);
            size_t member_len = desc->value_len;

            // 获取成员值
            uint8_t value_buf[4096];
            size_t value_len;
            rc = lmjcore_obj_member_get(txn, root_ptr, (uint8_t *)member_name,
                                        member_len, value_buf,
                                        sizeof(value_buf), &value_len);

            json_object *member = json_object_new_object();
            json_object_object_add(
                member, "name",
                json_object_new_string_len(member_name, member_len));

            if (rc == LMJCORE_SUCCESS) {
              // 检查是否是指针
              if (value_len == LMJCORE_PTR_LEN) {
                char ptr_str[LMJCORE_PTR_STRING_BUF_SIZE];
                ptr_to_hex(value_buf, ptr_str, sizeof(ptr_str));
                json_object_object_add(member, "type",
                                       json_object_new_string("ptr"));
                json_object_object_add(member, "ptr",
                                       json_object_new_string(ptr_str));
              } else {
                json_object_object_add(member, "type",
                                       json_object_new_string("binary"));
                json_object_object_add(member, "base64",
                                       json_object_new_string("...")); // 简化
              }
            } else if (rc == LMJCORE_ERROR_MEMBER_NOT_FOUND) {
              json_object_object_add(member, "type",
                                     json_object_new_string("missing"));
            }

            json_object_array_add(members, member);  // 修复：使用 json_object_array_add
          }
        }
        free(buf);
      }

      json_object_object_add(obj_data, "members", members);
      json_object_object_add(obj_data, "member_count",
                             json_object_new_int(member_count));
      send_json_response(res, LMJCORE_WEB_SUCCESS, obj_data);
    } else {
      send_json_response(res, rc,
                         create_error_response(rc, "Failed to get object"));
    }
  } else {
    // 链式查询
    const char *query_path = slash + 1;
    query_result_t result;
    memset(&result, 0, sizeof(result));

    rc = chain_query_parse(txn, root_ptr, query_path, &result);

    if (rc == LMJCORE_SUCCESS) {
      json_object *data = json_object_new_object();

      switch (result.type) {
      case QUERY_RESULT_VALUE:
        json_object_object_add(data, "type", json_object_new_string("value"));
        json_object_object_add(data, "base64",
                               json_object_new_string("...")); // 简化
        break;
      case QUERY_RESULT_PTR: {
        char ptr_str[LMJCORE_PTR_STRING_BUF_SIZE];
        ptr_to_hex(result.data, ptr_str, sizeof(ptr_str));
        json_object_object_add(data, "type", json_object_new_string("ptr"));
        json_object_object_add(data, "ptr", json_object_new_string(ptr_str));
        break;
      }
      case QUERY_RESULT_SET_ALL:
        json_object_object_add(data, "type", json_object_new_string("set"));
        json_object_object_add(data, "count", json_object_new_int(0)); // 简化
        break;
      default:
        break;
      }

      send_json_response(res, LMJCORE_WEB_SUCCESS, data);
      chain_query_result_free(&result);
    } else {
      send_json_response(res, rc, create_error_response(rc, "Query failed"));
    }
  }

  lmjcore_txn_abort(txn);
}

static void handle_ptr_put(http_request_t *req, http_response_t *res,
                           void *ctx) {
  const char *path = http_request_path(req);

  // 解析路径: /ptr/01abc.../member_name
  if (strncmp(path, "/ptr/", 5) != 0) {
    send_json_response(
        res, LMJCORE_ERROR_INVALID_PARAM,
        create_error_response(LMJCORE_ERROR_INVALID_PARAM, "Invalid path"));
    return;
  }

  const char *after_ptr = path + 5;
  const char *slash = strchr(after_ptr, '/');

  if (!slash || *(slash + 1) == '\0') {
    send_json_response(res, LMJCORE_ERROR_INVALID_PARAM,
                       create_error_response(LMJCORE_ERROR_INVALID_PARAM,
                                             "Missing member name"));
    return;
  }

  lmjcore_ptr obj_ptr;
  char ptr_hex[LMJCORE_PTR_STRING_BUF_SIZE];
  int hex_len = slash - after_ptr;
  strncpy(ptr_hex, after_ptr, hex_len);
  ptr_hex[hex_len] = '\0';

  int rc = hex_to_ptr(ptr_hex, obj_ptr);
  if (rc != LMJCORE_WEB_SUCCESS) {
    send_json_response(res, rc,
                       create_error_response(rc, "Invalid pointer format"));
    return;
  }

  const char *member_name = slash + 1;
  size_t member_len = strlen(member_name);

  size_t body_len;
  const uint8_t *body = http_request_body(req, &body_len);

  lmjcore_txn *txn = get_write_txn();
  if (!txn) {
    send_json_response(
        res, LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED,
        create_error_response(LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED,
                              "Failed to create transaction"));
    return;
  }

  // 检查对象是否存在
  rc = lmjcore_entity_exist(txn, obj_ptr);
  if (rc == 0) {
    // 对象不存在，先创建
    rc = lmjcore_obj_create(txn, obj_ptr);
    if (rc != LMJCORE_SUCCESS) {
      lmjcore_txn_abort(txn);
      send_json_response(res, rc,
                         create_error_response(rc, "Failed to create object"));
      return;
    }
  }

  // 写入成员
  rc = lmjcore_obj_member_put(txn, obj_ptr, (const uint8_t *)member_name,
                              member_len, body, body_len);

  if (rc == LMJCORE_SUCCESS) {
    rc = lmjcore_txn_commit(txn);
    if (rc == LMJCORE_SUCCESS) {
      send_json_response(res, LMJCORE_WEB_SUCCESS, NULL);
    } else {
      lmjcore_txn_abort(txn);
      send_json_response(res, rc, create_error_response(rc, "Commit failed"));
    }
  } else {
    lmjcore_txn_abort(txn);
    send_json_response(res, rc, create_error_response(rc, "Put failed"));
  }
}

static void handle_ptr_delete(http_request_t *req, http_response_t *res,
                              void *ctx) {
  const char *path = http_request_path(req);

  if (strncmp(path, "/ptr/", 5) != 0) {
    send_json_response(
        res, LMJCORE_ERROR_INVALID_PARAM,
        create_error_response(LMJCORE_ERROR_INVALID_PARAM, "Invalid path"));
    return;
  }

  const char *after_ptr = path + 5;
  const char *slash = strchr(after_ptr, '/');

  lmjcore_ptr ptr;
  char ptr_hex[LMJCORE_PTR_STRING_BUF_SIZE];
  int hex_len;

  if (slash) {
    hex_len = slash - after_ptr;
    strncpy(ptr_hex, after_ptr, hex_len);
    ptr_hex[hex_len] = '\0';
  } else {
    strcpy(ptr_hex, after_ptr);
    hex_len = strlen(after_ptr);
  }

  int rc = hex_to_ptr(ptr_hex, ptr);
  if (rc != LMJCORE_WEB_SUCCESS) {
    send_json_response(res, rc,
                       create_error_response(rc, "Invalid pointer format"));
    return;
  }

  lmjcore_txn *txn = get_write_txn();
  if (!txn) {
    send_json_response(
        res, LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED,
        create_error_response(LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED,
                              "Failed to create transaction"));
    return;
  }

  if (slash && *(slash + 1) != '\0') {
    // 删除成员
    const char *member_name = slash + 1;
    rc = lmjcore_obj_member_del(txn, ptr, (const uint8_t *)member_name,
                                strlen(member_name));
  } else {
    // 删除整个实体
    rc = lmjcore_obj_del(txn, ptr);
  }

  if (rc == LMJCORE_SUCCESS) {
    rc = lmjcore_txn_commit(txn);
    if (rc == LMJCORE_SUCCESS) {
      send_json_response(res, LMJCORE_WEB_SUCCESS, NULL);
    } else {
      lmjcore_txn_abort(txn);
      send_json_response(res, rc, create_error_response(rc, "Commit failed"));
    }
  } else {
    lmjcore_txn_abort(txn);
    send_json_response(res, rc, create_error_response(rc, "Delete failed"));
  }
}

static void handle_obj_create(http_request_t *req, http_response_t *res,
                              void *ctx) {
  lmjcore_ptr ptr;
  lmjcore_txn *txn = get_write_txn();

  if (!txn) {
    send_json_response(
        res, LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED,
        create_error_response(LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED,
                              "Failed to create transaction"));
    return;
  }

  int rc = lmjcore_obj_create(txn, ptr);
  if (rc == LMJCORE_SUCCESS) {
    rc = lmjcore_txn_commit(txn);
    if (rc == LMJCORE_SUCCESS) {
      char ptr_hex[LMJCORE_PTR_STRING_BUF_SIZE];
      ptr_to_hex(ptr, ptr_hex, sizeof(ptr_hex));

      json_object *data = json_object_new_object();
      json_object_object_add(data, "ptr", json_object_new_string(ptr_hex));
      send_json_response(res, LMJCORE_WEB_SUCCESS, data);
    } else {
      lmjcore_txn_abort(txn);
      send_json_response(res, rc, create_error_response(rc, "Commit failed"));
    }
  } else {
    lmjcore_txn_abort(txn);
    send_json_response(res, rc, create_error_response(rc, "Create failed"));
  }
}

static void handle_set_create(http_request_t *req, http_response_t *res,
                              void *ctx) {
  lmjcore_ptr ptr;
  lmjcore_txn *txn = get_write_txn();

  if (!txn) {
    send_json_response(
        res, LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED,
        create_error_response(LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED,
                              "Failed to create transaction"));
    return;
  }

  int rc = lmjcore_set_create(txn, ptr);
  if (rc == LMJCORE_SUCCESS) {
    rc = lmjcore_txn_commit(txn);
    if (rc == LMJCORE_SUCCESS) {
      char ptr_hex[LMJCORE_PTR_STRING_BUF_SIZE];
      ptr_to_hex(ptr, ptr_hex, sizeof(ptr_hex));

      json_object *data = json_object_new_object();
      json_object_object_add(data, "ptr", json_object_new_string(ptr_hex));
      send_json_response(res, LMJCORE_WEB_SUCCESS, data);
    } else {
      lmjcore_txn_abort(txn);
      send_json_response(res, rc, create_error_response(rc, "Commit failed"));
    }
  } else {
    lmjcore_txn_abort(txn);
    send_json_response(res, rc, create_error_response(rc, "Create failed"));
  }
}

static void handle_set_add(http_request_t *req, http_response_t *res,
                           void *ctx) {
  const char *path = http_request_path(req);

  // 解析路径: /ptr/01abc.../add
  if (strncmp(path, "/ptr/", 5) != 0) {
    send_json_response(
        res, LMJCORE_ERROR_INVALID_PARAM,
        create_error_response(LMJCORE_ERROR_INVALID_PARAM, "Invalid path"));
    return;
  }

  const char *after_ptr = path + 5;
  const char *slash = strchr(after_ptr, '/');

  if (!slash || strcmp(slash + 1, "add") != 0) {
    send_json_response(res, LMJCORE_ERROR_INVALID_PARAM,
                       create_error_response(LMJCORE_ERROR_INVALID_PARAM,
                                             "Expected /add endpoint"));
    return;
  }

  lmjcore_ptr set_ptr;
  char ptr_hex[LMJCORE_PTR_STRING_BUF_SIZE];
  int hex_len = slash - after_ptr;
  strncpy(ptr_hex, after_ptr, hex_len);
  ptr_hex[hex_len] = '\0';

  int rc = hex_to_ptr(ptr_hex, set_ptr);
  if (rc != LMJCORE_WEB_SUCCESS) {
    send_json_response(res, rc,
                       create_error_response(rc, "Invalid pointer format"));
    return;
  }

  size_t body_len;
  const uint8_t *body = http_request_body(req, &body_len);

  lmjcore_txn *txn = get_write_txn();
  if (!txn) {
    send_json_response(
        res, LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED,
        create_error_response(LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED,
                              "Failed to create transaction"));
    return;
  }

  rc = lmjcore_set_add(txn, set_ptr, body, body_len);

  if (rc == LMJCORE_SUCCESS) {
    rc = lmjcore_txn_commit(txn);
    if (rc == LMJCORE_SUCCESS) {
      send_json_response(res, LMJCORE_WEB_SUCCESS, NULL);
    } else {
      lmjcore_txn_abort(txn);
      send_json_response(res, rc, create_error_response(rc, "Commit failed"));
    }
  } else {
    lmjcore_txn_abort(txn);
    send_json_response(res, rc, create_error_response(rc, "Add failed"));
  }
}

static void handle_set_all(http_request_t *req, http_response_t *res,
                           void *ctx) {
  const char *path = http_request_path(req);

  if (strncmp(path, "/ptr/", 5) != 0) {
    send_json_response(
        res, LMJCORE_ERROR_INVALID_PARAM,
        create_error_response(LMJCORE_ERROR_INVALID_PARAM, "Invalid path"));
    return;
  }

  const char *after_ptr = path + 5;
  const char *slash = strchr(after_ptr, '/');

  if (!slash || strcmp(slash + 1, "all") != 0) {
    send_json_response(res, LMJCORE_ERROR_INVALID_PARAM,
                       create_error_response(LMJCORE_ERROR_INVALID_PARAM,
                                             "Expected /all endpoint"));
    return;
  }

  lmjcore_ptr set_ptr;
  char ptr_hex[LMJCORE_PTR_STRING_BUF_SIZE];
  int hex_len = slash - after_ptr;
  strncpy(ptr_hex, after_ptr, hex_len);
  ptr_hex[hex_len] = '\0';

  int rc = hex_to_ptr(ptr_hex, set_ptr);
  if (rc != LMJCORE_WEB_SUCCESS) {
    send_json_response(res, rc,
                       create_error_response(rc, "Invalid pointer format"));
    return;
  }

  lmjcore_txn *txn = get_read_txn();
  if (!txn) {
    send_json_response(
        res, LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED,
        create_error_response(LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED,
                              "Failed to create transaction"));
    return;
  }

  size_t total_len = 0;
  size_t element_count = 0;
  rc = lmjcore_set_stat(txn, set_ptr, &total_len, &element_count);

  if (rc == LMJCORE_SUCCESS) {
    json_object *data = json_object_new_object();
    json_object *elements = json_object_new_array();

    // 分配缓冲区获取元素
    size_t buf_size = 4096 + total_len;
    uint8_t *buf = malloc(buf_size);
    if (buf) {
      lmjcore_result_set *result;
      rc = lmjcore_set_get(txn, set_ptr, buf, buf_size, &result);
      if (rc == LMJCORE_SUCCESS) {
        for (size_t i = 0; i < result->element_count; i++) {
          lmjcore_descriptor *desc = &result->elements[i];
          char *element = (char *)(buf + desc->value_offset);
          size_t element_len = desc->value_len;

          json_object *elem_obj = json_object_new_object();
          json_object_object_add(elem_obj, "value",
                                 json_object_new_string_len(element, element_len));
          json_object_array_add(elements, elem_obj);
        }
      }
      free(buf);
    }

    json_object_object_add(data, "elements", elements);
    json_object_object_add(data, "count", json_object_new_int(element_count));
    send_json_response(res, LMJCORE_WEB_SUCCESS, data);
  } else {
    send_json_response(res, rc, create_error_response(rc, "Failed to get set"));
  }

  lmjcore_txn_abort(txn);
}

static void handle_set_contains(http_request_t *req, http_response_t *res,
                                void *ctx) {
  const char *path = http_request_path(req);

  if (strncmp(path, "/ptr/", 5) != 0) {
    send_json_response(
        res, LMJCORE_ERROR_INVALID_PARAM,
        create_error_response(LMJCORE_ERROR_INVALID_PARAM, "Invalid path"));
    return;
  }

  const char *after_ptr = path + 5;
  const char *slash = strchr(after_ptr, '/');

  if (!slash || strcmp(slash + 1, "contains") != 0) {
    send_json_response(res, LMJCORE_ERROR_INVALID_PARAM,
                       create_error_response(LMJCORE_ERROR_INVALID_PARAM,
                                             "Expected /contains endpoint"));
    return;
  }

  lmjcore_ptr set_ptr;
  char ptr_hex[LMJCORE_PTR_STRING_BUF_SIZE];
  int hex_len = slash - after_ptr;
  strncpy(ptr_hex, after_ptr, hex_len);
  ptr_hex[hex_len] = '\0';

  int rc = hex_to_ptr(ptr_hex, set_ptr);
  if (rc != LMJCORE_WEB_SUCCESS) {
    send_json_response(res, rc,
                       create_error_response(rc, "Invalid pointer format"));
    return;
  }

  size_t body_len;
  const uint8_t *body = http_request_body(req, &body_len);

  lmjcore_txn *txn = get_read_txn();
  if (!txn) {
    send_json_response(
        res, LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED,
        create_error_response(LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED,
                              "Failed to create transaction"));
    return;
  }

  rc = lmjcore_set_contains(txn, set_ptr, body, body_len);

  json_object *data = json_object_new_object();
  json_object_object_add(data, "exists", json_object_new_boolean(rc == 1));
  send_json_response(res, LMJCORE_WEB_SUCCESS, data);

  lmjcore_txn_abort(txn);
}

static void handle_ptr_generate(http_request_t *req, http_response_t *res,
                                void *ctx) {
  const char *content_type = http_request_header(req, "Content-Type");
  uint8_t type = LMJCORE_OBJ;

  if (content_type && strstr(content_type, "set")) {
    type = LMJCORE_SET;
  }

  char ptr_hex[LMJCORE_PTR_STRING_BUF_SIZE];
  int rc = lmjcore_web_generate_ptr(type, ptr_hex, sizeof(ptr_hex));

  if (rc == LMJCORE_WEB_SUCCESS) {
    json_object *data = json_object_new_object();
    json_object_object_add(data, "ptr", json_object_new_string(ptr_hex));
    send_json_response(res, LMJCORE_WEB_SUCCESS, data);
  } else {
    send_json_response(res, rc,
                       create_error_response(rc, "Failed to generate pointer"));
  }
}

static void handle_audit(http_request_t *req, http_response_t *res, void *ctx) {
  const char *path = http_request_path(req);

  if (strncmp(path, "/audit/", 7) != 0) {
    send_json_response(
        res, LMJCORE_ERROR_INVALID_PARAM,
        create_error_response(LMJCORE_ERROR_INVALID_PARAM, "Invalid path"));
    return;
  }

  const char *ptr_hex = path + 7;
  lmjcore_ptr obj_ptr;

  int rc = hex_to_ptr(ptr_hex, obj_ptr);
  if (rc != LMJCORE_WEB_SUCCESS) {
    send_json_response(res, rc,
                       create_error_response(rc, "Invalid pointer format"));
    return;
  }

  lmjcore_txn *txn = get_read_txn();
  if (!txn) {
    send_json_response(
        res, LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED,
        create_error_response(LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED,
                              "Failed to create transaction"));
    return;
  }

  // 审计需要先获取统计信息
  size_t total_member_len, member_count;
  rc = lmjcore_obj_stat_members(txn, obj_ptr, &total_member_len, &member_count);

  if (rc == LMJCORE_SUCCESS) {
    size_t buf_size = 4096 + total_member_len * 2;
    uint8_t *buf = malloc(buf_size);
    lmjcore_audit_report *report;

    rc = lmjcore_audit_object(txn, obj_ptr, buf, buf_size, &report);

    json_object *data = json_object_new_object();
    json_object *ghosts = json_object_new_array();

    if (rc == LMJCORE_SUCCESS && report) {
      for (size_t i = 0; i < report->audit_count; i++) {
        lmjcore_audit_descriptor *desc = &report->audit_descriptor[i];
        char ptr_str[LMJCORE_PTR_STRING_BUF_SIZE];
        ptr_to_hex(desc->ptr, ptr_str, sizeof(ptr_str));

        char *member_name =
            (char *)(buf + desc->member.member_name.value_offset);
        size_t member_len = desc->member.member_name.value_len;

        json_object *ghost = json_object_new_object();
        json_object_object_add(ghost, "ptr", json_object_new_string(ptr_str));
        json_object_object_add(
            ghost, "member",
            json_object_new_string_len(member_name, member_len));
        json_object_array_add(ghosts, ghost);
      }
    }

    json_object_object_add(data, "ghost_members", ghosts);
    json_object_object_add(data, "missing_values", json_object_new_array());
    json_object_object_add(data, "integrity",
                           json_object_new_string(report && report->audit_count > 0
                                                      ? "damaged"
                                                      : "healthy"));

    send_json_response(res, LMJCORE_WEB_SUCCESS, data);
    free(buf);
  } else {
    send_json_response(res, rc,
                       create_error_response(rc, "Failed to audit object"));
  }

  lmjcore_txn_abort(txn);
}

static void handle_repair(http_request_t *req, http_response_t *res,
                          void *ctx) {
  const char *path = http_request_path(req);

  if (strncmp(path, "/repair/", 8) != 0) {
    send_json_response(
        res, LMJCORE_ERROR_INVALID_PARAM,
        create_error_response(LMJCORE_ERROR_INVALID_PARAM, "Invalid path"));
    return;
  }

  const char *ptr_hex = path + 8;
  lmjcore_ptr obj_ptr;

  int rc = hex_to_ptr(ptr_hex, obj_ptr);
  if (rc != LMJCORE_WEB_SUCCESS) {
    send_json_response(res, rc,
                       create_error_response(rc, "Invalid pointer format"));
    return;
  }

  lmjcore_txn *txn = get_write_txn();
  if (!txn) {
    send_json_response(
        res, LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED,
        create_error_response(LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED,
                              "Failed to create transaction"));
    return;
  }

  // 先审计获取报告
  size_t total_member_len, member_count;
  rc = lmjcore_obj_stat_members(txn, obj_ptr, &total_member_len, &member_count);

  if (rc == LMJCORE_SUCCESS) {
    size_t buf_size = 4096 + total_member_len * 2;
    uint8_t *buf = malloc(buf_size);
    lmjcore_audit_report *report;

    rc = lmjcore_audit_object(txn, obj_ptr, buf, buf_size, &report);

    if (rc == LMJCORE_SUCCESS && report && report->audit_count > 0) {
      rc = lmjcore_repair_object(txn, buf, buf_size, report);

      if (rc == LMJCORE_SUCCESS) {
        rc = lmjcore_txn_commit(txn);
        if (rc == LMJCORE_SUCCESS) {
          json_object *data = json_object_new_object();
          json_object_object_add(data, "removed_ghosts",
                                 json_object_new_int(report->audit_count));
          json_object_object_add(data, "message",
                                 json_object_new_string("repaired"));
          send_json_response(res, LMJCORE_WEB_SUCCESS, data);
        } else {
          lmjcore_txn_abort(txn);
          send_json_response(res, rc,
                             create_error_response(rc, "Commit failed"));
        }
      } else {
        lmjcore_txn_abort(txn);
        send_json_response(res, rc, create_error_response(rc, "Repair failed"));
      }
    } else {
      lmjcore_txn_abort(txn);
      send_json_response(res, rc, create_error_response(rc, "No ghosts found"));
    }

    free(buf);
  } else {
    lmjcore_txn_abort(txn);
    send_json_response(res, rc,
                       create_error_response(rc, "Failed to audit object"));
  }
}