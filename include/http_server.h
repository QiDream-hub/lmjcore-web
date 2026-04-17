#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "lmjcore.h"
#include "router.h"
#include "http_parser.h"
#include "lmjcore_handle.h"
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

// ==================== HTTP 服务器配置 ====================

typedef struct {
  char *host;          // 监听地址（默认 "0.0.0.0"）
  int port;            // 监听端口（默认 8080）
  const char *db_path; // LMDB 数据库路径
  size_t map_size;     // 内存映射大小（默认 10MB = 10 * 1024 * 1024）
  lmjcore_ptr_generator_fn fn; // LMJCore 指针生成函数
  unsigned int env_flags; // 环境标志（默认安全模式 0）
  int max_connections;    // 最大连接队列（默认 128）
  int txn_timeout;        // 事务超时时间（秒，默认 5 秒）
} server_config_t;

// 默认配置
#define SERVER_DEFAULT_HOST "0.0.0.0"
#define SERVER_DEFAULT_PORT 8080
#define SERVER_DEFAULT_MAP_SIZE (10 * 1024 * 1024) // 10MB
#define SERVER_DEFAULT_MAX_CONNECTIONS 128
#define SERVER_DEFAULT_TXN_TIMEOUT 5

// ==================== HTTP 服务器结构 ====================

typedef struct {
  server_config_t config; // 服务器配置
  lmjcore_env *env;       // LMDB 环境
  router_t *router;       // URL 路由器
  int listen_fd;          // 监听套接字
  bool running;           // 运行状态标志
} http_server_t;

// ==================== 服务器生命周期函数 ====================

/**
 * @brief 初始化 HTTP 服务器
 *
 * @param server 服务器实例指针
 * @param config 服务器配置
 * @return int 0 成功，-1 失败
 */
int http_server_init(http_server_t *server, const server_config_t *config);

/**
 * @brief 启动 HTTP 服务器（阻塞）
 *
 * 开始监听端口并处理请求，每个连接会创建独立线程处理。
 *
 * @param server 服务器实例指针
 * @return int 0 成功，-1 失败
 */
int http_server_start(http_server_t *server);

/**
 * @brief 停止 HTTP 服务器
 *
 * 设置停止标志，等待当前连接处理完成后退出。
 *
 * @param server 服务器实例指针
 */
void http_server_stop(http_server_t *server);

/**
 * @brief 销毁 HTTP 服务器，释放资源
 *
 * @param server 服务器实例指针
 */
void http_server_destroy(http_server_t *server);

// ==================== 辅助函数 ====================

/**
 * @brief 设置服务器路由器
 *
 * @param server 服务器实例指针
 * @param router 路由器实例（由调用者创建）
 */
static inline void http_server_set_router(http_server_t *server,
                                          router_t *router) {
  if (server && router) {
    server->router = router;
  }
}

/**
 * @brief 获取服务器 LMDB 环境句柄
 *
 * @param server 服务器实例指针
 * @return lmjcore_env* 环境句柄
 */
static inline lmjcore_env *http_server_get_env(http_server_t *server) {
  return server ? server->env : NULL;
}

/**
 * @brief 获取服务器事务超时时间
 *
 * @param server 服务器实例指针
 * @return int 超时时间（秒）
 */
static inline int http_server_get_txn_timeout(http_server_t *server) {
  return server ? server->config.txn_timeout : SERVER_DEFAULT_TXN_TIMEOUT;
}

#endif // HTTP_SERVER_H
