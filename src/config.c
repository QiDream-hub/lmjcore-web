#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <getopt.h>

// ==================== 辅助函数 ====================

/**
 * @brief 去除字符串两端空白
 */
static char *trim(char *str) {
  if (!str) return NULL;

  // 去除前导空白
  while (isspace((unsigned char)*str)) str++;

  if (*str == 0) return str;

  // 去除尾部空白
  char *end = str + strlen(str) - 1;
  while (end > str && isspace((unsigned char)*end)) end--;

  end[1] = '\0';
  return str;
}

/**
 * @brief 解析整数值
 */
static int parse_int(const char *str, int *out) {
  if (!str || !out) return -1;

  char *endptr;
  long val = strtol(str, &endptr, 10);
  if (*endptr != '\0' && !isspace((unsigned char)*endptr)) {
    return -1;  // 解析失败
  }
  *out = (int)val;
  return 0;
}

/**
 * @brief 解析 size_t 值 (支持 K/M/G 后缀)
 */
static int parse_size(const char *str, size_t *out) {
  if (!str || !out) return -1;

  char *endptr;
  long long val = strtoll(str, &endptr, 10);

  // 处理后缀
  if (*endptr) {
    switch (tolower((unsigned char)*endptr)) {
      case 'k':
        val *= 1024;
        break;
      case 'm':
        val *= 1024 * 1024;
        break;
      case 'g':
        val *= 1024 * 1024 * 1024;
        break;
      default:
        if (!isspace((unsigned char)*endptr)) {
          return -1;  // 未知后缀
        }
    }
  }

  if (val < 0) return -1;
  *out = (size_t)val;
  return 0;
}

/**
 * @brief 解析布尔值
 */
static int parse_bool(const char *str, bool *out) {
  if (!str || !out) return -1;

  if (strcasecmp(str, "true") == 0 || strcasecmp(str, "yes") == 0 ||
      strcasecmp(str, "on") == 0 || strcmp(str, "1") == 0) {
    *out = true;
    return 0;
  }

  if (strcasecmp(str, "false") == 0 || strcasecmp(str, "no") == 0 ||
      strcasecmp(str, "off") == 0 || strcmp(str, "0") == 0) {
    *out = false;
    return 0;
  }

  return -1;
}

// ==================== 配置实现 ====================

void config_init(config_t *config) {
  if (!config) return;

  memset(config, 0, sizeof(config_t));

  // 设置默认值
  strncpy(config->config_path, CONFIG_DEFAULT_PATH, CONFIG_MAX_PATH - 1);
  strncpy(config->host, CONFIG_DEFAULT_HOST, CONFIG_MAX_HOST - 1);
  config->port = CONFIG_DEFAULT_PORT;
  strncpy(config->db_path, CONFIG_DEFAULT_DB_PATH, CONFIG_MAX_PATH - 1);
  config->map_size = CONFIG_DEFAULT_MAP_SIZE;
  config->max_connections = CONFIG_DEFAULT_MAX_CONNECTIONS;
  config->txn_timeout = CONFIG_DEFAULT_TXN_TIMEOUT;
  config->daemon = CONFIG_DEFAULT_DAEMON;
  config->log_level = CONFIG_DEFAULT_LOG_LEVEL;
}

int config_load(config_t *config, const char *path) {
  if (!config || !path) return -1;

  FILE *fp = fopen(path, "r");
  if (!fp) {
    fprintf(stderr, "Warning: Cannot open config file '%s', using defaults\n", path);
    return -1;
  }

  char line[1024];
  int line_num = 0;

  while (fgets(line, sizeof(line), fp)) {
    line_num++;

    // 去除注释 (# 或 ; 开头)
    char *comment = strchr(line, '#');
    if (!comment) comment = strchr(line, ';');
    if (comment) *comment = '\0';

    // 去除两端空白
    char *trimmed = trim(line);

    // 跳过空行
    if (*trimmed == '\0') continue;

    // 解析 key=value
    char *eq = strchr(trimmed, '=');
    if (!eq) {
      fprintf(stderr, "Warning: Invalid config line %d: %s\n", line_num, trimmed);
      continue;
    }

    *eq = '\0';
    char *key = trim(trimmed);
    char *value = trim(eq + 1);

    // 解析各个配置项
    int int_val;
    size_t size_val;
    bool bool_val;

    if (strcmp(key, "host") == 0) {
      strncpy(config->host, value, CONFIG_MAX_HOST - 1);
    } else if (strcmp(key, "port") == 0 && parse_int(value, &int_val) == 0) {
      config->port = int_val;
    } else if (strcmp(key, "db_path") == 0) {
      strncpy(config->db_path, value, CONFIG_MAX_PATH - 1);
    } else if (strcmp(key, "map_size") == 0 && parse_size(value, &size_val) == 0) {
      config->map_size = size_val;
    } else if (strcmp(key, "max_connections") == 0 && parse_int(value, &int_val) == 0) {
      config->max_connections = int_val;
    } else if (strcmp(key, "txn_timeout") == 0 && parse_int(value, &int_val) == 0) {
      config->txn_timeout = int_val;
    } else if (strcmp(key, "daemon") == 0 && parse_bool(value, &bool_val) == 0) {
      config->daemon = bool_val;
    } else if (strcmp(key, "log_level") == 0 && parse_int(value, &int_val) == 0) {
      config->log_level = int_val;
    } else {
      fprintf(stderr, "Warning: Unknown config key '%s' at line %d\n", key, line_num);
    }
  }

  fclose(fp);
  return 0;
}

