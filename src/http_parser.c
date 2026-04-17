/**
 * @file http_parser.c - 基于 llhttp 的 HTTP 解析实现
 *
 * 注意：本文件需要直接包含 llhttp.h，因此不能包含 router.h（会导致枚举冲突）。
 * 我们使用内部的类型定义，并在返回请求时进行转换。
 */

#include <llhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 本地定义 http_method_t 以避免与 llhttp 冲突
// 这些值必须与 router.h 中的 http_method_t 一致
typedef enum {
  LOCAL_HTTP_GET = 0,
  LOCAL_HTTP_POST = 1,
  LOCAL_HTTP_PUT = 2,
  LOCAL_HTTP_DELETE = 3,
  LOCAL_HTTP_PATCH = 4,
  LOCAL_HTTP_HEAD = 5,
  LOCAL_HTTP_OPTIONS = 6
} local_http_method_t;

// 本地请求结构（内部使用）
typedef struct {
  local_http_method_t method;
  char *url;
  char *body;
  size_t body_len;
  char *content_type;
  char *content_length;
  char *host;
} local_http_request_t;

// 解析器上下文结构
typedef struct http_parser_context {
  llhttp_t parser;
  local_http_request_t *local_request;
  char url_buf[2048];
  size_t url_len;
  char header_key[256];
  size_t header_key_len;
  char current_header_value[1024];
  size_t current_header_value_len;
  int parsing_header;
} http_parser_context_t;

// 公开响应结构（与 http_parser.h 一致）
typedef struct {
  int status_code;
  char *body;
  size_t body_len;
} http_response_t;

// 公开请求结构（与 http_parser.h 一致）
typedef struct {
  int method;
  char *url;
  char *body;
  size_t body_len;
  char *content_type;
  char *content_length;
  char *host;
} http_request_t;

// ==================== llhttp 回调函数 ====================

static int on_url(llhttp_t *parser, const char *at, size_t length) {
  http_parser_context_t *ctx = (http_parser_context_t *)parser->data;

  if (length >= sizeof(ctx->url_buf) - 1) {
    return HPE_USER;
  }

  memcpy(ctx->url_buf, at, length);
  ctx->url_buf[length] = '\0';
  ctx->url_len = length;

  return HPE_OK;
}

static int on_body(llhttp_t *parser, const char *at, size_t length) {
  http_parser_context_t *ctx = (http_parser_context_t *)parser->data;
  local_http_request_t *req = ctx->local_request;

  char *new_body = (char *)realloc(req->body, req->body_len + length + 1);
  if (!new_body) {
    return HPE_USER;
  }

  req->body = new_body;
  memcpy(req->body + req->body_len, at, length);
  req->body_len += length;
  req->body[req->body_len] = '\0';

  return HPE_OK;
}

static int on_header_field(llhttp_t *parser, const char *at, size_t length) {
  http_parser_context_t *ctx = (http_parser_context_t *)parser->data;

  if (!ctx->parsing_header) {
    ctx->header_key_len = 0;
    ctx->current_header_value_len = 0;
    ctx->parsing_header = 1;
  }

  if (ctx->header_key_len + length >= sizeof(ctx->header_key) - 1) {
    return HPE_USER;
  }

  memcpy(ctx->header_key + ctx->header_key_len, at, length);
  ctx->header_key_len += length;
  ctx->header_key[ctx->header_key_len] = '\0';

  return HPE_OK;
}

