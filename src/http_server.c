#include "../include/http_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // for strcasecmp
#include <unistd.h>

struct http_server {
  uv_loop_t *loop;
  uv_tcp_t tcp_server;
  uv_signal_t sigint;
  const char *host;
  uint16_t port;
  bool running;

  // 路由表
  struct route_entry {
    http_method_t method;
    char *pattern;
    http_handler_fn handler;
    void *ctx;
    struct route_entry *next;
  } *routes;
};

struct http_request {
  http_server_t *server;
  uv_tcp_t *client;
  uv_buf_t read_buf;
  char *raw_data;
  size_t raw_len;

  http_method_t method;
  char *path;
  char *query_string;
  struct {
    char **names;
    char **values;
    int count;
  } headers;

  uint8_t *body;
  size_t body_len;

  int parse_state;
};

struct http_response {
  http_request_t *req;
  int status_code;
  struct {
    char **names;
    char **values;
    int count;
  } headers;
  bool headers_sent;
  uv_write_t write_req;
  uv_buf_t *write_bufs;
  int write_buf_count;
  http_response_cb cb;
  void *cb_ctx;
};

// HTTP 解析器状态
#define PARSE_START 0
#define PARSE_METHOD 1
#define PARSE_PATH 2
#define PARSE_VERSION 3
#define PARSE_HEADER_NAME 4
#define PARSE_HEADER_VALUE 5
#define PARSE_BODY 6

static http_method_t parse_method(const char *str) {
  if (strcmp(str, "GET") == 0)
    return HTTP_GET;
  if (strcmp(str, "POST") == 0)
    return HTTP_POST;
  if (strcmp(str, "PUT") == 0)
    return HTTP_PUT;
  if (strcmp(str, "DELETE") == 0)
    return HTTP_DELETE;
  if (strcmp(str, "HEAD") == 0)
    return HTTP_HEAD;
  if (strcmp(str, "OPTIONS") == 0)
    return HTTP_OPTIONS;
  return HTTP_GET;
}

static void parse_http_request(http_request_t *req) {
  char *data = req->raw_data;
  size_t len = req->raw_len;

  char *line_start = data;
  char *line_end;
  int line_num = 0;

  while ((line_end = strstr(line_start, "\r\n")) && line_num < 100) {
    size_t line_len = line_end - line_start;
    char line[1024];

    if (line_len < sizeof(line)) {
      memcpy(line, line_start, line_len);
      line[line_len] = '\0';
    } else {
      line_start = line_end + 2;
      continue;
    }

    if (line_num == 0) {
      // 请求行: METHOD PATH VERSION
      char method[16], path[512], version[16];
      if (sscanf(line, "%15s %511s %15s", method, path, version) == 3) {
        req->method = parse_method(method);

        // 分离路径和查询字符串
        char *qmark = strchr(path, '?');
        if (qmark) {
          *qmark = '\0';
          req->query_string = strdup(qmark + 1);
        }
        req->path = strdup(path);
      }
    } else if (line_len > 0) {
      // 头部
      char *colon = strchr(line, ':');
      if (colon) {
        *colon = '\0';
        char *name = line;
        char *value = colon + 1;

        // 去除前导空格
        while (*value == ' ')
          value++;

        // 存储头部
        req->headers.names = realloc(req->headers.names,
                                     (req->headers.count + 1) * sizeof(char *));
        req->headers.values = realloc(
            req->headers.values, (req->headers.count + 1) * sizeof(char *));
        req->headers.names[req->headers.count] = strdup(name);
        req->headers.values[req->headers.count] = strdup(value);
        req->headers.count++;

        // 检查 Content-Length
        if (strcasecmp(name, "Content-Length") == 0) {
          size_t content_len = atoi(value);
          // 查找 body
          char *body_start = line_end + 2;
          // 跳过剩余头部
          while (body_start < data + len &&
                 strncmp(body_start, "\r\n", 2) != 0) {
            body_start = strstr(body_start, "\r\n");
            if (body_start)
              body_start += 2;
            else
              break;
          }
          if (body_start && body_start + 2 <= data + len) {
            body_start += 2;
            if (body_start + content_len <= data + len) {
              req->body = (uint8_t *)malloc(content_len);
              memcpy(req->body, body_start, content_len);
              req->body_len = content_len;
            }
          }
        }
      }
    }

    line_start = line_end + 2;
    line_num++;
  }
}

static void on_client_close(uv_handle_t *handle) { free(handle); }

static void on_write_complete(uv_write_t *write_req, int status) {
  http_response_t *res = (http_response_t *)write_req->data;
  if (res->write_bufs) {
    for (int i = 0; i < res->write_buf_count; i++) {
      if (res->write_bufs[i].base) {
        free(res->write_bufs[i].base);
      }
    }
    free(res->write_bufs);
  }

  if (res->cb) {
    res->cb(res, res->cb_ctx);
  }

  // 关闭连接
  uv_close((uv_handle_t *)res->req->client, on_client_close);

  // 清理请求 - 改名避免与 uv_write_t 的 req 成员冲突
  http_request_t *http_req = res->req;
  if (http_req->path)
    free(http_req->path);
  if (http_req->query_string)
    free(http_req->query_string);
  for (int i = 0; i < http_req->headers.count; i++) {
    free(http_req->headers.names[i]);
    free(http_req->headers.values[i]);
  }
  free(http_req->headers.names);
  free(http_req->headers.values);
  if (http_req->body)
    free(http_req->body);
  if (http_req->raw_data)
    free(http_req->raw_data);
  free(http_req);

  free(res);
}

