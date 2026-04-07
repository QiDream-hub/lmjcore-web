#include "../include/http_server.h"
#include "../thirdparty/URLRouer/router.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ==================== 辅助函数实现 ====================

// 辅助函数：查找头部结束位置（空行）
static const char *find_headers_end(const char *data, size_t data_len) {
  for (size_t i = 0; i + 3 < data_len; i++) {
    // 标准 HTTP 换行符 \r\n\r\n
    if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' &&
        data[i + 3] == '\n') {
      return data + i + 4;
    }
    // 宽容处理：仅 \n\n
    if (data[i] == '\n' && data[i + 1] == '\n') {
      return data + i + 2;
    }
  }
  return NULL;
}

// 辅助函数：字符串到 HTTP 方法枚举
static int str_to_http_method(const char *method_str, http_method_t *method) {
  if (strcmp(method_str, "GET") == 0)
    *method = HTTP_GET;
  else if (strcmp(method_str, "POST") == 0)
    *method = HTTP_POST;
  else if (strcmp(method_str, "PUT") == 0)
    *method = HTTP_PUT;
  else if (strcmp(method_str, "DELETE") == 0)
    *method = HTTP_DELETE;
  else if (strcmp(method_str, "PATCH") == 0)
    *method = HTTP_PATCH;
  else if (strcmp(method_str, "HEAD") == 0)
    *method = HTTP_HEAD;
  else if (strcmp(method_str, "OPTIONS") == 0)
    *method = HTTP_OPTIONS;
  else
    return -1; // 不支持的方法
  return 0;
}

/**
 * @brief 获取 HTTP 状态码对应的描述文本
 */
static const char *get_status_text(int status_code) {
  switch (status_code) {
  case 200:
    return "OK";
  case 201:
    return "Created";
  case 204:
    return "No Content";
  case 400:
    return "Bad Request";
  case 404:
    return "Not Found";
  case 405:
    return "Method Not Allowed";
  case 408:
    return "Request Timeout";
  case 500:
    return "Internal Server Error";
  default:
    return "Unknown";
  }
}

// 辅助函数：安全地复制字符串
static char *safe_strdup(const char *s) {
  if (!s)
    return NULL;
  return strdup(s);
}

/**
 * 在数据缓冲区中查找指定的字节序列
 */
static char *find_key_offset(const char *data, size_t data_len,
                             size_t start_offset, const char *target_key,
                             size_t target_key_len) {
  if (!data || !target_key || data_len == 0 || target_key_len == 0) {
    return NULL;
  }

  if (start_offset >= data_len || target_key_len > data_len - start_offset) {
    return NULL;
  }

  for (size_t i = start_offset; i <= data_len - target_key_len; i++) {
    if (data[i] == target_key[0]) {
      if (memcmp(data + i, target_key, target_key_len) == 0) {
        return (char *)(data + i);
      }
    }
  }
  return NULL;
}

// 复用通用定义查找空格
static char *find_space_offset(const char *data, size_t data_len,
                               size_t start_offset) {
  const char target_key = ' ';
  return find_key_offset(data, data_len, start_offset, &target_key, 1);
}

// ==================== 核心解析与构建函数 ====================

int http_parse_request(const char *data, size_t data_len,
                       http_request_t *request_out) {
  if (!data || data_len == 0 || !request_out)
    return -1;

  memset(request_out, 0, sizeof(http_request_t));

  // 分配临时缓冲区
  char *buffer = (char *)malloc(data_len + 1);
  if (!buffer)
    return -1;

  memcpy(buffer, data, data_len);
  buffer[data_len] = '\0';

  // --- 解析首行 ---
  char *space1 = find_space_offset(buffer, data_len, 0);
  if (!space1)
    goto cleanup_error;

  *space1 = '\0';
  char *method_str = buffer;

  char *space2 = find_space_offset(buffer, data_len, space1 - buffer + 1);
  if (!space2)
    goto cleanup_error;

  *space2 = '\0';
  char *url_str = space1 + 1;

  // 查找行尾
  char *line_end =
      find_key_offset(buffer, data_len, space2 - buffer + 1, "\r", 1);
  if (!line_end) {
    line_end = find_key_offset(buffer, data_len, space2 - buffer + 1, "\n", 1);
  }

  if (line_end) {
    *line_end = '\0';
  }
  char *version_str = space2 + 1;

  // --- 数据验证与转换 ---
  if (!method_str || !url_str || !version_str) {
    goto cleanup_error;
  }

  if (str_to_http_method(method_str, &request_out->method) != 0) {
    goto cleanup_error;
  }

  request_out->url = safe_strdup(url_str);
  if (!request_out->url)
    goto cleanup_error;

  // --- 解析请求体 ---
  const char *body_start = find_headers_end(data, data_len);

  if (body_start) {
    size_t body_size = data_len - (body_start - data);
    if (body_size > 0) {
      request_out->body = (char *)malloc(body_size);
      if (!request_out->body) {
        goto cleanup_error;
      }
      memcpy(request_out->body, body_start, body_size);
      request_out->body_len = body_size;
    }
  }

  free(buffer);
  return 0;

cleanup_error:
  if (request_out->url)
    free(request_out->url);
  if (request_out->body)
    free(request_out->body);
  free(buffer);
  memset(request_out, 0, sizeof(http_request_t));
  return -1;
}

int http_build_response(const http_response_t *response, char *out_buf,
                        size_t out_buf_size) {
  if (!response || !out_buf || out_buf_size == 0) {
    return -1;
  }

  // 检查是否需要 Body
  if (response->status_code >= 200 && response->status_code < 300) {
    if (!response->body && response->status_code != 204) {
      return -3;
    }
  }

  const char *status_text = get_status_text(response->status_code);
  int written = 0;

  if (response->body && response->body_len > 0) {
    written = snprintf(out_buf, out_buf_size,
                       "HTTP/1.1 %d %s\r\n"
                       "Content-Type: application/json\r\n"
                       "Content-Length: %zu\r\n"
                       "Connection: close\r\n"
                       "\r\n"
                       "%.*s",
                       response->status_code, status_text, response->body_len,
                       (int)response->body_len, response->body);
  } else {
    written = snprintf(out_buf, out_buf_size,
                       "HTTP/1.1 %d %s\r\n"
                       "Content-Length: 0\r\n"
                       "Connection: close\r\n"
                       "\r\n",
                       response->status_code, status_text);
  }

  if (written < 0 || (size_t)written >= out_buf_size) {
    return -2;
  }

  return written;
}

// ==================== 资源释放函数 ====================

void http_free_request(http_request_t *request) {
  if (request) {
    if (request->url) {
      free(request->url);
      request->url = NULL;
    }
    if (request->body) {
      free(request->body);
      request->body = NULL;
    }
    request->body_len = 0;
    request->method = 0;
  }
}

void http_free_response(http_response_t *response) {
  if (response) {
    free(response->body);
    response->body = NULL;
    response->body_len = 0;
    response->status_code = 0;
  }
}

int handle_request(http_server_t *server, const http_request_t *request,
                   http_response_t *response) {

  route_params_t params;
  route_callback_t callback =
      router_match(server->router, request->method, request->url, &params);

  handle_params param = {.params = &params,
                         .env = server->env,
                         .body = request->body,
                         .body_len = request->body_len};
  return callback(&param, response);
}