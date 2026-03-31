#include "../include/lmjcore_web.h"
#include "../thirdparty/LMJCore/core/include/lmjcore.h"
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile int g_running = 1;

void signal_handler(int sig) {
  printf("\nShutting down...\n");
  g_running = 0;
  lmjcore_web_stop();
}

void print_usage(const char *prog) {
  printf("Usage: %s [options]\n", prog);
  printf("Options:\n");
  printf("  -p, --path PATH     Database path (default: ./data)\n");
  printf("  -s, --size SIZE     Map size in MB (default: 100)\n");
  printf("  -h, --host HOST     Bind host (default: 0.0.0.0)\n");
  printf("  -P, --port PORT     Bind port (default: 8080)\n");
  printf("  -f, --flags FLAGS   Environment flags (comma separated)\n");
  printf("  -v, --verbose       Verbose output\n");
  printf("  --help              Show this help\n");
  printf("\nFlags:\n");
  printf("  nosync     - Disable fsync\n");
  printf("  writemap   - Use writable memory map\n");
  printf("  mapasync   - Async memory sync\n");
  printf("  nometasync - Disable meta sync\n");
  printf("  readonly   - Read-only mode\n");
  printf("\nExamples:\n");
  printf("  %s -p /var/lmjcore -s 500 -P 8080\n", prog);
  printf("  %s --flags nosync,writemap\n", prog);
}

unsigned int parse_flags(const char *flag_str) {
  unsigned int flags = 0;
  char *str = strdup(flag_str);
  char *token = strtok(str, ",");

  while (token) {
    if (strcmp(token, "nosync") == 0)
      flags |= LMJCORE_ENV_NOSYNC;
    else if (strcmp(token, "writemap") == 0)
      flags |= LMJCORE_ENV_WRITEMAP;
    else if (strcmp(token, "mapasync") == 0)
      flags |= LMJCORE_ENV_MAPASYNC;
    else if (strcmp(token, "nometasync") == 0)
      flags |= LMJCORE_ENV_NOMETASYNC;
    else if (strcmp(token, "readonly") == 0)
      flags |= LMJCORE_ENV_READONLY;
    else if (strcmp(token, "nolock") == 0)
      flags |= LMJCORE_ENV_NOLOCK;
    else {
      fprintf(stderr, "Unknown flag: %s\n", token);
    }
    token = strtok(NULL, ",");
  }

  free(str);
  return flags;
}

int main(int argc, char *argv[]) {
  lmjcore_web_config_t config = {.db_path = "./data",
                                 .map_size = 100 * 1024 * 1024, // 100 MB
                                 .env_flags = 0,
                                 .bind_host = "0.0.0.0",
                                 .bind_port = 8080,
                                 .max_path_depth = 32,
                                 .enable_audit = true};

  int verbose = 0;

  static struct option long_options[] = {
      {"path", required_argument, 0, 'p'},  {"size", required_argument, 0, 's'},
      {"host", required_argument, 0, 'h'},  {"port", required_argument, 0, 'P'},
      {"flags", required_argument, 0, 'f'}, {"verbose", no_argument, 0, 'v'},
      {"help", no_argument, 0, 1},          {0, 0, 0, 0}};

  int opt;
  int option_index = 0;
  while ((opt = getopt_long(argc, argv, "p:s:h:P:f:v", long_options,
                            &option_index)) != -1) {
    switch (opt) {
    case 'p':
      config.db_path = optarg;
      break;
    case 's':
      config.map_size = (size_t)atoll(optarg) * 1024 * 1024;
      break;
    case 'h':
      config.bind_host = optarg;
      break;
    case 'P':
      config.bind_port = (uint16_t)atoi(optarg);
      break;
    case 'f':
      config.env_flags = parse_flags(optarg);
      break;
    case 'v':
      verbose = 1;
      break;
    case 1:
      print_usage(argv[0]);
      return 0;
    default:
      print_usage(argv[0]);
      return 1;
    }
  }

  // 设置信号处理
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  printf("LMJCore Web Server v1.0.0\n");
  printf("========================\n");
  if (verbose) {
    printf("Configuration:\n");
    printf("  Database: %s\n", config.db_path);
    printf("  Map size: %zu MB\n", config.map_size / (1024 * 1024));
    printf("  Bind: %s:%d\n", config.bind_host, config.bind_port);
    printf("  Flags: 0x%x\n", config.env_flags);
    printf("========================\n");
  }

  int rc = lmjcore_web_init(&config);
  if (rc != LMJCORE_WEB_SUCCESS) {
    fprintf(stderr, "Failed to initialize: %d\n", rc);
    return 1;
  }

  rc = lmjcore_web_start();
  if (rc != LMJCORE_WEB_SUCCESS) {
    fprintf(stderr, "Failed to start server: %d\n", rc);
    lmjcore_web_cleanup();
    return 1;
  }

  // 等待停止信号
  while (g_running) {
    sleep(1);
  }

  lmjcore_web_cleanup();
  printf("Server stopped.\n");

  return 0;
}