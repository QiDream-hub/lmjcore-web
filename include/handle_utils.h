// include/handle_utils.h - 处理器通用工具函数
#ifndef HANDLE_UTILS_H
#define HANDLE_UTILS_H

#include "cJSON.h"
#include "json_response.h"
#include "lmjcore.h"
#include "router.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

// ==================== 值类型标记 ====================

// 存储标签 / 编码类型（lmjcore_encode_value 的 type 参数）
#define LMJCORE_VALUE_TYPE_RAW 0x00  // 原始数据
#define LMJCORE_VALUE_TYPE_PTR 0x01  // 指针引用
#define LMJCORE_VALUE_TYPE_NULL 0x02 // 空值

// API 返回类型（读取时区分 obj/set；raw/null 数值与存储标签对齐）
typedef enum {
  VALUE_TYPE_RAW = LMJCORE_VALUE_TYPE_RAW,   // 0
  VALUE_TYPE_NULL = LMJCORE_VALUE_TYPE_NULL, // 2
  VALUE_TYPE_SET = 3,
  VALUE_TYPE_OBJECT = 4
} api_value_type_t;

// ==================== 事务管理宏（统一入口） ====================
//
// 约定：
//   - hp->txn 非空表示复用调用方事务（batch），txn_owned=0，处理器不提交/回滚；
//   - 处理器自行开启的事务 txn_owned=1，由 TXN_COMMIT 提交或由此回滚；
//   - 失败路径统一设置错误响应并跳转到 cleanup_label，因此每个使用这些宏的
//     处理器都必须提供唯一的 cleanup 标签；
//   - 在 cleanup 中调用 TXN_ABORT 作为兜底（幂等：提交/回滚后 txn_owned 清零）。

/** @brief 开始事务：复用外部事务或新建，失败时写 500 并跳转 cleanup */
#define TXN_BEGIN(txn, hp, flags, txn_owned, response, cleanup_label)          \
  do {                                                                         \
    if ((hp)->txn) {                                                           \
      (txn) = (hp)->txn;                                                       \
      (txn_owned) = 0;                                                         \
    } else {                                                                   \
      int _txn_rc = lmjcore_txn_begin((hp)->env, NULL, (flags), &(txn));       \
      if (_txn_rc != LMJCORE_SUCCESS || !(txn)) {                              \
        json_response_error((response), HTTP_STATUS_INTERNAL_SERVER_ERROR,     \
                            "Failed to begin transaction");                    \
        goto cleanup_label;                                                    \
      }                                                                        \
      (txn_owned) = 1;                                                         \
    }                                                                          \
  } while (0)

/** @brief 提交事务（仅自有事务），失败时回滚、写 500 并跳转 cleanup */
#define TXN_COMMIT(txn, txn_owned, response, cleanup_label)                    \
  do {                                                                         \
    if (txn_owned) {                                                           \
      int _txn_rc = lmjcore_txn_commit(txn);                                   \
      (txn_owned) = 0;                                                         \
      if (_txn_rc != LMJCORE_SUCCESS) {                                        \
        lmjcore_txn_abort(txn);                                                \
        json_response_error((response), HTTP_STATUS_INTERNAL_SERVER_ERROR,     \
                            "Failed to commit transaction");                   \
        goto cleanup_label;                                                    \
      }                                                                        \
    }                                                                          \
  } while (0)

/** @brief 回滚事务（仅自有事务，幂等） */
#define TXN_ABORT(txn, txn_owned)                                              \
  do {                                                                         \
    if ((txn_owned) && (txn)) {                                                \
      lmjcore_txn_abort(txn);                                                  \
      (txn_owned) = 0;                                                         \
    }                                                                          \
  } while (0)

/** @brief 检查事务超时：超时回滚、写 408 并跳转 cleanup */
#define TXN_CHECK_TIMEOUT(hp, txn, txn_owned, response, cleanup_label)         \
  do {                                                                         \
    if (lmjcore_txn_check_timeout((hp)->txn_start_time, (hp)->txn_timeout)) {  \
      TXN_ABORT(txn, txn_owned);                                               \
      json_response_error((response), HTTP_STATUS_REQUEST_TIMEOUT,             \
                          "Transaction timeout");                              \
      goto cleanup_label;                                                      \
    }                                                                          \
  } while (0)

