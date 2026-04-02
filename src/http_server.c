#include "../include/http_server.h"
#include "../thirdparty/URLRouer/router.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 辅助函数：查找头部结束位置（空行）
static const char *find_headers_end(const char *data, size_t data_len) {
  for (size_t i = 0; i + 3 < data_len; i++) {
    if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' &&
        data[i + 3] == '\n') {
      return data + i + 4;
    }
    // 也支持仅 \n\n 的情况（非标准但宽容处理）
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

/**
 * @brief 在数据缓冲区中查找指定的键（通用版本）
 *
 * @param data          数据缓冲区
 * @param data_len      数据长度
 * @param start_offset  起始查找偏移量
 * @param target_key    目标键
 * @param target_key_len 目标键长度
 * @return int       找到的偏移量，失败返回-1
 *
 * @note 查找是字节精确匹配（memcmp），区分大小写
 * @note out_key指向data内部，调用者需要确保data的生命周期足够长
 * @note 如果target_key_len > data_len，会立即返回失败
 */
static int find_key_offset(const char *data, int data_len, int start_offset,
                           const char *target_key, int target_key_len) {
  // 参数检查
  if (!data || !target_key || target_key_len <= 0 || data_len <= 0 ||
      start_offset < 0 || start_offset >= data_len) {
    return -1;
  }

  if (target_key_len > data_len) {
    return -1;
  }

  for (int i = start_offset; i <= data_len - target_key_len; i++) {
    if (memcmp(data + i, target_key, target_key_len) == 0) {
      return i;
    }
  }

  return -1;
}

// 复用通用定义查找空格
static int find_space_offset(const char *data, size_t data_len,
                             size_t start_offset) {
  const char target_key = ' ';
  return find_key_offset(data, data_len, start_offset, &target_key, 1);
}

// 主解析函数
int http_parse_request(const char *data, size_t data_len,
                       http_request_t *request_out) {
  if (!data || data_len == 0 || !request_out)
    return -1;

  // 初始化输出结构体
  memset(request_out, 0, sizeof(http_request_t));
  request_out->url = NULL;
  request_out->body = NULL;
  request_out->body_len = 0;

  char *buffer = (char *)malloc(data_len + 1);
  if (!buffer)
    return -1;
  memcpy(buffer, data, data_len);
  buffer[data_len] = '\0';

  // 解析方法
  int space1 = find_space_offset(buffer, data_len, 0);
  if (space1 == -1) {
    free(buffer);
    return -1;
  }
  buffer[space1] = '\0';
  char *method_str = buffer;

  // 解析URL
  int space2 = find_space_offset(buffer, data_len, space1 + 1);
  if (space2 == -1) {
    free(buffer);
    return -1;
  }
  buffer[space2] = '\0';
  char *url_str = buffer + space1 + 1;

  // 解析版本
  int space3 = find_space_offset(buffer, data_len, space2 + 1);
  if (space3 != -1) {
    buffer[space3] = '\0';
  }
  char *version_str = buffer + space2 + 1;

  if (!method_str || !url_str || !version_str) {
    free(buffer);
    return -1;
  }

  // 转换方法
  if (str_to_http_method(method_str, &request_out->method) != 0) {
    free(buffer);
    return -1;
  }

  // 复制 URL
  request_out->url = strdup(url_str);
  if (!request_out->url) {
    free(buffer);
    return -1;
  }

  // 查找头部结束位置（空行）
  const char *body_start = find_headers_end(data, data_len);

  if (body_start) {
    // 计算请求体长度
    size_t body_size = data_len - (body_start - data);
    if (body_size > 0) {
      request_out->body = (char *)malloc(body_size);
      if (!request_out->body) {
        free(request_out->url);
        free(buffer);
        return -1;
      }
      memcpy(request_out->body, body_start, body_size);
      request_out->body_len = body_size;
    }
  }

  free(buffer);
  return 0;
}

void http_free_request(http_request_t *request) {
  if (request) {
    free(request->url);
    free(request->body);
    request->url = NULL;
    request->body = NULL;
    request->body_len = 0;
  }
} // 构建 HTTP 响应
int http_build_response(const http_response_t *response, char *out_buf,
                        size_t out_buf_size) {
  // 参数校验
  if (!response || !out_buf || out_buf_size == 0) {
    return -1;
  }

  // 对于需要响应体的状态码，body 不能为空
  if (response->status_code >= 200 && response->status_code < 300) {
    if (!response->body && response->status_code != 204) {
      return -3;
    }
  }

  const char *status_text = get_status_text(response->status_code);
  int written = 0;

  // 构建响应行和基本响应头
  if (response->body && response->body_len > 0) {
    // 有响应体的情况
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
    // 无响应体的情况（如 204 No Content）
    written = snprintf(out_buf, out_buf_size,
                       "HTTP/1.1 %d %s\r\n"
                       "Content-Length: 0\r\n"
                       "Connection: close\r\n"
                       "\r\n",
                       response->status_code, status_text);
  }

  // 检查缓冲区是否足够
  if (written < 0 || (size_t)written >= out_buf_size) {
    return -2;
  }

  return written;
}

//  释放 HTTP 响应资源
void http_free_response(http_response_t *response) {
  if (response) {
    free(response->body);
    response->body = NULL;
    response->body_len = 0;
    response->status_code = 0;
  }
}