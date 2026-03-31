#ifndef LMJCORE_WEB_H
#define LMJCORE_WEB_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// 错误码扩展
#define LMJCORE_WEB_SUCCESS 0
#define LMJCORE_WEB_ERROR_INVALID_PATH -33000
#define LMJCORE_WEB_ERROR_INVALID_PTR -33001
#define LMJCORE_WEB_ERROR_PATH_TOO_DEEP -33002
#define LMJCORE_WEB_ERROR_JSON_PARSE -33003
#define LMJCORE_WEB_ERROR_TXN_NOT_FOUND -33004

// 配置结构
typedef struct {
    const char* db_path;        // 数据库路径
    size_t map_size;            // 内存映射大小
    unsigned int env_flags;     // 环境标志
    const char* bind_host;      // 绑定地址
    uint16_t bind_port;         // 绑定端口
    int max_path_depth;         // 最大路径深度
    bool enable_audit;          // 启用审计
} lmjcore_web_config_t;

// 响应结构
typedef struct {
    int code;                   // 错误码
    char* message;              // 错误消息
    char* data;                 // JSON 数据
    size_t data_len;            // 数据长度
} lmjcore_web_response_t;

// 初始化
int lmjcore_web_init(const lmjcore_web_config_t* config);

// 启动服务器
int lmjcore_web_start(void);

// 停止服务器
int lmjcore_web_stop(void);

// 清理资源
int lmjcore_web_cleanup(void);

// 指针生成
int lmjcore_web_generate_ptr(uint8_t type, char* out_hex, size_t out_size);

#endif // LMJCORE_WEB_H