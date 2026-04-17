#include "http_server.h"
#include "lmjcore.h"
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#define SOCKET_ERROR_VAL SOCKET_ERROR
#define CLOSE_SOCKET closesocket
#define THREAD_RETURN_TYPE DWORD WINAPI
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
typedef int socket_t;
#define SOCKET_ERROR_VAL -1
#define CLOSE_SOCKET close
#define THREAD_RETURN_TYPE void *
#endif

// ==================== 线程参数结构 ====================

typedef struct {
  http_server_t *server;
  socket_t client_fd;
  struct sockaddr_in client_addr;
} thread_args_t;

// ==================== 内部辅助函数 ====================

/**
 * @brief 获取当前时间戳字符串（用于日志）
 */
static void get_timestamp(char *buffer, size_t size) {
  time_t now = time(NULL);
  struct tm *tm_info = localtime(&now);
  strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

/**
 * @brief 日志输出
 */
static void log_request(const char *method, const char *url, int status_code,
                        const char *client_ip) {
  char timestamp[64];
  get_timestamp(timestamp, sizeof(timestamp));
  printf("[%s] %s - %s %s -> %d\n", timestamp, client_ip, method, url,
         status_code);
}

/**
 * @brief 设置套接字选项
 */
static int set_socket_options(socket_t fd) {
  int opt = 1;

  // 设置地址重用
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
#ifdef _WIN32
                 (const char *)&opt, sizeof(opt)) < 0) {
#else
                 &opt, sizeof(opt)) < 0) {
#endif
    perror("setsockopt SO_REUSEADDR");
    return -1;
  }

  // 设置 TCP_NODELAY（禁用 Nagle 算法，提高小包响应速度）
#ifdef _WIN32
  if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&opt,
                 sizeof(opt)) < 0) {
#else
  if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
#endif
    perror("setsockopt TCP_NODELAY");
    // 非致命错误，继续执行
  }

  return 0;
}

/**
 * @brief 读取 HTTP 请求
 */
static int read_http_request(socket_t client_fd, char *buffer,
                             size_t buffer_size) {
  // 使用 recv 读取数据
  ssize_t bytes_read = recv(client_fd, buffer, buffer_size - 1, 0);

  if (bytes_read <= 0) {
    return -1;
  }

  buffer[bytes_read] = '\0';
  return bytes_read;
}

/**
 * @brief 发送 HTTP 响应
 */