static void alloc_buffer(uv_handle_t *handle, size_t suggested_size,
                         uv_buf_t *buf) {
  buf->base = malloc(suggested_size);
  buf->len = suggested_size;
}

static void on_client_read(uv_stream_t *stream, ssize_t nread,
                           const uv_buf_t *buf) {
  if (nread < 0) {
    if (nread != UV_EOF) {
      // 读取错误
    }
    uv_close((uv_handle_t *)stream, NULL);
    free(buf->base);
    return;
  }

  http_request_t *req = (http_request_t *)stream->data;
  if (!req) {
    req = calloc(1, sizeof(http_request_t));
    req->client = (uv_tcp_t *)stream;
    stream->data = req;
  }

  // 累积数据
  req->raw_data = realloc(req->raw_data, req->raw_len + nread + 1);
  memcpy(req->raw_data + req->raw_len, buf->base, nread);
  req->raw_len += nread;
  req->raw_data[req->raw_len] = '\0';

  // 解析请求
  parse_http_request(req);

  // 查找路由
  http_server_t *server = req->server;
  http_handler_fn handler = NULL;
  void *ctx = NULL;

  if (server && req->path) {
    for (struct route_entry *r = server->routes; r; r = r->next) {
      if (r->method == req->method) {
        // 简单通配符匹配
        if (strcmp(r->pattern, "*") == 0) {
          handler = r->handler;
          ctx = r->ctx;
          break;
        }
        if (strcmp(r->pattern, req->path) == 0) {
          handler = r->handler;
          ctx = r->ctx;
          break;
        }
        // 通配符后缀
        size_t pattern_len = strlen(r->pattern);
        if (pattern_len > 0 && r->pattern[pattern_len - 1] == '*') {
          char *base = strndup(r->pattern, pattern_len - 1);
          if (base && strncmp(base, req->path, pattern_len - 1) == 0) {
            handler = r->handler;
            ctx = r->ctx;
            free(base);
            break;
          }
          free(base);
        }
      }
    }
  }

  if (handler) {
    http_response_t *res = calloc(1, sizeof(http_response_t));
    res->req = req;
    res->status_code = 200;

    handler(req, res, ctx);

    if (!res->headers_sent) {
      // 发送默认响应
      http_response_send_string(res, "OK");
    }
  } else {
    // 404
    http_response_t *res = calloc(1, sizeof(http_response_t));
    res->req = req;
    res->status_code = 404;
    http_response_send_string(res, "Not Found");
  }

  free(buf->base);
}

static void on_new_connection(uv_stream_t *server, int status) {
  if (status < 0)
    return;

  http_server_t *http = (http_server_t *)server->data;
  uv_tcp_t *client = malloc(sizeof(uv_tcp_t));
  uv_tcp_init(http->loop, client);

  if (uv_accept(server, (uv_stream_t *)client) == 0) {
    uv_read_start((uv_stream_t *)client, alloc_buffer, on_client_read);
  } else {
    uv_close((uv_handle_t *)client, NULL);
    free(client);
  }
}

static void on_signal(uv_signal_t *handle, int signum) {
  http_server_t *server = (http_server_t *)handle->data;
  if (server && server->running) {
    server->running = false;
    uv_stop(server->loop);
  }
}

http_server_t *http_server_create(const char *host, uint16_t port) {
  http_server_t *server = calloc(1, sizeof(http_server_t));
  if (!server)
    return NULL;

  server->host = host ? strdup(host) : strdup("0.0.0.0");
  server->port = port;
  server->loop = uv_default_loop();

  return server;
}

int http_server_start(http_server_t *server) {
  uv_tcp_init(server->loop, &server->tcp_server);
  server->tcp_server.data = server;

  struct sockaddr_in addr;
  uv_ip4_addr(server->host, server->port, &addr);

  int rc = uv_tcp_bind(&server->tcp_server, (const struct sockaddr *)&addr, 0);
  if (rc)
    return rc;

  rc = uv_listen((uv_stream_t *)&server->tcp_server, 128, on_new_connection);
  if (rc)
    return rc;

  uv_signal_init(server->loop, &server->sigint);
  uv_signal_start(&server->sigint, on_signal, SIGINT);
  server->sigint.data = server;

  server->running = true;
  rc = uv_run(server->loop, UV_RUN_DEFAULT);

  return rc;
}

void http_server_stop(http_server_t *server) {
  if (server && server->running) {
    server->running = false;
    uv_stop(server->loop);
  }
}

