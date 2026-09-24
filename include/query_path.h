// include/query_path.h - 链式查询路径的解析与深度访问
//
// 定位：本模块只做「深度访问」——沿对象图走一条确定的路径，返回单个叶子值。
// 多路径的组合查询由 GET /batch 负责，本模块不提供模式匹配 / 通配符 / fan-out。
//
// 路径语法：
//   <34 位十六进制指针>.<成员名>[.<成员名>...]
//   - 根段必须是对象指针（01 开头）；集合指针（02）返回 SET_NOT_SUPPORTED
//   - 成员名中的字面 '.' 写作 "%2E"（先按 '.' 切分，再逐段 URL 解码）
//   - 空段视为语法错误（不再静默丢弃），至少需要 1 个成员段
//
// 成本：每段一次 lmjcore_obj_member_get，全程 O(深度)，不做任何枚举。
#ifndef QUERY_PATH_H
#define QUERY_PATH_H

#include "handle_utils.h"
#include "lmjcore.h"

#include <stddef.h>

// ==================== 默认上限 ====================

/** 成员段数上限（不含根指针段） */
#define QUERY_MAX_DEPTH_DEFAULT 64
/** 叶子值字节上限 */
#define QUERY_MAX_VALUE_BYTES_DEFAULT 8192

// ==================== 查询选项 ====================

typedef struct {
  size_t max_depth;       // 成员段数上限（0 表示使用默认值）
  size_t max_value_bytes; // 叶子值上限（0 表示使用默认值）
} query_options_t;

// ==================== 失败位置 ====================

/**
 * 失败位置，用于在错误响应中给出可定位的信息：
 *   {"error":"Member not found","at":"01ab…/user/profile","segment":2}
 */
typedef struct {
  char *at;       // 失败位置 "<ptr>/<seg>/<seg>"（malloc，可为 NULL）
  size_t segment; // 失败段索引（0 = 根指针段）

  // 中间值不是引用时，记录其实际存储类型（依据 value[0] 类型标记）
  bool has_found_type;
  api_value_type_t found_type;
} query_error_t;

// ==================== API ====================

/**
 * @brief 用给定上限初始化选项，0 表示回落到默认值
 */
void query_options_init(query_options_t *options, size_t max_depth,
                        size_t max_value_bytes);

/**
 * @brief 执行深度访问查询
 *
 * @param txn 只读事务（批量操作时复用调用方的共享事务）
 * @param path 查询路径字符串
 * @param options 选项（可为 NULL，使用默认值）
 * @param out_value 输出：叶子值字符串（malloc，调用方释放）
 * @param out_type 输出：叶子值类型
 * @param err 输出：失败位置（可为 NULL；成功时不写入）
 * @return int LMJCORE_SUCCESS 或错误码
 *   - LMJCORE_ERROR_PATH_PARSE: 路径语法错误（含空段、缺少成员段）
 *   - LMJCORE_ERROR_PATH_INVALID_PTR: 根段不是合法指针
 *   - LMJCORE_ERROR_PATH_TOO_DEEP: 超过深度上限
 *   - LMJCORE_ERROR_PATH_URL_DECODE: 段 URL 解码失败
 *   - LMJCORE_ERROR_SET_NOT_SUPPORTED: 根/中间目标是集合
 *   - LMJCORE_ERROR_TYPE_MISMATCH: 中间值不是指针引用
 *   - LMJCORE_ERROR_MEMBER_NOT_FOUND / ENTITY_NOT_FOUND
 *   - LMJCORE_ERROR_VALUE_TOO_LARGE: 叶子值超过上限
 */
int query_path_execute(lmjcore_txn *txn, const char *path,
                       const query_options_t *options, char **out_value,
                       api_value_type_t *out_type, query_error_t *err);

/** @brief 释放 query_error_t 持有的内存并清零 */
void query_error_free(query_error_t *err);

#endif // QUERY_PATH_H
