#include "http_server.h"
#include "routes.h"
#include "config.h"
#include "lmjcore_uuid_gen.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

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

/**
 * @brief 守护进程模式
 */
static void daemonize(void) {
  pid_t pid;

  // 第一次 fork，创建子进程并退出父进程
  pid = fork();
  if (pid < 0) {
    fprintf(stderr, "Failed to fork\n");
    exit(1);
  }
  if (pid > 0) {
    // 父进程退出
    exit(0);
  }

  // 创建新会话
  if (setsid() < 0) {
    fprintf(stderr, "Failed to setsid\n");
    exit(1);
  }

  // 第二次 fork，确保不会是会话首领
  pid = fork();
  if (pid < 0) {
    fprintf(stderr, "Failed to fork\n");
    exit(1);
  }
  if (pid > 0) {
    exit(0);
  }

  // 更改工作目录到根目录
  chdir("/");

  // 设置文件权限掩码
  umask(0);

  // 关闭所有文件描述符 (可选)
  // for (int i = 0; i < 1024; i++) {
  //   close(i);
  // }
}

int main(int argc, char **argv) {
  config_t config;

  // 初始化配置为默认值
  config_init(&config);

  // 解析命令行参数 (优先)
  if (config_parse_args(&config, argc, argv) != 0) {
    fprintf(stderr, "Failed to parse command line arguments\n");
    config_print_usage(argv[0]);
    return 1;
  }

  // 从配置文件加载配置 (如果存在)
  // 注意：配置文件先加载，命令行参数会覆盖配置文件
  if (strcmp(config.config_path, CONFIG_DEFAULT_PATH) != 0) {
    // 用户指定了配置文件，必须存在
    config_load(&config, config.config_path);
  } else {
    // 使用默认路径，文件不存在也可以继续
    config_load(&config, config.config_path);
  }

  // 打印配置信息 (调试模式)
  if (config.log_level <= 0) {  // DEBUG
    config_print(&config);
  }

  // 守护进程模式
  if (config.daemon) {
    printf("Starting daemon mode...\n");
    daemonize();
  }

  // 设置信号处理
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  // 转换为服务器配置
  server_config_t server_config;
  memset(&server_config, 0, sizeof(server_config_t));
  if (config_to_server_config(&config, &server_config) != 0) {
    fprintf(stderr, "Failed to convert config\n");
    return 1;
  }

  // 设置 LMJCore 指针生成函数
  server_config.fn = lmjcore_uuidv4_ptr_gen;

  // 初始化服务器
  http_server_t server;
  if (http_server_init(&server, &server_config) != 0) {
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
  printf("Starting LMJCore HTTP Server on %s:%d ...\n", config.host, config.port);
  printf("Database path: %s\n", config.db_path);
  if (config.daemon) {
    printf("Running in daemon mode\n");
  }
  int rc = http_server_start(&server);

  // 清理资源
  router_destroy(router);
  http_server_destroy(&server);

  return rc;
}
