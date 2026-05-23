#include "http_server.h"
#include "routes.h"
#include "config.h"
#include "zlog.h"
#include "lmjcore_uuid_gen.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// 全局服务器指针，用于信号处理
static http_server_t *g_server = NULL;
// 原子标志，防止重复停止
static volatile sig_atomic_t g_server_stopping = 0;

// 信号处理函数 (只设置标志，不做复杂操作)
static void signal_handler(int sig) {
  if (sig == SIGINT || sig == SIGTERM) {
    // 防止重复停止
    if (g_server && g_server_stopping == 0) {
      g_server_stopping = 1;
      http_server_stop(g_server);
    }
  }
}

int main(int argc, char **argv) {
  config_t config;

  // 初始化配置为默认值
  config_init(&config);

  // 解析命令行参数 (优先)
  if (config_parse_args(&config, argc, argv) != 0) {
    dzlog_error("Failed to parse command line arguments");
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

  // 根据配置构建 zlog 配置字符串
  const char *zlog_level_str = "INFO";
  switch (config.log_level) {
    case 0: zlog_level_str = "DEBUG"; break;
    case 1: zlog_level_str = "INFO"; break;
    case 2: zlog_level_str = "WARN"; break;
    case 3: zlog_level_str = "ERROR"; break;
    default: zlog_level_str = "INFO"; break;
  }

  // 解析日志输出目标
  const char *log_output = ">stdout";
  if (strcmp(config.log_output, "stderr") == 0) {
    log_output = ">stderr";
  } else if (strcmp(config.log_output, "stdout") != 0) {
    // 假设是文件路径
    log_output = config.log_output;
  }

  char zlog_config[1024];
  int written = snprintf(zlog_config, sizeof(zlog_config),
    "[global]\n"
    "strict init = false\n"
    "default format = \"%%d %%V %%-6c [%%F:%%L] %%m%%n\"\n"
    "[rules]\n"
  );
  // 追加级别和输出目标
  snprintf(zlog_config + written, sizeof(zlog_config) - written,
    "main.%s        %s;\n",
    zlog_level_str, log_output
  );

  // 初始化 zlog 日志系统（直接使用配置字符串，不依赖外部文件）
  if (zlog_init_from_string(zlog_config) != 0) {
    fprintf(stderr, "Failed to initialize zlog\n");
    return 1;
  }

  // 设置默认 category 为 main
  if (dzlog_set_category("main") != 0) {
    fprintf(stderr, "Failed to set zlog category\n");
    return 1;
  }

  // 打印配置信息 (调试模式)
  if (config.log_level == 0) {
    config_print(&config);
  }

  // 设置信号处理
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  // 转换为服务器配置
  server_config_t server_config;
  memset(&server_config, 0, sizeof(server_config_t));
  if (config_to_server_config(&config, &server_config) != 0) {
    dzlog_error("Failed to convert config");
    return 1;
  }

  // 设置 LMJCore 指针生成函数
  server_config.fn = lmjcore_uuidv4_ptr_gen;

  // 初始化服务器
  http_server_t server;
  if (http_server_init(&server, &server_config) != 0) {
    dzlog_error("Failed to initialize server");
    return 1;
  }

  g_server = &server;

  // 创建路由器
  router_t *router = router_create();
  if (!router) {
    dzlog_error("Failed to create router");
    http_server_destroy(&server);
    return 1;
  }

  // 设置路由器
  http_server_set_router(&server, router);

  // 注册路由
  if (register_all_routes(router) != 0) {
    dzlog_error("Failed to setup routes");
    router_destroy(router);
    http_server_destroy(&server);
    return 1;
  }

  // 启动服务器（阻塞）
  dzlog_info("Starting LMJCore HTTP Server on %s:%d ...", config.host, config.port);
  dzlog_info("Database path: %s", config.db_path);
  int rc = http_server_start(&server);

  // 服务器停止后输出日志
  if (g_server_stopping) {
    dzlog_info("Received signal, server stopped");
  }

  // 清理资源
  router_destroy(router);
  http_server_destroy(&server);

  // 清理 zlog
  zlog_fini();

  return rc;
}
