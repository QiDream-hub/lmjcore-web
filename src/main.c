#include "http_server.h"
#include "routes.h"
#include "lmjcore_uuid_gen.h"
#include <signal.h>
#include <stdio.h>

// 全局服务器指针，用于信号处理
static http_server_t *g_server = NULL;

// 信号处理函数
static void signal_handler(int sig) {
  if (sig == SIGINT || sig == SIGTERM) {
    printf("\nReceived signal %d, shutting down...\n", sig);
    if (g_server) {
      http_server_stop(g_server);
    }
  }
}

int main(int argc, char *argv[]) {
  // 设置信号处理
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  // 配置服务器
  server_config_t config = {.host = "0.0.0.0",
                            .port = 8080,
                            .db_path = "./lmjcore_data",
                            .map_size = 10 * 1024 * 1024, // 10MB
                            .env_flags = 0,
                            .max_connections = 128,
                            .fn = lmjcore_uuidv4_ptr_gen};

  // 初始化服务器
  http_server_t server;
  if (http_server_init(&server, &config) != 0) {
    fprintf(stderr, "Failed to initialize server\n");
    return 1;
  }

  g_server = &server;

  // 创建路由器
  router_t *router = router_create();
  if (!router) {
    fprintf(stderr, "Failed to create router\n");
    http_server_destroy(&server);
    return 1;
  }

  // 设置路由器
  http_server_set_router(&server, router);

  // 注册路由
  if (register_all_routes(router) != 0) {
    fprintf(stderr, "Failed to setup routes\n");
    router_destroy(router);
    http_server_destroy(&server);
    return 1;
  }

  // 启动服务器（阻塞）
  printf("Starting LMJCore HTTP Server...\n");
  int rc = http_server_start(&server);

  // 清理资源
  router_destroy(router);
  http_server_destroy(&server);

  return rc;
}