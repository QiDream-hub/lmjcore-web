// include/handle_utils.h - 处理器通用工具函数
#ifndef HANDLE_UTILS_H
#define HANDLE_UTILS_H

#include "lmjcore.h"
#include "router.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

// ==================== 值类型标记 ====================

// 值类型标记（存储格式）
#define LMJCORE_VALUE_TYPE_RAW 0x00  // 原始数据
#define LMJCORE_VALUE_TYPE_PTR 0x01  // 指针引用
#define LMJCORE_VALUE_TYPE_NULL 0x02 // 空值

// API 返回的类型标识
typedef enum {
  VALUE_TYPE_RAW = 0,
  VALUE_TYPE_REF = 1,
  VALUE_TYPE_NULL = 2,
  VALUE_TYPE_SET = 3,
  VALUE_TYPE_OBJECT = 4
} api_value_type_t;

// 处理器参数
typedef struct {
  route_params_t *params;
  lmjcore_env *env;
  lmjcore_txn *txn;        // 可选：外部传入的事务（批量操作时共享）
  char *body;              // 请求体
  size_t body_len;         // 请求体长度
  int txn_timeout;         // 事务超时时间（秒）
  time_t txn_start_time;   // 事务开始时间
  bool auto_manage_txn;    // 是否自动管理事务（默认 true，批量操作时设为 false）
} handle_params_t;

// ==================== 工具函数声明 ====================

/**
 * @brief 从 route_params_t 中获取指定索引的参数
 *
 * @param params 参数列表
 * @param index 参数索引
 * @return const char* 参数字符串（静态缓冲区，下次调用会覆盖）
 */
const char *route_params_get(route_params_t *params, size_t index);

/**
 * @brief 将 34 位十六进制字符串转换为 17 字节二进制指针
 *
 * @param str 十六进制字符串
 * @param ptr_out 输出缓冲区（至少 17 字节）
 * @return int 错误码（0 表示成功）
 */
int lmjcore_ptr_from_hex(const char *str, uint8_t *ptr_out);

/**
 * @brief 将 17 字节二进制指针转换为 34 位十六进制字符串
 *
 * @param ptr 二进制指针
 * @param str_out 输出缓冲区（至少 35 字节）
 * @return int 错误码（0 表示成功）
 */
int lmjcore_ptr_to_hex(const uint8_t *ptr, char *str_out);

/**
 * @brief 解析查询路径字符串
 *
 * 格式："01abc123.user.profile.name"
 *
 * @param path_str 路径字符串
 * @param start_ptr_out 输出起始指针（34 位十六进制字符串，需调用方释放）
 * @param segments_out 输出路径段数组（需调用方释放）
 * @param segment_count_out 输出路径段数量
 * @return int 错误码
 */
int lmjcore_parse_query_path(const char *path_str, char **start_ptr_out,
                             char ***segments_out, size_t *segment_count_out);

/**
 * @brief 编码值为存储格式
 *
 * @param value_str 输入值字符串
 * @param value_len 值长度
 * @param out_buf 输出缓冲区
 * @param out_buf_size 输出缓冲区大小
 * @param out_len 输出实际长度
 * @return int 错误码
 */
int lmjcore_encode_value(const char *value_str, size_t value_len,
                         uint8_t *out_buf, size_t out_buf_size,
                         size_t *out_len);

/**
 * @brief 解码存储格式的值为字符串
 *
 * @param data 存储的数据
 * @param data_len 数据长度
 * @param out_str 输出字符串（需调用方释放）
 * @param out_type 输出类型
 * @return int 错误码
 */
int lmjcore_decode_value(const uint8_t *data, size_t data_len, char **out_str,
                         api_value_type_t *out_type);

/**
 * @brief 释放路径解析结果
 */
void lmjcore_free_path_parse_result(char *start_ptr, char **segments,
                                    size_t segment_count);

/**
 * @brief 检查事务是否超时
 *
 * @param start_time 事务开始时间
 * @param timeout 超时时间（秒）
 * @return bool true 表示已超时，false 表示未超时
 */
bool lmjcore_txn_check_timeout(time_t start_time, int timeout);

/**
 * @brief 获取当前时间（用于事务超时检查）
 *
 * @return time_t 当前时间
 */
time_t lmjcore_txn_get_start_time(void);

/**
 * @brief URL 解码字符串（将 %XX 编码转换为原始字符）
 *
 * @param src 源字符串（URL 编码）
 * @param src_len 源字符串长度
 * @param out_buf 输出缓冲区（需调用方分配足够空间）
 * @param out_buf_size 输出缓冲区大小
 * @return int 解码后的实际长度，失败返回 -1
 */
int url_decode(const char *src, size_t src_len, char *out_buf,
               size_t out_buf_size);

/**
 * @brief 事务管理：开启事务（支持外部传入或自动创建）
 *
 * @param hp 处理器参数
 * @param txn_out 输出事务指针
 * @param flags 事务标志（如 LMJCORE_TXN_READONLY）
 * @return LMJCORE_SUCCESS 成功，否则失败
 */
int handle_txn_begin(handle_params_t *hp, lmjcore_txn **txn_out, int flags);

/**
 * @brief 事务管理：提交/回滚事务（仅当自动管理时）
 *
 * @param hp 处理器参数
 * @param txn 事务指针
 * @param success 是否成功
 * @return LMJCORE_SUCCESS 成功，否则失败
 */
int handle_txn_end(handle_params_t *hp, lmjcore_txn *txn, int success);

#endif // HANDLE_UTILS_H