static int send_http_response(socket_t client_fd, const char *response,
                              size_t response_len) {
  ssize_t total_sent = 0;

  while (total_sent < (ssize_t)response_len) {
    ssize_t sent =
        send(client_fd, response + total_sent, response_len - total_sent, 0);
    if (sent < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    total_sent += sent;
  }

  return total_sent;
}

/**
 * @brief 线程函数：处理单个 HTTP 连接
 */
static THREAD_RETURN_TYPE handle_connection_thread(void *arg) {
  thread_args_t *args = (thread_args_t *)arg;
  http_server_t *server = args->server;
  socket_t client_fd = args->client_fd;

  char client_ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(args->client_addr.sin_addr), client_ip,
            sizeof(client_ip));

  set_socket_options(client_fd);

  http_parser_context_t *parser_ctx = NULL;
  http_parser_create(&parser_ctx);

  char request_buffer[16384] = {0};
  int bytes_read =
      read_http_request(client_fd, request_buffer, sizeof(request_buffer));

  http_response_t response = {0};
  http_request_t *request = NULL;
  const char *method_str = "UNKNOWN";
  const char *url_str = "/";

  if (bytes_read > 0 && parser_ctx) {
    if (http_parser_execute(parser_ctx, request_buffer, bytes_read) == 0) {
      request = http_parser_get_request(parser_ctx);
      if (request) {
        method_str = (request->method == HTTP_GET)       ? "GET"
                     : (request->method == HTTP_POST)    ? "POST"
                     : (request->method == HTTP_PUT)     ? "PUT"
                     : (request->method == HTTP_DELETE)  ? "DELETE"
                     : (request->method == HTTP_PATCH)   ? "PATCH"
                     : (request->method == HTTP_HEAD)    ? "HEAD"
                     : (request->method == HTTP_OPTIONS) ? "OPTIONS"
                                                         : "UNKNOWN";
        url_str = request->url ? request->url : "/";

        if (request->method == HTTP_OPTIONS) {
          response.status_code = 204;
          response.body = NULL;
          response.body_len = 0;
        }
        else if (server->router) {
          route_node_t *node =
              router_match(server->router, request->method, request->url);
          route_callback_t handler = router_get_callback(node);

          if (node && handler) {
            route_params_t params = {0};
            route_param_t param_storage[16];
            size_t param_count = 0;
            if (router_extract(node, request->url, param_storage, 16, &param_count) == 0) {
              params.params = param_storage;
              params.count = param_count;
            }

            handle_params_t h_params = {.params = &params,
                                        .env = server->env,
                                        .body = request->body,
                                        .body_len = request->body_len};

            int handler_result = handler(&h_params, &response);

            if (handler_result != 0 && response.body == NULL) {
              response.status_code = 500;
              response.body = strdup("{\"error\":\"Internal Server Error\"}");
              response.body_len = strlen(response.body);
            }
          } else {
            response.status_code = 404;
            response.body = strdup("{\"error\":\"Not Found\"}");
            response.body_len = strlen(response.body);
          }
        } else {
          response.status_code = 500;
          response.body = strdup("{\"error\":\"Router not configured\"}");
          response.body_len = strlen(response.body);
        }
      } else {
        response.status_code = 500;
        response.body = strdup("{\"error\":\"Failed to get request\"}");
        response.body_len = strlen(response.body);
      }
    } else {
      response.status_code = 400;
      response.body = strdup("{\"error\":\"Bad Request\"}");
      response.body_len = strlen(response.body);
    }
  } else if (bytes_read <= 0) {
    response.status_code = 400;
    response.body = strdup("{\"error\":\"Failed to read request\"}");
    response.body_len = strlen(response.body);
  }

  if (response.status_code == 0) {
    response.status_code = 200;
  }

  char response_buf[32768];
  int response_len =
      http_build_response(&response, response_buf, sizeof(response_buf));

  if (response_len > 0) {
    send_http_response(client_fd, response_buf, response_len);
    log_request(method_str, url_str, response.status_code, client_ip);
  } else {
    const char *fallback = "HTTP/1.1 500 Internal Server Error\r\n"
                           "Content-Length: 0\r\n"
                           "Connection: close\r\n\r\n";
    send_http_response(client_fd, fallback, strlen(fallback));
    log_request(method_str, url_str, response.status_code, client_ip);
  }

  http_free_response(&response);
  http_free_request(request);
  if (parser_ctx) {
    http_parser_destroy(parser_ctx);
  }
  CLOSE_SOCKET(client_fd);
  free(args);

#ifdef _WIN32
  return 0;
#else
  return NULL;
#endif
}

// ==================== 服务器生命周期实现 ====================

int http_server_init(http_server_t *server, const server_config_t *config) {
  if (!server || !config) {
    return -1;
  }

  memset(server, 0, sizeof(http_server_t));

  // 复制配置
  server->config = *config;
  if (!server->config.host) {
    server->config.host = strdup(SERVER_DEFAULT_HOST);
  } else {
    server->config.host = strdup(config->host);
  }

  if (server->config.port <= 0) {
    server->config.port = SERVER_DEFAULT_PORT;
  }

  if (server->config.map_size == 0) {
    server->config.map_size = SERVER_DEFAULT_MAP_SIZE;
  }

  if (server->config.max_connections <= 0) {
    server->config.max_connections = SERVER_DEFAULT_MAX_CONNECTIONS;
  }

  // 初始化 LMDB 环境
  if (server->config.db_path) {
    int rc = lmjcore_init(server->config.db_path, server->config.map_size,
                          server->config.env_flags, server->config.fn, NULL,
                          &server->env);
    if (rc != 0) {
      fprintf(stderr, "Failed to create LMDB environment at %s\n",
              server->config.db_path);
      free(server->config.host);
      return -1;
    }
  }

  server->listen_fd = SOCKET_ERROR_VAL;
  server->running = false;
  server->router = NULL;

#ifdef _WIN32
  // Windows 需要初始化 Winsock
  WSADATA wsa_data;
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    fprintf(stderr, "WSAStartup failed\n");
    lmjcore_env_close(server->env);
    free(server->config.host);
    return -1;
  }