static int on_header_value(llhttp_t *parser, const char *at, size_t length) {
  http_parser_context_t *ctx = (http_parser_context_t *)parser->data;

  ctx->parsing_header = 0;

  if (ctx->current_header_value_len + length >=
      sizeof(ctx->current_header_value) - 1) {
    return HPE_USER;
  }

  memcpy(ctx->current_header_value + ctx->current_header_value_len, at, length);
  ctx->current_header_value_len += length;
  ctx->current_header_value[ctx->current_header_value_len] = '\0';

  if (strcasecmp(ctx->header_key, "Content-Type") == 0) {
    ctx->local_request->content_type = strdup(ctx->current_header_value);
  } else if (strcasecmp(ctx->header_key, "Content-Length") == 0) {
    ctx->local_request->content_length = strdup(ctx->current_header_value);
  } else if (strcasecmp(ctx->header_key, "Host") == 0) {
    ctx->local_request->host = strdup(ctx->current_header_value);
  }

  return HPE_OK;
}

static int on_message_complete(llhttp_t *parser) {
  http_parser_context_t *ctx = (http_parser_context_t *)parser->data;

  ctx->local_request->url = strdup(ctx->url_buf);

  return HPE_OK;
}

// ==================== 辅助函数 ====================

/**
 * @brief 将 llhttp 方法枚举转换为本地方法枚举
 *
 * llhttp 枚举值：
 *   HTTP_DELETE = 0, HTTP_GET = 1, HTTP_HEAD = 2, HTTP_POST = 3,
 *   HTTP_PUT = 4, HTTP_OPTIONS = 6, HTTP_PATCH = 28
 *
 * 本地枚举值（与 router.h 一致）：
 *   HTTP_GET = 0, HTTP_POST = 1, HTTP_PUT = 2, HTTP_DELETE = 3,
 *   HTTP_PATCH = 4, HTTP_HEAD = 5, HTTP_OPTIONS = 6
 */
static local_http_method_t llhttp_method_to_local(llhttp_method_t method) {
  switch (method) {
  case 1:  return LOCAL_HTTP_GET;       // llhttp HTTP_GET
  case 3:  return LOCAL_HTTP_POST;      // llhttp HTTP_POST
  case 4:  return LOCAL_HTTP_PUT;       // llhttp HTTP_PUT
  case 0:  return LOCAL_HTTP_DELETE;    // llhttp HTTP_DELETE
  case 28: return LOCAL_HTTP_PATCH;     // llhttp HTTP_PATCH
  case 2:  return LOCAL_HTTP_HEAD;      // llhttp HTTP_HEAD
  case 6:  return LOCAL_HTTP_OPTIONS;   // llhttp HTTP_OPTIONS
  default: return LOCAL_HTTP_GET;
  }
}

// ==================== 公开 API 实现 ====================

void http_parser_create(http_parser_context_t **ctx_out) {
  if (!ctx_out) return;

  http_parser_context_t *ctx =
      (http_parser_context_t *)calloc(1, sizeof(http_parser_context_t));
  if (!ctx) {
    *ctx_out = NULL;
    return;
  }

  llhttp_settings_t *settings =
      (llhttp_settings_t *)malloc(sizeof(llhttp_settings_t));
  if (!settings) {
    free(ctx);
    *ctx_out = NULL;
    return;
  }

  llhttp_settings_init(settings);
  settings->on_url = on_url;
  settings->on_body = on_body;
  settings->on_header_field = on_header_field;
  settings->on_header_value = on_header_value;
  settings->on_message_complete = on_message_complete;

  llhttp_init(&ctx->parser, HTTP_REQUEST, settings);
  ctx->parser.data = ctx;
  ctx->local_request =
      (local_http_request_t *)calloc(1, sizeof(local_http_request_t));

  *ctx_out = ctx;
}

int http_parser_execute(http_parser_context_t *ctx, const char *data,
                        size_t data_len) {
  if (!ctx || !data || data_len == 0) return -1;

  enum llhttp_errno err = llhttp_execute(&ctx->parser, data, data_len);

  if (err == HPE_OK) {
    ctx->local_request->method = llhttp_method_to_local(ctx->parser.method);
    return 0;
  } else if (err == HPE_PAUSED) {
    return 0;
  } else {
    fprintf(stderr, "HTTP 解析错误：%s (%d) at position %ld\n",
            llhttp_errno_name(err), err,
            llhttp_get_error_pos(&ctx->parser) - data);
    return -1;
  }
}

