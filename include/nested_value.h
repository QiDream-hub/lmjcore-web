// include/nested_value.h - 嵌套值统一创建工具
// 供 obj/init、set/init 等处理器共用：把任意 JSON 节点写入当前事务，
// 标量一律按原始数据（null 存空值）存储，对象/数组递归创建嵌套 obj/set 实体并返回指针引用。
#ifndef NESTED_VALUE_H
#define NESTED_VALUE_H

#include "cJSON.h"
#include "lmjcore.h"

#include <stddef.h>
#include <stdint.h>

// JSON 嵌套深度上限（防止 C 递归过深）
#define LMJCORE_NEST_DEPTH_MAX 64

/**
 * @brief 校验成员名是否合法（非空且不超过引擎上限）
 *
 * @param name 成员名
 * @param name_len 成员名长度
 * @param err_buf 错误消息输出缓冲区（可为 NULL）
 * @param err_buf_size err_buf 大小
 * @return LMJCORE_SUCCESS 合法；LMJCORE_ERROR_INVALID_PARAM 非法（同时写出原因）
 */
int lmjcore_nested_member_name_check(const char *name, size_t name_len,
                                     char *err_buf, size_t err_buf_size);

/**
 * @brief 将任意 JSON 节点转换为可存储的编码值（同一事务内完成）
 *
 * 类型映射（init 不做类型推断，标量一律 raw）：
 *   - JSON object -> 新建嵌套 obj，逐个递归填充成员，返回 REF(该 obj 指针)
 *   - JSON array  -> 新建嵌套 set，逐个递归添加元素，返回 REF(该 set 指针)
 *   - JSON null   -> null
 *   - JSON string -> raw（原样存储，即使内容形似指针）
 *   - JSON number -> raw（按其 JSON 数字文本存储，如 21 -> "21"）
 *   - JSON true/false -> raw（"true"/"false"）
 *
 * 空对象/空数组允许（填入空实体的指针）。
 * 任一子节点失败时，整个事务内已创建的嵌套实体均随事务回滚；
 * err_buf 会写出从调用位置到失败点的路径链（如 "member 'ip': element [0]: member 'x': ..."）。
 *
 * @param txn 有效的写事务
 * @param node 待编码的 JSON 节点（只读）
 * @param depth 当前嵌套深度（顶层调用传 0，内部递归 +1）
 * @param err_buf 错误消息输出缓冲区（可为 NULL）
 * @param err_buf_size err_buf 大小
 * @param out_enc 输出：malloc 的编码值缓冲区（调用方负责 free）
 * @param out_enc_len 输出：编码值长度
 * @return int 错误码（LMJCORE_SUCCESS 成功）
 */
int lmjcore_json_encode_value(lmjcore_txn *txn, const cJSON *node, int depth,
                              char *err_buf, size_t err_buf_size,
                              uint8_t **out_enc, size_t *out_enc_len);

#endif // NESTED_VALUE_H
