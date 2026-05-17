// include/error_response.h - 统一错误响应构建
#ifndef ERROR_RESPONSE_H
#define ERROR_RESPONSE_H

#include "error_codes.h"
#include "http_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==================== JSON 转义工具函数 ====================

/**
 * @brief 将字符串转义为合法的 JSON 字符串
 * 
 * 处理特殊字符：双引号、反斜杠、换行、回车、制表符
 * 
 * @param src 源字符串
 * @param src_len 源字符串长度
 * @param out_buf 输出缓冲区
 * @param out_buf_size 输出缓冲区大小
 * @return int 转义后的长度，失败返回 -1
 */
static inline int json_escape_string(const char *src, size_t src_len,
                                     char *out_buf, size_t out_buf_size) {
  if (!src || !out_buf || out_buf_size == 0) {
    return -1;
  }

  size_t j = 0;
  for (size_t i = 0; i < src_len && src[i] != '\0'; i++) {
    char c = src[i];
    
    // 检查剩余空间（最多需要 6 字节：\uXXXX）
    if (j + 6 >= out_buf_size) {
      return -1;
    }

    switch (c) {
    case '"':
      out_buf[j++] = '\\';
      out_buf[j++] = '"';
      break;
    case '\\':
      out_buf[j++] = '\\';
      out_buf[j++] = '\\';
      break;
    case '\n':
      out_buf[j++] = '\\';
      out_buf[j++] = 'n';
      break;
    case '\r':
      out_buf[j++] = '\\';
      out_buf[j++] = 'r';
      break;
    case '\t':
      out_buf[j++] = '\\';
      out_buf[j++] = 't';
      break;
    case '\b':
      out_buf[j++] = '\\';
      out_buf[j++] = 'b';
      break;
    case '\f':
      out_buf[j++] = '\\';
      out_buf[j++] = 'f';
      break;
    default:
      // 控制字符使用 \uXXXX 格式
      if ((unsigned char)c < 0x20) {
        snprintf(out_buf + j, out_buf_size - j, "\\u%04x", (unsigned char)c);
        j += 6;
      } else {
        out_buf[j++] = c;
      }
      break;
    }
  }

  out_buf[j] = '\0';
  return (int)j;
}

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

  // 限制错误消息长度，防止转义后溢出
  // 转义可能使字符串扩展最多 6 倍（控制字符 -> \uXXXX）
  // 为安全起见，限制原始消息长度
  size_t msg_len = error_msg ? strlen(error_msg) : 0;
  if (msg_len > 100) {
    msg_len = 100;  // 限制消息长度
  }
  
  // 转义后的消息缓冲区（最多 6 倍扩展 + 安全余量）
  char escaped_msg[512];
  int escaped_len = json_escape_string(error_msg ? error_msg : "", msg_len,
                                        escaped_msg, sizeof(escaped_msg));
  if (escaped_len < 0) {
    // 转义失败，使用默认消息
    response->body = strdup("{\"error\":\"Error message format failed\"}");
    if (!response->body) {
      response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
      response->body = strdup("{\"error\":\"Memory allocation failed\"}");
      response->body_len = strlen(response->body);
      return -1;
    }
    response->body_len = strlen(response->body);
    return 0;
  }

  // 构建 JSON 错误消息
  response->body = (char *)malloc(escaped_len + 32);
  if (!response->body) {
    response->status_code = HTTP_STATUS_INTERNAL_SERVER_ERROR;
    response->body = strdup("{\"error\":\"Memory allocation failed\"}");
    response->body_len = strlen(response->body);
    return -1;
  }
  
  snprintf(response->body, escaped_len + 32, "{\"error\":\"%s\"}", escaped_msg);
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