void http_server_on(http_server_t *server, http_method_t method,
                    const char *path, http_handler_fn handler, void *ctx) {
  if (!server || !path || !handler)
    return;

  struct route_entry *route = malloc(sizeof(struct route_entry));
  route->method = method;
  route->pattern = strdup(path);
  route->handler = handler;
  route->ctx = ctx;
  route->next = server->routes;
  server->routes = route;
}

http_method_t http_request_method(http_request_t *req) {
  return req ? req->method : HTTP_GET;
}

const char *http_request_path(http_request_t *req) {
  return req ? req->path : NULL;
}

const char *http_request_header(http_request_t *req, const char *name) {
  if (!req || !name)
    return NULL;

  for (int i = 0; i < req->headers.count; i++) {
    if (strcasecmp(req->headers.names[i], name) == 0) {
      return req->headers.values[i];
    }
  }
  return NULL;
}

const uint8_t *http_request_body(http_request_t *req, size_t *len) {
  if (!req)
    return NULL;
  if (len)
    *len = req->body_len;
  return req->body;
}

void http_response_set_status(http_response_t *res, int status) {
  if (res)
    res->status_code = status;
}

void http_response_set_header(http_response_t *res, const char *name,
                              const char *value) {
  if (!res || !name || !value)
    return;

  res->headers.names =
      realloc(res->headers.names, (res->headers.count + 1) * sizeof(char *));
  res->headers.values =
      realloc(res->headers.values, (res->headers.count + 1) * sizeof(char *));
  res->headers.names[res->headers.count] = strdup(name);
  res->headers.values[res->headers.count] = strdup(value);
  res->headers.count++;
}

void http_response_send(http_response_t *res, const uint8_t *data, size_t len) {
  if (!res || res->headers_sent)
    return;

  // 构建响应头
  char status_line[128];
  const char *status_text = "OK";
  if (res->status_code == 404)
    status_text = "Not Found";
  if (res->status_code == 500)
    status_text = "Internal Server Error";

  sprintf(status_line, "HTTP/1.1 %d %s\r\n", res->status_code, status_text);

  // 如果没有设置 Content-Length，自动添加
  bool has_content_length = false;
  for (int i = 0; i < res->headers.count; i++) {
    if (strcasecmp(res->headers.names[i], "Content-Length") == 0) {
      has_content_length = true;
      break;
    }
  }
  if (!has_content_length && len > 0) {
    char content_length[32];
    sprintf(content_length, "%zu", len);
    http_response_set_header(res, "Content-Length", content_length);
  }

  // 计算总长度
  size_t header_len = strlen(status_line);
  for (int i = 0; i < res->headers.count; i++) {
    header_len +=
        strlen(res->headers.names[i]) + 2 + strlen(res->headers.values[i]) + 2;
  }
  header_len += 2; // 结尾的 \r\n

  // 构建缓冲区
  int buf_count = 2;
  if (len > 0)
    buf_count++;

  uv_buf_t *bufs = malloc(buf_count * sizeof(uv_buf_t));
  int idx = 0;

  bufs[idx].base = malloc(header_len);
  bufs[idx].len = header_len;
  char *ptr = bufs[idx].base;

  memcpy(ptr, status_line, strlen(status_line));
  ptr += strlen(status_line);

  for (int i = 0; i < res->headers.count; i++) {
    size_t name_len = strlen(res->headers.names[i]);
    size_t value_len = strlen(res->headers.values[i]);
    memcpy(ptr, res->headers.names[i], name_len);
    ptr += name_len;
    *ptr++ = ':';
    *ptr++ = ' ';
    memcpy(ptr, res->headers.values[i], value_len);
    ptr += value_len;
    *ptr++ = '\r';
    *ptr++ = '\n';
  }
  *ptr++ = '\r';
  *ptr++ = '\n';
  idx++;

  if (len > 0) {
    bufs[idx].base = (char *)data;
    bufs[idx].len = len;
    idx++;
  }

  res->write_bufs = bufs;
  res->write_buf_count = idx;
  res->write_req.data = res;

  uv_write(&res->write_req, (uv_stream_t *)res->req->client, bufs, idx,
           on_write_complete);
  res->headers_sent = true;
}

void http_response_send_string(http_response_t *res, const char *str) {
  if (!res || !str)
    return;
  http_response_set_header(res, "Content-Type", "text/plain");
  http_response_send(res, (const uint8_t *)str, strlen(str));
}

void http_response_send_json(http_response_t *res, int code, const char *json) {
  if (!res || !json)
    return;
  http_response_set_status(res, code);
  http_response_set_header(res, "Content-Type", "application/json");
  http_response_send(res, (const uint8_t *)json, strlen(json));
}

void http_server_destroy(http_server_t *server) {
  if (server) {
    if (server->host)
      free((void *)server->host);
    // 清理路由
    struct route_entry *r = server->routes;
    while (r) {
      struct route_entry *next = r->next;
      if (r->pattern)
        free(r->pattern);
      free(r);
      r = next;
    }
    free(server);
  }
}