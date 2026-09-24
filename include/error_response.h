// include/error_response.h - 统一错误响应便捷宏
//
// 实现全部位于 json_response.c（基于 cJSON），本头文件只提供语义化宏，
// 保证错误响应与其它 JSON 响应共用同一套序列化与转义逻辑。
#ifndef ERROR_RESPONSE_H
#define ERROR_RESPONSE_H

#include "error_codes.h"
#include "json_response.h"

// ==================== 错误响应构建宏 ====================
// 所有宏均以 return 结束，可直接用于处理器返回值位置。

/** @brief 参数无效 */
#define RETURN_ERROR_INVALID_PARAM(response)                                   \
  return json_response_error((response), HTTP_STATUS_BAD_REQUEST,              \
                             "Invalid parameters")

/** @brief 缺少必需参数 */
#define RETURN_ERROR_MISSING_PARAM(param_name, response)                       \
  return json_response_error((response), HTTP_STATUS_BAD_REQUEST,              \
                             "Missing " param_name " parameter")

/** @brief 指针格式无效 */
#define RETURN_ERROR_INVALID_PTR(response)                                     \
  return json_response_error((response), HTTP_STATUS_BAD_REQUEST,              \
                             "Invalid pointer format")

/** @brief 实体不存在 */
#define RETURN_ERROR_NOT_FOUND(entity_type, response)                          \
  return json_response_error((response), HTTP_STATUS_NOT_FOUND,                \
                             entity_type " not found")

/** @brief 成员不存在 */
#define RETURN_ERROR_MEMBER_NOT_FOUND(response)                                \
  return json_response_error((response), HTTP_STATUS_NOT_FOUND,                \
                             "Member not found")

/** @brief 内存分配失败 */
#define RETURN_ERROR_NO_MEMORY(response)                                       \
  return json_response_error((response), HTTP_STATUS_INTERNAL_SERVER_ERROR,    \
                             "Failed to allocate memory")

/** @brief 事务操作失败 */
#define RETURN_ERROR_TXN_FAILED(action, response)                              \
  return json_response_error((response), HTTP_STATUS_INTERNAL_SERVER_ERROR,    \
                             "Failed to " action " transaction")

/** @brief 请求体缺少 value 字段 */
#define RETURN_ERROR_BODY_PARSE(response)                                      \
  return json_response_error((response), HTTP_STATUS_BAD_REQUEST,              \
                             "Missing value in request body")

/** @brief 事务超时 */
#define RETURN_ERROR_TXN_TIMEOUT(response)                                     \
  return json_response_error((response), HTTP_STATUS_REQUEST_TIMEOUT,          \
                             "Transaction timeout")

#endif // ERROR_RESPONSE_H
