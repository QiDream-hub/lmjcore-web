// include/lmjcore_handle.h
#ifndef LMJCORE_HANDLE_H
#define LMJCORE_HANDLE_H

#include "../thirdparty/LMJCore/core/include/lmjcore.h"
#include "../thirdparty/URLRouer/router.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

// ==================== 值类型定义 ====================

// 值类型标记（存储格式）
#define LMJCORE_VALUE_TYPE_RAW 0x00  // 原始数据
#define LMJCORE_VALUE_TYPE_PTR 0x01  // 指针引用
#define LMJCORE_VALUE_TYPE_NULL 0x02 // 空值

// API 返回的类型标识
typedef enum {
  VALUE_TYPE_RAW = 0,
  VALUE_TYPE_REF = 1,
  VALUE_TYPE_NULL = 2,
  VALUE_TYPE_SET = 3,   // 集合类型
  VALUE_TYPE_OBJECT = 4 // 对象类型
} api_value_type_t;

// ==================== 响应结构体 ====================

// 成员值响应
typedef struct {
  char *member_name;     // 成员名称
  api_value_type_t type; // 值类型
  char *value;           // 值字符串（原始数据或指针字符串）
  size_t value_len;      // 值长度
} member_response_t;

// 完整对象响应
typedef struct {
  char *ptr;                  // 对象指针字符串
  member_response_t *members; // 成员数组
  size_t member_count;        // 成员数量
} object_response_t;

// 集合元素响应
typedef struct {
  api_value_type_t type; // 元素类型
  char *value;           // 元素值
  size_t value_len;      // 值长度
} element_response_t;

// 完整集合响应
typedef struct {
  char *ptr;                    // 集合指针字符串
  element_response_t *elements; // 元素数组
  size_t element_count;         // 元素数量
} set_response_t;

// 链式查询响应
typedef struct {
  char *path;            // 查询路径
  api_value_type_t type; // 值类型
  char *value;           // 值
  size_t value_len;      // 值长度
} query_response_t;

// 创建响应
typedef struct {
  char *ptr; // 新创建的实体指针
} create_response_t;

// 处理器参数
typedef struct {
  route_params_t *params;
  char *body;      // 请求体
  size_t body_len; // 请求体长度
  lmjcore_env *env;
} handle_params;

// ==================== 对象处理器 ====================

/**
 * @brief POST /obj - 创建空对象
 *
 * 请求体: 无
 * 响应: {"ptr": "01abc123..."}
 */
int handle_obj_create(void *params, void *cbdata);

/**
 * @brief GET /obj/{ptr} - 获取完整对象
 *
 * 响应: {"ptr": "...", "members": [...], "count": N}
 */
int handle_obj_get(void *params, void *cbdata);

/**
 * @brief GET /obj/{ptr}/{member} - 获取成员值
 *
 * 响应: {"member": "name", "value": "...", "type": "raw|ref|null"}
 */
int handle_obj_member_get(void *params, void *cbdata);

/**
 * @brief PUT /obj/{ptr}/{member} - 设置成员值
 *
 * 请求体: {"value": "..."}
 * 响应: {"success": true}
 */
int handle_obj_member_put(void *params, void *cbdata);

/**
 * @brief DELETE /obj/{ptr}/{member} - 删除成员
 *
 * 响应: {"success": true}
 */
int handle_obj_member_del(void *params, void *cbdata);

/**
 * @brief GET /obj/query - 链式查询
 *
 * 查询参数: ?path=01abc123.user.profile.name
 * 响应: {"path": "...", "value": "...", "type": "raw|ref"}
 */
int handle_obj_query(void *params, void *cbdata);

// ==================== 集合处理器 ====================

/**
 * @brief POST /set - 创建空集合
 *
 * 响应: {"ptr": "02def456..."}
 */
int handle_set_create(void *params, void *cbdata);

/**
 * @brief GET /set/{ptr} - 获取完整集合
 *
 * 响应: {"ptr": "...", "elements": [...], "count": N}
 */
int handle_set_get(void *params, void *cbdata);

/**
 * @brief POST /set/{ptr}/elements - 添加元素
 *
 * 请求体: {"value": "..."}
 * 响应: {"success": true}
 */
int handle_set_add(void *params, void *cbdata);

/**
 * @brief DELETE /set/{ptr}/elements - 删除元素
 *
 * 请求体: {"value": "..."}
 * 响应: {"success": true}
 */
int handle_set_remove(void *params, void *cbdata);

// ==================== 工具处理器 ====================

/**
 * @brief GET /ptr/{ptr}/exist - 检查指针是否存在
 *
 * 响应: {"exist": true, "type": "object|set"}
 */
int handle_ptr_exist(void *params, void *cbdata);

/**
 * @brief GET /health - 健康检查
 *
 * 响应: {"status": "ok", "uptime": 12345}
 */
int handle_health(void *params, void *cbdata);

// ==================== 工具函数 ====================

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
 * 格式: "01abc123.user.profile.name"
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
 * 根据值内容自动判断类型：
 * - 如果是 34 位十六进制且以 01 或 02 开头 -> 指针类型
 * - 如果是 null -> 空值类型
 * - 其他 -> 原始数据类型
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
 * @brief 释放成员响应数组
 *
 * @param members 成员数组
 * @param count 成员数量
 */
void lmjcore_free_member_responses(member_response_t *members, size_t count);

/**
 * @brief 释放元素响应数组
 *
 * @param elements 元素数组
 * @param count 元素数量
 */
void lmjcore_free_element_responses(element_response_t *elements, size_t count);

/**
 * @brief 释放对象响应
 *
 * @param response 对象响应
 */
void lmjcore_free_object_response(object_response_t *response);

/**
 * @brief 释放集合响应
 *
 * @param response 集合响应
 */
void lmjcore_free_set_response(set_response_t *response);

/**
 * @brief 释放路径解析结果
 *
 * @param start_ptr 起始指针字符串
 * @param segments 路径段数组
 * @param segment_count 路径段数量
 */
void lmjcore_free_path_parse_result(char *start_ptr, char **segments,
                                    size_t segment_count);

#endif // LMJCORE_HANDLE_H