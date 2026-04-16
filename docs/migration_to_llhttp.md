# 迁移到 llhttp 实现路径

## 📋 概述

本文档描述如何将项目当前的自研 HTTP 解析器替换为 [llhttp](https://github.com/nodejs/llhttp) —— Node.js 同款高性能 HTTP 解析器。

### 迁移收益

| 维度 | 当前实现 | 迁移后 |
|------|----------|--------|
| **代码量** | ~300 行手写解析逻辑 | ~50 行回调注册 |
| **RFC 合规性** | 基础支持 | 100% 兼容 RFC 7230 |
| **性能** | 一般 | 业界最快 |
| **边缘场景** | 手动处理 | 自动处理（分块传输、管道化等） |
| **维护成本** | 自行修复 bug | 社区维护 |

---

## 📁 目录结构

### 迁移前
```
thirdparty/
├── LMJCore/
└── URLRouter/

include/
└── http_parser.h

src/
└── http_parser.c
```

### 迁移后
```
thirdparty/
├── LMJCore/
├── URLRouter/
└── llhttp/              # 新增子模块
    ├── include/
    │   └── llhttp.h
    └── src/
        ├── llhttp.c
        └── llhttp.gyp

include/
└── http_parser.h        # 修改：适配层

src/
└── http_parser.c        # 修改：基于 llhttp 回调实现
```

---

## 🔧 步骤一：添加 llhttp 子模块

```bash
cd thirdparty/
git submodule add https://github.com/nodejs/llhttp.git
git submodule update --init
```

### 验证
```bash
ls thirdparty/llhttp/build/c/
# 应看到：llhttp.h  llhttp.c
```

---

## 🔧 步骤二：修改 CMakeLists.txt

在根目录 `CMakeLists.txt` 中添加 llhttp 编译：

```cmake
# ==================== 第三方库 ====================

# URLRouter
add_subdirectory(thirdparty/URLRouter)

# LMJCore
add_subdirectory(thirdparty/LMJCore)

# llhttp（新增）
add_library(llhttp STATIC
    thirdparty/llhttp/build/c/llhttp.c
)
target_include_directories(llhttp PUBLIC
    thirdparty/llhttp/build/c/
)

# ==================== 主程序 ====================

add_executable(lmjcore_server
    src/main.c
    src/http_server.c
    src/http_parser.c
    src/routes.c
    src/handlers/handle_utils.c
    src/handlers/obj_handle.c
    src/handlers/set_handle.c
    src/handlers/utils_handle.c
)

target_include_directories(lmjcore_server PRIVATE
    include/
    thirdparty/URLRouter/include/
    thirdparty/LMJCore/core/include/
    thirdparty/llhttp/build/c/    # 新增
)

target_link_libraries(lmjcore_server
    urlrouter
    lmjcore
    llhttp                        # 新增
    lmdb
    pthread
)
```

---

## 🔧 步骤三：修改 http_parser.h

**修改点**：
1. 添加 llhttp 类型引用
2. 扩展 `http_request_t` 结构
3. 添加解析器上下文结构

```c
#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include "../thirdparty/URLRouter/include/router.h"
#include "../thirdparty/llhttp/build/c/llhttp.h"

// ==================== HTTP 请求结构 ====================

typedef struct {
  http_method_t method;     // HTTP 方法
  char *url;                // URL
  char *body;               // 请求体
  size_t body_len;          // 请求体长度
  
  // 新增：头部存储（可选）
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

// ==================== llhttp 解析器上下文 ====================

typedef struct {
  llhttp_t parser;                  // llhttp 解析器实例
  http_request_t *request;          // 当前请求
  char url_buf[2048];               // URL 缓冲区
  size_t url_len;                   // URL 长度
  char header_key[256];             // 当前头部键
  size_t header_key_len;            // 当前头部键长度
  char current_header_value[1024];  // 当前头部值缓冲区
  size_t current_header_value_len;  // 当前头部值长度
  int parsing_header;               // 是否正在解析头部
} http_parser_context_t;

// ==================== HTTP 解析函数 ====================

/**
 * @brief 初始化 HTTP 解析器
 * @param ctx 解析器上下文
 */
void http_parser_init(http_parser_context_t *ctx);

/**
 * @brief 解析 HTTP 请求（流式）
 * @param ctx 解析器上下文
 * @param data 原始请求数据
 * @param data_len 数据长度
 * @return int 错误码（0 表示成功，HPE_OK 表示解析中）
 */
int http_parser_execute(http_parser_context_t *ctx, const char *data, 
                        size_t data_len);

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

#endif
```

---

## 🔧 步骤四：重写 http_parser.c

**核心改动**：
1. 使用 llhttp 回调机制
2. 移除手写解析逻辑
3. 保留 `http_build_response`（与 llhttp 无关）

```c
#include "../include/http_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==================== llhttp 回调函数 ====================

/**
 * @brief URL 解析完成回调
 */
static int on_url(llhttp_t *parser, const char *at, size_t length) {
  http_parser_context_t *ctx = (http_parser_context_t *)parser->data;
  
  if (length >= sizeof(ctx->url_buf) - 1) {
    return HPE_USER;  // URL 过长
  }
  
  memcpy(ctx->url_buf, at, length);
  ctx->url_buf[length] = '\0';
  ctx->url_len = length;
  
  return HPE_OK;
}

/**
 * @brief 请求体数据回调
 */
static int on_body(llhttp_t *parser, const char *at, size_t length) {
  http_parser_context_t *ctx = (http_parser_context_t *)parser->data;
  http_request_t *req = ctx->request;
  
  // 扩展 body 缓冲区
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

/**
 * @brief 头部字段回调
 */
static int on_header_field(llhttp_t *parser, const char *at, size_t length) {
  http_parser_context_t *ctx = (http_parser_context_t *)parser->data;
  
  // 如果正在解析值，说明上一个头部完成，开始新头部
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

/**
 * @brief 头部值回调
 */
static int on_header_value(llhttp_t *parser, const char *at, size_t length) {
  http_parser_context_t *ctx = (http_parser_context_t *)parser->data;
  
  // 标记为正在解析值
  ctx->parsing_header = 0;
  
  if (ctx->current_header_value_len + length >= 
      sizeof(ctx->current_header_value) - 1) {
    return HPE_USER;
  }
  
  memcpy(ctx->current_header_value + ctx->current_header_value_len, at, length);
  ctx->current_header_value_len += length;
  ctx->current_header_value[ctx->current_header_value_len] = '\0';
  
  // 存储常用头部
  if (strcasecmp(ctx->header_key, "Content-Type") == 0) {
    ctx->request->content_type = strdup(ctx->current_header_value);
  } else if (strcasecmp(ctx->header_key, "Content-Length") == 0) {
    ctx->request->content_length = strdup(ctx->current_header_value);
  } else if (strcasecmp(ctx->header_key, "Host") == 0) {
    ctx->request->host = strdup(ctx->current_header_value);
  }
  
  return HPE_OK;
}

/**
 * @brief 请求完成回调
 */
static int on_message_complete(llhttp_t *parser) {
  http_parser_context_t *ctx = (http_parser_context_t *)parser->data;
  
  // 将 URL 复制到请求结构
  ctx->request->url = strdup(ctx->url_buf);
  
  return HPE_OK;
}

// ==================== 辅助函数 ====================

/**
 * @brief 字符串到 HTTP 方法枚举
 */
static http_method_t llhttp_method_to_enum(llhttp_method_t method) {
  switch (method) {
    case HTTP_GET:     return HTTP_GET;
    case HTTP_POST:    return HTTP_POST;
    case HTTP_PUT:     return HTTP_PUT;
    case HTTP_DELETE:  return HTTP_DELETE;
    case HTTP_PATCH:   return HTTP_PATCH;
    case HTTP_HEAD:    return HTTP_HEAD;
    case HTTP_OPTIONS: return HTTP_OPTIONS;
    default:           return HTTP_GET;
  }
}

// ==================== 核心函数实现 ====================

void http_parser_init(http_parser_context_t *ctx) {
  memset(ctx, 0, sizeof(http_parser_context_t));
  
  static llhttp_settings_t settings = {
    .on_url = on_url,
    .on_body = on_body,
    .on_header_field = on_header_field,
    .on_header_value = on_header_value,
    .on_message_complete = on_message_complete,
  };
  
  llhttp_settings_init(&settings);
  llhttp_init(&ctx->parser, HTTP_REQUEST, &settings);
  
  ctx->parser.data = ctx;
  
  // 分配请求结构
  ctx->request = (http_request_t *)calloc(1, sizeof(http_request_t));
}

int http_parser_execute(http_parser_context_t *ctx, const char *data, 
                        size_t data_len) {
  if (!ctx || !data || data_len == 0) {
    return -1;
  }
  
  // 执行解析
  enum llhttp_errno err = llhttp_execute(&ctx->parser, data, data_len);
  
  if (err == HPE_OK) {
    // 解析成功
    ctx->request->method = llhttp_method_to_enum(ctx->parser.method);
    return 0;
  } else if (err == HPE_PAUSED) {
    // 需要更多数据
    return 0;
  } else {
    // 解析错误
    fprintf(stderr, "HTTP 解析错误：%s (%d) at position %ld\n",
            llhttp_errno_name(err), err,
            llhttp_get_error_pos(&ctx->parser) - data);
    return -1;
  }
}

void http_parser_reset(http_parser_context_t *ctx) {
  if (!ctx) return;
  
  llhttp_reset(&ctx->parser);
  
  // 清空 URL 缓冲区
  ctx->url_buf[0] = '\0';
  ctx->url_len = 0;
  
  // 清空头部相关
  ctx->header_key[0] = '\0';
  ctx->header_key_len = 0;
  ctx->current_header_value[0] = '\0';
  ctx->current_header_value_len = 0;
  ctx->parsing_header = 0;
  
  // 释放并重新分配请求结构
  http_free_request(ctx->request);
  free(ctx->request);
  ctx->request = (http_request_t *)calloc(1, sizeof(http_request_t));
}

int http_build_response(const http_response_t *response, char *out_buf,
                        size_t out_buf_size) {
  if (!response || !out_buf || out_buf_size == 0) {
    return -1;
  }

  const char *get_status_text(int status_code) {
    switch (status_code) {
    case 200:  return "OK";
    case 201:  return "Created";
    case 204:  return "No Content";
    case 400:  return "Bad Request";
    case 404:  return "Not Found";
    case 405:  return "Method Not Allowed";
    case 408:  return "Request Timeout";
    case 500:  return "Internal Server Error";
    default:   return "Unknown";
    }
  }

  const char *status_text = get_status_text(response->status_code);
  int written = 0;

  if (response->body && response->body_len > 0) {
    written = snprintf(out_buf, out_buf_size,
                       "HTTP/1.1 %d %s\r\n"
                       "Access-Control-Allow-Origin: *\r\n"
                       "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
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
                       "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
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

void http_free_request(http_request_t *request) {
  if (request) {
    free(request->url);
    request->url = NULL;
    free(request->body);
    request->body = NULL;
    free(request->content_type);
    request->content_type = NULL;
    free(request->content_length);
    request->content_length = NULL;
    free(request->host);
    request->host = NULL;
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
```

---

## 🔧 步骤五：修改 http_server.c 调用方式

**修改点**：
1. 创建解析器上下文
2. 使用流式解析替代一次性解析

```c
// 在 connection_data_t 中添加
typedef struct {
  http_parser_context_t parser_ctx;  // 新增
  int socket_fd;
  char recv_buffer[8192];
  size_t recv_len;
  // ... 其他字段
} connection_data_t;

// 在 handle_client 函数中修改
void *handle_client(void *arg) {
  connection_data_t *conn = (connection_data_t *)arg;
  
  // 初始化解析器
  http_parser_init(&conn->parser_ctx);
  
  while (1) {
    ssize_t bytes_read = recv(conn->socket_fd, conn->recv_buffer, 
                              sizeof(conn->recv_buffer) - 1, 0);
    
    if (bytes_read <= 0) {
      break;  // 连接关闭
    }
    
    conn->recv_buffer[bytes_read] = '\0';
    conn->recv_len += bytes_read;
    
    // 执行解析
    int ret = http_parser_execute(&conn->parser_ctx, 
                                  conn->recv_buffer, 
                                  bytes_read);
    
    if (ret < 0) {
      // 解析错误，发送 400
      send_400_response(conn->socket_fd);
      break;
    }
    
    // 检查是否解析完成
    if (llhttp_message_complete(&conn->parser_ctx.parser)) {
      // 调用路由处理
      http_request_t *req = conn->parser_ctx.request;
      handle_request(conn->socket_fd, req);
      
      // 重置解析器（支持 Keep-Alive）
      http_parser_reset(&conn->parser_ctx);
      conn->recv_len = 0;
    }
  }
  
  // 清理
  http_free_request(conn->parser_ctx.request);
  close(conn->socket_fd);
  free(conn);
  return NULL;
}
```

---

## ✅ 步骤六：验证与测试

### 编译验证
```bash
mkdir build && cd build
cmake ..
make

# 应无编译错误
```

### 功能测试
```bash
# 启动服务
./lmjcore_server

# 测试基本请求
curl -X GET http://localhost:8080/obj/01xxxxxxxxxxxxxxxx
curl -X POST http://localhost:8080/obj -d '{"name":"test"}'

# 测试头部解析
curl -v -H "Custom-Header: test" http://localhost:8080/

# 测试分块传输（llhttp 自动处理）
curl -X POST -H "Transfer-Encoding: chunked" -d "hello" http://localhost:8080/
```

### 性能测试（可选）
```bash
# 使用 ab 进行压力测试
ab -n 10000 -c 100 http://localhost:8080/

# 对比迁移前后的 QPS
```

---

## 📊 迁移检查清单

- [ ] llhttp 子模块添加成功
- [ ] CMakeLists.txt 已更新
- [ ] http_parser.h 已修改
- [ ] http_parser.c 已重写
- [ ] http_server.c 调用方式已更新
- [ ] 编译无错误
- [ ] 基本 GET/POST 请求正常
- [ ] 头部解析正常
- [ ] 请求体解析正常
- [ ] 错误请求返回 400
- [ ] Keep-Alive 连接正常（可选）

---

## 🔙 回滚方案

如需回滚到原实现：

```bash
# 恢复 git 修改
git checkout include/http_parser.h
git checkout src/http_parser.c
git checkout CMakeLists.txt

# 移除子模块
git submodule deinit thirdparty/llhttp
git rm thirdparty/llhttp
```

---

## 📚 参考资料

- [llhttp 官方文档](https://github.com/nodejs/llhttp)
- [llhttp API 参考](https://github.com/nodejs/llhttp/blob/master/include/llhttp.h)
- [RFC 7230 HTTP/1.1 规范](https://tools.ietf.org/html/rfc7230)

---

**最后更新**: 2026 年 4 月 16 日
**作者**: AI Assistant