http_request_t *http_parser_get_request(http_parser_context_t *ctx) {
  if (!ctx || !ctx->local_request) return NULL;

  local_http_request_t *local_req = ctx->local_request;
  http_request_t *pub_req =
      (http_request_t *)calloc(1, sizeof(http_request_t));
  if (!pub_req) return NULL;

  pub_req->method = (int)local_req->method;
  pub_req->url = local_req->url ? strdup(local_req->url) : NULL;
  pub_req->body = local_req->body ? strdup(local_req->body) : NULL;
  pub_req->body_len = local_req->body_len;
  pub_req->content_type =
      local_req->content_type ? strdup(local_req->content_type) : NULL;
  pub_req->content_length =
      local_req->content_length ? strdup(local_req->content_length) : NULL;
  pub_req->host = local_req->host ? strdup(local_req->host) : NULL;

  return pub_req;
}

void http_parser_reset(http_parser_context_t *ctx) {
  if (!ctx) return;

  llhttp_reset(&ctx->parser);
  ctx->url_buf[0] = '\0';
  ctx->url_len = 0;
  ctx->header_key[0] = '\0';
  ctx->header_key_len = 0;
  ctx->current_header_value[0] = '\0';
  ctx->current_header_value_len = 0;
  ctx->parsing_header = 0;

  if (ctx->local_request) {
    free(ctx->local_request->url);
    free(ctx->local_request->body);
    free(ctx->local_request->content_type);
    free(ctx->local_request->content_length);
    free(ctx->local_request->host);
    memset(ctx->local_request, 0, sizeof(local_http_request_t));
  }
}

void http_parser_destroy(http_parser_context_t *ctx) {
  if (!ctx) return;

  if (ctx->parser.settings) {
    free((void *)ctx->parser.settings);
  }

  if (ctx->local_request) {
    free(ctx->local_request->url);
    free(ctx->local_request->body);
    free(ctx->local_request->content_type);
    free(ctx->local_request->content_length);
    free(ctx->local_request->host);
    free(ctx->local_request);
  }
  free(ctx);
}

static const char *get_status_text(int status_code) {
  switch (status_code) {
  case 200: return "OK";
  case 201: return "Created";
  case 204: return "No Content";
  case 400: return "Bad Request";
  case 404: return "Not Found";
  case 405: return "Method Not Allowed";
  case 408: return "Request Timeout";
  case 500: return "Internal Server Error";
  default:  return "Unknown";
  }
}

int http_build_response(const http_response_t *response, char *out_buf,
                        size_t out_buf_size) {
  if (!response || !out_buf || out_buf_size == 0) return -1;

  const char *status_text = get_status_text(response->status_code);
  int written = 0;

  if (response->body && response->body_len > 0) {
    written = snprintf(out_buf, out_buf_size,
                       "HTTP/1.1 %d %s\r\n"
                       "Access-Control-Allow-Origin: *\r\n"
                       "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
                       "Access-Control-Allow-Headers: Content-Type\r\n"
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
                       "Access-Control-Allow-Origin: *\r\n"
                       "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
                       "Access-Control-Allow-Headers: Content-Type\r\n"
                       "Content-Length: 0\r\n"
                       "Connection: close\r\n"
                       "\r\n",
                       response->status_code, status_text);
  }

  if (written < 0 || (size_t)written >= out_buf_size) return -2;
  return written;
}

void http_free_request(http_request_t *request) {
  if (!request) return;

  free(request->url);
  free(request->body);
  free(request->content_type);
  free(request->content_length);
  free(request->host);
  free(request);
}

void http_free_response(http_response_t *response) {
  if (!response) return;

  free(response->body);
  response->body = NULL;
  response->body_len = 0;
  response->status_code = 0;
}
