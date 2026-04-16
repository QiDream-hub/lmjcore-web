#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include "router.h"

// ==================== HTTP 请求结构 ====================

typedef struct {
  http_method_t method;     // HTTP 方法
  char *url;                // URL
  char *body;               // 请求体
  size_t body_len;          // 请求体长度

  // 头部存储
  char *content_type;       // Content-Type
  char *content_length;     // Content-Length
  char *host;               // Host
} http_request_t;

// ==================== HTTP 响应结构 ====================

typedef struct {
  int status_code;          // HTTP 状态码
  char *body;               // JSON 响应体
  size_t body_len;          // 响应体长度
} http_response_t;

// ==================== llhttp 解析器上下文（不透明指针） ====================

typedef struct http_parser_context http_parser_context_t;

// ==================== HTTP 解析函数 ====================

/**
 * @brief 创建 HTTP 解析器
 * @param ctx 解析器上下文指针的指针
 */
void http_parser_create(http_parser_context_t **ctx);

/**
 * @brief 解析 HTTP 请求（流式）
 * @param ctx 解析器上下文
 * @param data 原始请求数据
 * @param data_len 数据长度
 * @return int 错误码（0 表示成功）
 */
int http_parser_execute(http_parser_context_t *ctx, const char *data,
                        size_t data_len);

/**
 * @brief 获取解析后的请求
 * @param ctx 解析器上下文
 * @return http_request_t* 请求结构指针（调用者负责释放）
 */
http_request_t *http_parser_get_request(http_parser_context_t *ctx);

/**
 * @brief 构建 HTTP 响应
 * @param response 响应结构
 * @param out_buf 输出缓冲区
 * @param out_buf_size 缓冲区大小
 * @return int 构建的响应长度，负数表示错误
 */
int http_build_response(const http_response_t *response, char *out_buf,
                        size_t out_buf_size);

/**
 * @brief 释放 HTTP 请求资源
 * @param request 请求结构
 */
void http_free_request(http_request_t *request);

/**
 * @brief 释放 HTTP 响应资源
 * @param response 响应结构
 */
void http_free_response(http_response_t *response);

/**
 * @brief 重置解析器状态（用于连接复用）
 * @param ctx 解析器上下文
 */
void http_parser_reset(http_parser_context_t *ctx);

/**
 * @brief 销毁解析器
 * @param ctx 解析器上下文
 */
void http_parser_destroy(http_parser_context_t *ctx);

#endif
