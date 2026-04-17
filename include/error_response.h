// include/error_response.h - 统一错误响应构建
#ifndef ERROR_RESPONSE_H
#define ERROR_RESPONSE_H

#include "error_codes.h"
#include "http_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==================== 错误响应构建宏 ====================

/**
 * @brief 构建 JSON 错误响应
 *
 * @param status_code HTTP 状态码
 * @param error_msg 错误消息
 * @param response http_response_t 指针
 * @return int 0 (始终成功)
 */
static inline int build_error_response(int status_code, const char *error_msg,
                                       http_response_t *response) {
  if (!response) {
    return -1;
  }

  // 释放之前的响应体
  if (response->body) {
    free(response->body);
    response->body = NULL;
    response->body_len = 0;
  }

  response->status_code = status_code;

  // 构建 JSON 错误消息
  char json_buf[512];
  snprintf(json_buf, sizeof(json_buf), "{\"error\":\"%s\"}", error_msg);

  response->body = strdup(json_buf);
  if (!response->body) {
    response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
    response->body = strdup("{\"error\":\"Memory allocation failed\"}");
    response->body_len = strlen(response->body);
    return -1;
  }

  response->body_len = strlen(response->body);
  return 0;
}

/**
 * @brief 根据 LMJCore 错误码构建错误响应
 *
 * @param error_code LMJCore 错误码
 * @param response http_response_t 指针
 * @return int 0 (始终成功)
 */
static inline int build_lmjcore_error_response(int error_code,
                                               http_response_t *response) {
  if (!response) {
    return -1;
  }

  int http_status = lmjcore_error_to_http_status(error_code);
  const char *error_msg = lmjcore_strerror(error_code);

  return build_error_response(http_status, error_msg, response);
}

/**
 * @brief 构建成功响应
 *
 * @param status_code HTTP 状态码
 * @param json_body JSON 响应体
 * @param response http_response_t 指针
 * @return int 0 成功，-1 失败
 */
static inline int build_success_response(int status_code, const char *json_body,
                                         http_response_t *response) {
  if (!response || !json_body) {
    return -1;
  }

  // 释放之前的响应体
  if (response->body) {
    free(response->body);
    response->body = NULL;
    response->body_len = 0;
  }

  response->status_code = status_code;
  response->body = strdup(json_body);

  if (!response->body) {
    response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
    response->body = strdup("{\"error\":\"Memory allocation failed\"}");
    response->body_len = strlen(response->body);
    return -1;
  }

  response->body_len = strlen(response->body);
  return 0;
}

/**
 * @brief 便捷宏：返回参数无效错误
 */
#define RETURN_ERROR_INVALID_PARAM(response)                                   \
  return build_error_response(HTTP_STATUS_BAD_REQUEST, "Invalid parameters",   \
                              response)

/**
 * @brief 便捷宏：返回缺少参数错误
 */
#define RETURN_ERROR_MISSING_PARAM(param_name, response)                       \
  return build_error_response(HTTP_STATUS_BAD_REQUEST,                         \
                              "Missing " param_name " parameter", response)

/**
 * @brief 便捷宏：返回指针格式无效错误
 */
#define RETURN_ERROR_INVALID_PTR(response)                                     \
  return build_error_response(HTTP_STATUS_BAD_REQUEST,                         \
                              "Invalid pointer format", response)

/**
 * @brief 便捷宏：返回实体不存在错误
 */
#define RETURN_ERROR_NOT_FOUND(entity_type, response)                          \
  return build_error_response(HTTP_STATUS_NOT_FOUND,                           \
                              entity_type " not found", response)

/**
 * @brief 便捷宏：返回成员不存在错误
 */
#define RETURN_ERROR_MEMBER_NOT_FOUND(response)                                \
  return build_error_response(HTTP_STATUS_NOT_FOUND, "Member not found",       \
                              response)

/**
 * @brief 便捷宏：返回内存分配失败错误
 */
#define RETURN_ERROR_NO_MEMORY(response)                                       \
  return build_error_response(HTTP_STATUS_INTERNAL_SERVER_ERROR,               \
                              "Failed to allocate memory", response)

/**
 * @brief 便捷宏：返回事务错误
 */
#define RETURN_ERROR_TXN_FAILED(action, response)                              \
  return build_error_response(HTTP_STATUS_INTERNAL_SERVER_ERROR,               \
                              "Failed to " action " transaction", response)

/**
 * @brief 便捷宏：返回请求体解析错误
 */
#define RETURN_ERROR_BODY_PARSE(response)                                      \
  return build_error_response(HTTP_STATUS_BAD_REQUEST,                         \
                              "Missing value in request body", response)

/**
 * @brief 便捷宏：返回事务超时错误
 */
#define RETURN_ERROR_TXN_TIMEOUT(response)                                     \
  return build_error_response(HTTP_STATUS_REQUEST_TIMEOUT,                     \
                              "Transaction timeout", response)

#endif // ERROR_RESPONSE_H