#endif

  return 0;
}

int http_server_start(http_server_t *server) {
  if (!server) {
    return -1;
  }

  // 创建套接字
  server->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server->listen_fd == SOCKET_ERROR_VAL) {
    perror("socket");
    return -1;
  }

  // 设置套接字选项
  if (set_socket_options(server->listen_fd) < 0) {
    CLOSE_SOCKET(server->listen_fd);
    return -1;
  }

  // 绑定地址
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(server->config.port);

  if (strcmp(server->config.host, "0.0.0.0") == 0) {
    addr.sin_addr.s_addr = INADDR_ANY;
  } else {
    if (inet_pton(AF_INET, server->config.host, &addr.sin_addr) <= 0) {
      fprintf(stderr, "Invalid host address: %s\n", server->config.host);
      CLOSE_SOCKET(server->listen_fd);
      return -1;
    }
  }

  if (bind(server->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    CLOSE_SOCKET(server->listen_fd);
    return -1;
  }

  // 监听
  if (listen(server->listen_fd, server->config.max_connections) < 0) {
    perror("listen");
    CLOSE_SOCKET(server->listen_fd);
    return -1;
  }

  printf("LMJCore HTTP Server started on %s:%d\n", server->config.host,
         server->config.port);
  printf("Database path: %s\n",
         server->config.db_path ? server->config.db_path : "(none)");
  printf("Press Ctrl+C to stop\n");

  // 设置运行标志
  server->running = true;

  // 忽略 SIGPIPE 信号（防止向已关闭的套接字写入时崩溃）
#ifndef _WIN32
  signal(SIGPIPE, SIG_IGN);
#endif

  // 主循环
  while (server->running) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    socket_t client_fd =
        accept(server->listen_fd, (struct sockaddr *)&client_addr, &client_len);

    if (client_fd == SOCKET_ERROR_VAL) {
      if (server->running) {
        perror("accept");
      }
      continue;
    }

    // 创建线程参数
    thread_args_t *args = (thread_args_t *)malloc(sizeof(thread_args_t));
    if (!args) {
      fprintf(stderr, "Failed to allocate thread args\n");
      CLOSE_SOCKET(client_fd);
      continue;
    }

    args->server = server;
    args->client_fd = client_fd;
    args->client_addr = client_addr;

    // 创建线程处理连接
#ifdef _WIN32
    HANDLE thread =
        CreateThread(NULL, 0, handle_connection_thread, args, 0, NULL);
    if (thread) {
      CloseHandle(thread);
    } else {
      fprintf(stderr, "Failed to create thread\n");
      CLOSE_SOCKET(client_fd);
      free(args);
    }
#else
    pthread_t thread;
    if (pthread_create(&thread, NULL, handle_connection_thread, args) != 0) {
      perror("pthread_create");
      CLOSE_SOCKET(client_fd);
      free(args);
    } else {
      pthread_detach(thread); // 分离线程，自动回收资源
    }
#endif
  }

  // 关闭监听套接字
  CLOSE_SOCKET(server->listen_fd);
  server->listen_fd = SOCKET_ERROR_VAL;

  return 0;
}

void http_server_stop(http_server_t *server) {
  if (!server) {
    return;
  }

  printf("Stopping server...\n");
  server->running = false;

  // 关闭监听套接字以中断 accept
  if (server->listen_fd != SOCKET_ERROR_VAL) {
    CLOSE_SOCKET(server->listen_fd);
    server->listen_fd = SOCKET_ERROR_VAL;
  }
}

void http_server_destroy(http_server_t *server) {
  if (!server) {
    return;
  }

  // 停止服务器
  http_server_stop(server);

  // 释放配置中的动态分配内存
  if (server->config.host) {
    free(server->config.host);
    server->config.host = NULL;
  }

  // 关闭 LMDB 环境
  if (server->env) {
    lmjcore_cleanup(server->env);
    server->env = NULL;
  }

  // 注意：router 由调用者管理，这里不释放

#ifdef _WIN32
  WSACleanup();
#endif

  printf("Server destroyed\n");
}