#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>
#include <time.h>

// ==================== 全局状态 ====================

static struct {
  log_level_t level;
  bool use_color;
  FILE *stream;
} g_log = {
  .level = LOG_DEFAULT_LEVEL,
  .use_color = true,
  .stream = NULL
};

// ==================== ANSI 颜色代码 ====================

#define COLOR_RESET "\033[0m"
#define COLOR_DEBUG "\033[90m"   // 灰色
#define COLOR_INFO  "\033[32m"   // 绿色
#define COLOR_WARN  "\033[33m"   // 黄色
#define COLOR_ERROR "\033[31m"   // 红色

// ==================== 辅助函数 ====================

/**
 * @brief 获取时间戳
 */
static void get_timestamp(char *buf, size_t size) {
  struct timeval tv;
  struct tm *tm_info;
  
  gettimeofday(&tv, NULL);
  tm_info = localtime(&tv.tv_sec);
  
  strftime(buf, size, "%Y-%m-%d %H:%M:%S", tm_info);
  // 添加毫秒
  snprintf(buf + strlen(buf), size - strlen(buf), ".%03ld", 
           tv.tv_usec / 1000);
}

/**
 * @brief 获取短文件名 (去除路径前缀)
 */
static const char *get_short_name(const char *file) {
  const char *slash = strrchr(file, '/');
  return slash ? slash + 1 : file;
}

/**
 * @brief 获取颜色前缀
 */
static const char *get_color(log_level_t level) {
  if (!g_log.use_color) return "";
  
  switch (level) {
    case LOG_DEBUG: return COLOR_DEBUG;
    case LOG_INFO:  return COLOR_INFO;
    case LOG_WARN:  return COLOR_WARN;
    case LOG_ERROR: return COLOR_ERROR;
    default:        return "";
  }
}

/**
 * @brief 获取颜色后缀
 */
static const char *get_color_reset(void) {
  return g_log.use_color ? COLOR_RESET : "";
}

// ==================== 日志实现 ====================

void log_init(log_level_t level, bool use_color) {
  g_log.level = level;
  g_log.use_color = use_color;
  g_log.stream = stdout;
}

void log_set_level(log_level_t level) {
  g_log.level = level;
}

log_level_t log_get_level(void) {
  return g_log.level;
}

void log_set_stream(FILE *stream) {
  g_log.stream = stream;
}

const char* log_level_str(log_level_t level) {
  switch (level) {
    case LOG_DEBUG: return "DEBUG";
    case LOG_INFO:  return "INFO";
    case LOG_WARN:  return "WARN";
    case LOG_ERROR: return "ERROR";
    default:        return "UNKNOWN";
  }
}

void log_write(log_level_t level, const char *file, int line, 
               const char *format, ...) {
  // 检查日志级别
  if (level < g_log.level) {
    return;
  }
  
  FILE *out = g_log.stream ? g_log.stream : stdout;
  if (level >= LOG_WARN) {
    out = stderr;  // 警告和错误输出到 stderr
  }
  
  // 获取时间戳
  char timestamp[32];
  get_timestamp(timestamp, sizeof(timestamp));
  
  // 获取短文件名
  const char *short_file = get_short_name(file);
  
  // 写入日志头
  fprintf(out, "[%s] %s%-5s%s [%s:%d] ", 
          timestamp,
          get_color(level),
          log_level_str(level),
          get_color_reset(),
          short_file, line);
  
  // 写入日志内容
  va_list args;
  va_start(args, format);
  vfprintf(out, format, args);
  va_end(args);
  
  // 换行
  fprintf(out, "\n");
  fflush(out);
}
