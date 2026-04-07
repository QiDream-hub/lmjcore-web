// src/http_server.h - HTTP 服务器内部接口
#ifndef LMJCORE_HTTP_SERVER_H
#define LMJCORE_HTTP_SERVER_H

#include "../include/lmjcore_server.h"
#include "../thirdparty/URLRouer/router.h"
#include <stdbool.h>

// ==================== HTTP 请求结构 ====================

typedef struct {
  http_method_t method; // HTTP 方法
  char *url;            // URL
  char *body;           // 请求体
  size_t body_len;      // 请求体长度
} http_request_t;

// ==================== HTTP 响应结构 ====================

typedef struct {
  int status_code; // HTTP 状态码
  char *body;      // JSON 响应体
  size_t body_len; // 响应体长度
} http_response_t;

// ==================== 服务器内部结构 ====================

typedef struct lmjcore_server {
  int socket_fd;          // 监听 socket
  server_config_t config; // 服务器配置
  router_t *router;       // 路由实例
  lmjcore_env *env;       // LMJCore 环境
  volatile bool running;  // 运行状态标志
  time_t start_time;      // 启动时间
} lmjcore_server_t;

// ==================== HTTP 解析函数 ====================

/**
 * @brief 解析 HTTP 请求
 *
 * @param data 原始请求数据
 * @param data_len 数据长度
 * @param request_out 输出解析后的请求结构
 * @return int 错误码（0 表示成功）
 */
int http_parse_request(const char *data, size_t data_len,
                       http_request_t *request_out);

/**
 * @brief 构建 HTTP 响应
 *
 * @param response 响应结构
 * @param out_buf 输出缓冲区
 * @param out_buf_size 缓冲区大小
 * @return int 构建的响应长度，负数表示错误
 */
int http_build_response(const http_response_t *response, char *out_buf,
                        size_t out_buf_size);

/**
 * @brief 释放 HTTP 请求资源
 *
 * @param request 请求结构
 */
void http_free_request(http_request_t *request);

/**
 * @brief 释放 HTTP 响应资源
 *
 * @param response 响应结构
 */
void http_free_response(http_response_t *response);

// ==================== 请求处理函数 ====================

/**
 * @brief 处理单个 HTTP 请求
 *
 * @param server 服务器实例
 * @param request 请求结构
 * @param response 输出响应结构
 * @return int 错误码
 */
int lmjcore_handle_request(lmjcore_server_t *server,
                           const http_request_t *request,
                           http_response_t *response);

#endif // LMJCORE_HTTP_SERVER_H