// include/json_response.h - 统一 JSON 响应构建层（基于 cJSON）
//
// 设计目标：
//   1. 处理器只负责构建 cJSON 响应模型，响应体的序列化统一交给发送层
//      （http_build_response）完成，处理器不再手写 snprintf/字符串拼接；
//   2. 字符串（成员名/成员值/错误信息）统一由 cJSON 转义，避免产生非法 JSON；
//   3. 响应体长度不再受固定栈缓冲区限制，大成员值不会被静默截断；
//   4. 响应模型（ptr/members/elements/count/error/exist/health）集中定义，便于统一演进。
//
// 所有权约定：
//   - json_new_* 返回新分配的 cJSON 文档，调用方负责 cJSON_Delete 或交给
//     json_response_set（后者接管所有权，随响应一并释放）。
//   - json_array_append 接管 item 所有权（失败时自动释放）。
#ifndef JSON_RESPONSE_H
#define JSON_RESPONSE_H

#include "cJSON.h"
#include "error_codes.h"
#include "http_parser.h"

#include <stdbool.h>
#include <stddef.h>

// ==================== 响应输出 ====================

/**
 * @brief 设置响应状态码并接管 cJSON 文档所有权（不在此处序列化）
 *
 * 响应体在发送阶段由 http_build_response 统一序列化，避免处理器层反复
 * 序列化/解析。
 *
 * @param response 响应结构
 * @param status_code HTTP 状态码
 * @param root cJSON 文档（可为 NULL，此时记录内存错误状态）
 * @return int 0 成功，-1 失败
 */
int json_response_set(http_response_t *response, int status_code, cJSON *root);

/**
 * @brief 设置错误响应 {"error":"..."}（消息自动 JSON 转义，不截断）
 *
 * @param response 响应结构
 * @param status_code HTTP 状态码
 * @param fmt printf 风格格式串
 * @return int 0 成功，-1 失败
 */
int json_response_error(http_response_t *response, int status_code,
                        const char *fmt, ...);

/**
 * @brief 根据 LMJCore 错误码设置错误响应（状态码与消息自动映射）
 */
int json_response_lmjcore_error(http_response_t *response, int error_code);

/**
 * @brief 设置成功响应 {"success":true}
 */
int json_response_success(http_response_t *response, int status_code);

// ==================== 响应模型构建（失败返回 NULL） ====================

/** @brief {"ptr":"<ptr>"} */
cJSON *json_new_ptr(const char *ptr_str);

/** @brief {"ptr":"<ptr>","<count_key>":<count>} */
cJSON *json_new_counted_ptr(const char *ptr_str, const char *count_key,
                            size_t count);

/**
 * @brief {"ptr":"<ptr>","<array_key>":[...]}，并把数组句柄写入 out_array
 *
 * 调用方随后用 json_array_append 追加条目，最后用 json_entity_set_count 收尾。
 */
cJSON *json_new_entity(const char *ptr_str, const char *array_key,
                       cJSON **out_array);

/** @brief 为 json_new_entity 的结果追加 "count" 字段 */
int json_entity_set_count(cJSON *root, size_t count);

/** @brief 向数组追加条目（接管 item 所有权；array/item 为 NULL 时返回 false） */
bool json_array_append(cJSON *array, cJSON *item);

/** @brief {"value":"<value>","type":"<type>"} */
cJSON *json_new_value(const char *value, const char *type);

/**
 * @brief {"<key>":"<key_value>","value":"<value>","type":"<type>"}
 *
 * 用于成员条目（key="name"）或成员查询结果（key="member"）。
 * key 为 "value" 时退化为 json_new_value，避免写入重复键。
 */
cJSON *json_new_value_entry(const char *key, const char *key_value,
                            const char *value, const char *type);

/** @brief {"exist":<bool>} 或 {"exist":true,"type":"<type>"} */
cJSON *json_new_exist(bool exists, const char *type);

/** @brief {"status":"<status>","uptime":<uptime>} */
cJSON *json_new_health(const char *status, long uptime);

// ==================== 常用错误响应 ====================

/**
 * @brief 设置值超限响应 413 {"error":"Value too large","limit":N}
 *
 * 供成员读取与链式查询共用，避免各自拼装。
 */
int json_response_value_too_large(http_response_t *response, size_t limit);

#endif // JSON_RESPONSE_H
