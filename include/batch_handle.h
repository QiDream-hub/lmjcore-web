// include/batch_handle.h - 批量操作处理器声明
#ifndef BATCH_HANDLE_H
#define BATCH_HANDLE_H

#include "handle_utils.h"
#include "router.h"

/**
 * @brief GET /batch - 只读批量执行多个操作（同一只读事务内）
 *
 * 支持在同一个只读事务内执行多个只读操作，保证一致性。
 * - 仅允许 GET 操作
 * - 如包含 PUT/POST/DELETE 则返回错误
 */
int handle_batch_get(void *params, void *cbdata);

/**
 * @brief POST /batch - 写操作批量执行多个操作（同一写事务内）
 *
 * 支持在同一个写事务内执行多个操作，保证原子性。
 * - 允许所有操作类型（GET/PUT/POST/DELETE）
 * - 限制总时长（超时报错）
 */
int handle_batch_post(void *params, void *cbdata);

#endif // BATCH_HANDLE_H