// ==================== 处理器参数 ====================

typedef struct {
  route_params_t *params;
  lmjcore_env *env;
  lmjcore_txn *txn;        // 可选：外部传入的事务（批量操作时共享）
  router_t *router;        // 可选：路由器（批量操作时使用）
  char *body;              // 请求体
  size_t body_len;         // 请求体长度
  int txn_timeout;         // 事务超时时间（秒）
  time_t txn_start_time;   // 事务开始时间
  bool auto_manage_txn;    // 是否自动管理事务（默认 true，批量操作时设为 false）
  int query_max_depth;     // 链式查询深度上限（成员段数，0 表示用默认值）
  size_t max_value_bytes;  // 单个成员值/查询叶子值上限（字节，0 表示用默认值）
} handle_params_t;

// ==================== 取值/查询上限默认值（config.h 以此为单一来源） ====================

/** 链式查询最大深度（成员段数，不含起始指针段） */
#define HANDLE_DEFAULT_QUERY_MAX_DEPTH 64
/** 单个成员值 / 查询叶子值的最大字节数 */
#define HANDLE_DEFAULT_MAX_VALUE_BYTES 8192

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
 * @brief 将类型字符串解析为存储标签
 *
 * @param type_str 类型字符串（"raw" / "ref" / "null"）
 * @return int 对应的 LMJCORE_VALUE_TYPE_*，无法识别返回 -1
 */
int encode_type_from_string(const char *type_str);

/**
 * @brief 从请求体解析 {value, type}
 *
 * 要求 body 为对象，且 value、type 均为字符串，type 取值为 raw/ref/null。
 *
 * @param body 请求体 JSON 对象
 * @param out_value 输出：value 字符串
 * @param out_value_len 输出：value 长度
 * @param out_type 输出：LMJCORE_VALUE_TYPE_* 存储标签
 * @return int LMJCORE_SUCCESS 成功；LMJCORE_ERROR_INVALID_PARAM 缺失或非法
 */
int handle_parse_typed_value(const cJSON *body, const char **out_value,
                             size_t *out_value_len, int *out_type);

/**
 * @brief 编码值为存储格式
 *
 * @param value_str 输入值字符串
 * @param value_len 值长度
 * @param type 存储标签（LMJCORE_VALUE_TYPE_RAW / PTR / NULL）
 * @param out_buf 输出缓冲区
 * @param out_buf_size 输出缓冲区大小
 * @param out_len 输出实际长度
 * @return int 错误码
 *   - LMJCORE_ERROR_INVALID_PARAM: 未知类型，或 PTR 时 value_str 非合法指针
 */
int lmjcore_encode_value(const char *value_str, size_t value_len, int type,
                         uint8_t *out_buf, size_t out_buf_size,
                         size_t *out_len);

/**
 * @brief 读取成员值，缓冲区按需增长，不超过上限
 *
 * 用于替代"固定 4096 字节缓冲"的读法：核心层在缓冲不足时既不拷贝也不回传
 * 所需长度，因此这里以倍增方式重试，直到读成功或达到 max_bytes。
 *
 * @param txn 有效事务句柄
 * @param obj_ptr 目标对象指针
 * @param member_name 成员名（二进制安全）
 * @param member_name_len 成员名长度
 * @param max_bytes 值上限（0 表示使用 HANDLE_DEFAULT_MAX_VALUE_BYTES）
 * @param out_buf 输出：malloc 的缓冲区（调用方释放；失败时不设置）
 * @param out_len 输出：实际值长度
 * @return int 错误码
 *   - LMJCORE_ERROR_VALUE_TOO_LARGE: 值超过 max_bytes
 */
int lmjcore_obj_member_get_capped(lmjcore_txn *txn, const lmjcore_ptr obj_ptr,
                                  const char *member_name,
                                  size_t member_name_len, size_t max_bytes,
                                  uint8_t **out_buf, size_t *out_len);

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
 * @brief 将 API 值类型转换为字符串表示
 *
 * @param type API 值类型
 * @return const char* 类型字符串（静态常量，无需释放）
 */
const char *value_type_to_string(api_value_type_t type);

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

#endif // HANDLE_UTILS_H
