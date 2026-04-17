#ifndef CONFIG_H
#define CONFIG_H

#include "http_server.h"
#include <stdbool.h>

// ==================== 配置文件结构 ====================

#define CONFIG_MAX_PATH 512
#define CONFIG_MAX_HOST 64
#define CONFIG_DEFAULT_PATH "lmjcore.conf"

typedef struct {
  char config_path[CONFIG_MAX_PATH];  // 配置文件路径
  char host[CONFIG_MAX_HOST];         // 监听地址
  int port;                           // 监听端口
  char db_path[CONFIG_MAX_PATH];      // LMDB 数据库路径
  size_t map_size;                    // 内存映射大小 (字节)
  int max_connections;                // 最大连接数
  int txn_timeout;                    // 事务超时 (秒)
  bool daemon;                        // 是否守护进程模式
  int log_level;                      // 日志级别 (0=DEBUG, 1=INFO, 2=WARN, 3=ERROR)
} config_t;

// ==================== 默认配置值 ====================

#define CONFIG_DEFAULT_HOST "0.0.0.0"
#define CONFIG_DEFAULT_PORT 8080
#define CONFIG_DEFAULT_DB_PATH "./lmjcore_data"
#define CONFIG_DEFAULT_MAP_SIZE (10 * 1024 * 1024)  // 10MB
#define CONFIG_DEFAULT_MAX_CONNECTIONS 128
#define CONFIG_DEFAULT_TXN_TIMEOUT 5
#define CONFIG_DEFAULT_DAEMON false
#define CONFIG_DEFAULT_LOG_LEVEL 1  // INFO

// ==================== 函数声明 ====================

/**
 * @brief 初始化配置结构为默认值
 *
 * @param config 配置结构指针
 */
void config_init(config_t *config);

/**
 * @brief 从配置文件读取配置
 *
 * @param config 配置结构指针
 * @param path 配置文件路径
 * @return int 0 成功，-1 失败
 */
int config_load(config_t *config, const char *path);

/**
 * @brief 从命令行参数解析配置
 *
 * @param config 配置结构指针
 * @param argc 参数个数
 * @param argv 参数数组
 * @return int 0 成功，-1 失败 (如参数错误)
 */
int config_parse_args(config_t *config, int argc, char **argv);

/**
 * @brief 将配置转换为服务器配置
 *
 * @param config 配置结构指针
 * @param server_config 服务器配置结构指针
 * @return int 0 成功，-1 失败
 */
int config_to_server_config(config_t *config, server_config_t *server_config);

/**
 * @brief 打印当前配置 (用于调试)
 *
 * @param config 配置结构指针
 */
void config_print(const config_t *config);

/**
 * @brief 打印使用说明
 *
 * @param program_name 程序名称
 */
void config_print_usage(const char *program_name);

#endif // CONFIG_H
