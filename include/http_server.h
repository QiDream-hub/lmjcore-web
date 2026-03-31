#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <uv.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct http_server http_server_t;
typedef struct http_request http_request_t;
typedef struct http_response http_response_t;

// HTTP 方法
typedef enum {
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_HEAD,
    HTTP_OPTIONS
} http_method_t;

// 请求处理回调
typedef void (*http_handler_fn)(http_request_t* req, http_response_t* res, void* ctx);

// 响应回调
typedef void (*http_response_cb)(http_response_t* res, void* ctx);

// 服务器创建
http_server_t* http_server_create(const char* host, uint16_t port);

// 服务器启动/停止
int http_server_start(http_server_t* server);
void http_server_stop(http_server_t* server);

// 路由注册
void http_server_on(http_server_t* server, http_method_t method, 
                    const char* path, http_handler_fn handler, void* ctx);

// 请求接口
http_method_t http_request_method(http_request_t* req);
const char* http_request_path(http_request_t* req);
const char* http_request_header(http_request_t* req, const char* name);
const uint8_t* http_request_body(http_request_t* req, size_t* len);

// 响应接口
void http_response_set_status(http_response_t* res, int status);
void http_response_set_header(http_response_t* res, const char* name, const char* value);
void http_response_send(http_response_t* res, const uint8_t* data, size_t len);
void http_response_send_string(http_response_t* res, const char* str);
void http_response_send_json(http_response_t* res, int code, const char* json);

// 销毁
void http_server_destroy(http_server_t* server);

#endif // HTTP_SERVER_H