int config_parse_args(config_t *config, int argc, char **argv) {
  if (!config) return -1;

  static struct option long_options[] = {
      {"host", required_argument, 0, 'H'},
      {"port", required_argument, 0, 'p'},
      {"db-path", required_argument, 0, 'd'},
      {"map-size", required_argument, 0, 'm'},
      {"max-connections", required_argument, 0, 'c'},
      {"txn-timeout", required_argument, 0, 't'},
      {"config", required_argument, 0, 'C'},
      {"daemon", no_argument, 0, 'D'},
      {"log-level", required_argument, 0, 'l'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};

  int opt;
  int option_index = 0;

  while ((opt = getopt_long(argc, argv, "H:p:d:m:c:t:C:Dl:h", long_options,
                            &option_index)) != -1) {
    int int_val;
    size_t size_val;

    switch (opt) {
    case 'H':
      strncpy(config->host, optarg, CONFIG_MAX_HOST - 1);
      break;

    case 'p':
      if (parse_int(optarg, &int_val) != 0) {
        fprintf(stderr, "Error: Invalid port value: %s\n", optarg);
        return -1;
      }
      config->port = int_val;
      break;

    case 'd':
      strncpy(config->db_path, optarg, CONFIG_MAX_PATH - 1);
      break;

    case 'm':
      if (parse_size(optarg, &size_val) != 0) {
        fprintf(stderr, "Error: Invalid map_size value: %s\n", optarg);
        return -1;
      }
      config->map_size = size_val;
      break;

    case 'c':
      if (parse_int(optarg, &int_val) != 0) {
        fprintf(stderr, "Error: Invalid max_connections value: %s\n", optarg);
        return -1;
      }
      config->max_connections = int_val;
      break;

    case 't':
      if (parse_int(optarg, &int_val) != 0) {
        fprintf(stderr, "Error: Invalid txn_timeout value: %s\n", optarg);
        return -1;
      }
      config->txn_timeout = int_val;
      break;

    case 'C':
      strncpy(config->config_path, optarg, CONFIG_MAX_PATH - 1);
      break;

    case 'D':
      config->daemon = true;
      break;

    case 'l':
      if (parse_int(optarg, &int_val) != 0 || int_val < 0 || int_val > 3) {
        fprintf(stderr, "Error: Invalid log_level value: %s (must be 0-3)\n", optarg);
        return -1;
      }
      config->log_level = int_val;
      break;

    case 'h':
      config_print_usage(argv[0]);
      exit(0);

    default:
      return -1;
    }
  }

  return 0;
}

int config_to_server_config(config_t *config, server_config_t *server_config) {
  if (!config || !server_config) return -1;

  server_config->host = config->host;
  server_config->port = config->port;
  server_config->db_path = config->db_path;
  server_config->map_size = config->map_size;
  server_config->max_connections = config->max_connections;
  server_config->txn_timeout = config->txn_timeout;
  // env_flags 和 fn 由其他代码设置

  return 0;
}

void config_print(const config_t *config) {
  if (!config) return;

  printf("Current Configuration:\n");
  printf("  Config File:    %s\n", config->config_path);
  printf("  Host:           %s\n", config->host);
  printf("  Port:           %d\n", config->port);
  printf("  DB Path:        %s\n", config->db_path);
  printf("  Map Size:       %zu bytes\n", config->map_size);
  printf("  Max Connections:%d\n", config->max_connections);
  printf("  Txn Timeout:    %d seconds\n", config->txn_timeout);
  printf("  Daemon:         %s\n", config->daemon ? "yes" : "no");
  printf("  Log Level:      %d\n", config->log_level);
}

void config_print_usage(const char *program_name) {
  printf("Usage: %s [OPTIONS]\n", program_name);
  printf("\nOptions:\n");
  printf("  -H, --host <addr>        Listen address (default: %s)\n", CONFIG_DEFAULT_HOST);
  printf("  -p, --port <port>        Listen port (default: %d)\n", CONFIG_DEFAULT_PORT);
  printf("  -d, --db-path <path>     LMDB database path (default: %s)\n", CONFIG_DEFAULT_DB_PATH);
  printf("  -m, --map-size <size>    Memory map size (default: %d, support K/M/G suffix)\n", CONFIG_DEFAULT_MAP_SIZE);
  printf("  -c, --max-connections <n> Max connections (default: %d)\n", CONFIG_DEFAULT_MAX_CONNECTIONS);
  printf("  -t, --txn-timeout <sec>  Transaction timeout in seconds (default: %d)\n", CONFIG_DEFAULT_TXN_TIMEOUT);
  printf("  -C, --config <file>      Config file path (default: %s)\n", CONFIG_DEFAULT_PATH);
  printf("  -D, --daemon             Run as daemon process\n");
  printf("  -l, --log-level <0-3>    Log level: 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR (default: %d)\n", CONFIG_DEFAULT_LOG_LEVEL);
  printf("  -h, --help               Show this help message\n");
  printf("\nExample:\n");
  printf("  %s -p 8080 -d /data/lmjcore\n", program_name);
  printf("  %s -C /etc/lmjcore.conf --daemon\n", program_name);
}
