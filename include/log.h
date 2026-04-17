#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <stdbool.h>

// ==================== 日志级别 ====================

typedef enum {
  LOG_DEBUG = 0,
  LOG_INFO  = 1,
  LOG_WARN  = 2,
  LOG_ERROR = 3
} log_level_t;

// ==================== 日志配置 ====================

#define LOG_DEFAULT_LEVEL LOG_INFO
#define LOG_MAX_PREFIX_LEN 64

// ==================== 日志函数 ====================

/**
 * @brief 初始化日志系统
 *
 * @param level 日志级别
 * @param use_color 是否使用颜色输出
 */
void log_init(log_level_t level, bool use_color);

/**
 * @brief 设置日志级别
 *
 * @param level 日志级别
 */
void log_set_level(log_level_t level);

/**
 * @brief 获取当前日志级别
 *
 * @return log_level_t 当前日志级别
 */
log_level_t log_get_level(void);

/**
 * @brief 设置日志输出流
 *
 * @param stream 输出流 (stdout/stderr)
 */
void log_set_stream(FILE *stream);

/**
 * @brief 输出日志消息
 *
 * @param level 日志级别
 * @param file 源文件名称
 * @param line 行号
 * @param format 格式字符串
 * @param ... 可变参数
 */
void log_write(log_level_t level, const char *file, int line, 
               const char *format, ...);

/**
 * @brief 获取日志级别字符串
 *
 * @param level 日志级别
 * @return const char* 级别字符串
 */
const char* log_level_str(log_level_t level);

// ==================== 日志宏 ====================

// 内部宏，用户不应直接使用
#define _LOG(level, fmt, ...) \
  log_write(level, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

// 公共宏
#define LOG_DEBUG(fmt, ...) \
  _LOG(LOG_DEBUG, fmt, ##__VA_ARGS__)

#define LOG_INFO(fmt, ...) \
  _LOG(LOG_INFO, fmt, ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) \
  _LOG(LOG_WARN, fmt, ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...) \
  _LOG(LOG_ERROR, fmt, ##__VA_ARGS__)

#endif // LOG_H
