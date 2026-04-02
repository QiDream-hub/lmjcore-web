// src/error_codes.h - 扩展错误码定义
#ifndef LMJCORE_SERVER_ERRORS_H
#define LMJCORE_SERVER_ERRORS_H

#include "../thirdparty/LMJCore/core/include/lmjcore.h"

// ==================== LMJCore 网络壳扩展错误码 ====================
// 扩展范围：-32100 ~ -32199

// 事务相关 (-32100 ~ -32119)
#define LMJCORE_ERROR_TXN_TIMEOUT        -32101  // 事务超时
#define LMJCORE_ERROR_TXN_BEGIN_FAILED   -32102  // 事务开启失败
#define LMJCORE_ERROR_TXN_COMMIT_FAILED  -32103  // 事务提交失败
#define LMJCORE_ERROR_TXN_ABORT_FAILED   -32104  // 事务中止失败

// 路径解析相关 (-32120 ~ -32139)
#define LMJCORE_ERROR_PATH_PARSE         -32121  // 路径解析错误
#define LMJCORE_ERROR_PATH_TOO_DEEP      -32122  // 路径深度超限
#define LMJCORE_ERROR_PATH_INVALID_PTR   -32123  // 无效的起始指针
#define LMJCORE_ERROR_PATH_URL_DECODE    -32124  // URL 解码失败

// 类型相关 (-32140 ~ -32159)
#define LMJCORE_ERROR_SET_NOT_SUPPORTED  -32141  // 集合不支持链式解析
#define LMJCORE_ERROR_INVALID_TYPE       -32142  // 无效的值类型标记
#define LMJCORE_ERROR_TYPE_MISMATCH      -32143  // 类型不匹配

// HTTP 相关 (-32160 ~ -32179)
#define LMJCORE_ERROR_HTTP_PARSE         -32161  // HTTP 请求解析失败
#define LMJCORE_ERROR_HTTP_METHOD        -32162  // HTTP 方法不支持
#define LMJCORE_ERROR_HTTP_BODY_PARSE    -32163  // 请求体解析失败
#define LMJCORE_ERROR_HTTP_BODY_MISSING  -32164  // 缺少请求体
#define LMJCORE_ERROR_HTTP_PARAM_MISSING -32165  // 缺少必需参数

// 服务器相关 (-32180 ~ -32199)
#define LMJCORE_ERROR_SERVER_INIT        -32181  // 服务器初始化失败
#define LMJCORE_ERROR_SERVER_START       -32182  // 服务器启动失败
#define LMJCORE_ERROR_SERVER_STOP        -32183  // 服务器停止失败
#define LMJCORE_ERROR_SERVER_SOCKET      -32184  // Socket 操作失败
#define LMJCORE_ERROR_SERVER_BIND        -32185  // 端口绑定失败
#define LMJCORE_ERROR_SERVER_ACCEPT      -32186  // 接受连接失败

// ==================== HTTP 状态码映射 ====================
// 辅助宏，用于根据错误码返回 HTTP 状态码
#define HTTP_STATUS_OK 200
#define HTTP_STATUS_CREATED 201
#define HTTP_STATUS_NO_CONTENT 204
#define HTTP_STATUS_BAD_REQUEST 400
#define HTTP_STATUS_NOT_FOUND 404
#define HTTP_STATUS_METHOD_NOT_ALLOWED 405
#define HTTP_STATUS_REQUEST_TIMEOUT 408
#define HTTP_STATUS_INTERNAL_SERVER_ERROR 500

/**
 * @brief 将 LMJCore 错误码映射为 HTTP 状态码
 * 
 * @param error_code LMJCore 错误码
 * @return int HTTP 状态码
 */
static inline int lmjcore_error_to_http_status(int error_code) {
    switch (error_code) {
        case 0:  // LMJCORE_SUCCESS
            return HTTP_STATUS_OK;
        
        case LMJCORE_ERROR_ENTITY_NOT_FOUND:
        case LMJCORE_ERROR_MEMBER_NOT_FOUND:
            return HTTP_STATUS_NOT_FOUND;
        
        case LMJCORE_ERROR_INVALID_PARAM:
        case LMJCORE_ERROR_PATH_PARSE:
        case LMJCORE_ERROR_PATH_INVALID_PTR:
        case LMJCORE_ERROR_HTTP_BODY_PARSE:
        case LMJCORE_ERROR_HTTP_BODY_MISSING:
        case LMJCORE_ERROR_HTTP_PARAM_MISSING:
        case LMJCORE_ERROR_MEMBER_TOO_LONG:
        case LMJCORE_ERROR_SET_NOT_SUPPORTED:
        case LMJCORE_ERROR_INVALID_TYPE:
            return HTTP_STATUS_BAD_REQUEST;
        
        case LMJCORE_ERROR_READONLY_TXN:
            return HTTP_STATUS_METHOD_NOT_ALLOWED;
        
        case LMJCORE_ERROR_TXN_TIMEOUT:
            return HTTP_STATUS_REQUEST_TIMEOUT;
        
        case LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED:
        case LMJCORE_ERROR_SERVER_INIT:
        case LMJCORE_ERROR_SERVER_START:
        case LMJCORE_ERROR_SERVER_SOCKET:
        case LMJCORE_ERROR_SERVER_BIND:
        default:
            return HTTP_STATUS_INTERNAL_SERVER_ERROR;
    }
}

#endif // LMJCORE_SERVER_ERRORS_H