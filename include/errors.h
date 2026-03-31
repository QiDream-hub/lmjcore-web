#ifndef LMJCORE_WEB_ERRORS_H
#define LMJCORE_WEB_ERRORS_H

// LMJCore Web 特定错误码
#define LMJCORE_WEB_ERROR_BASE          -1000

#define LMJCORE_WEB_ERROR_INVALID_PATH  (LMJCORE_WEB_ERROR_BASE - 1)
#define LMJCORE_WEB_ERROR_TOO_DEEP      (LMJCORE_WEB_ERROR_BASE - 2)
#define LMJCORE_WEB_ERROR_INVALID_PTR   (LMJCORE_WEB_ERROR_BASE - 3)
#define LMJCORE_WEB_ERROR_PATH_EMPTY    (LMJCORE_WEB_ERROR_BASE - 4)

// 错误信息映射
static inline const char* lmjcore_web_strerror(int code) {
    switch (code) {
        case LMJCORE_WEB_ERROR_INVALID_PATH:
            return "Invalid path format";
        case LMJCORE_WEB_ERROR_TOO_DEEP:
            return "Path too deep (max 32 segments)";
        case LMJCORE_WEB_ERROR_INVALID_PTR:
            return "Invalid pointer format";
        case LMJCORE_WEB_ERROR_PATH_EMPTY:
            return "Empty path";
        default:
            return "Unknown error";
    }
}

#endif // LMJCORE_WEB_ERRORS_H