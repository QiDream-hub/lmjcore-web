// include/batch_handle.h - 批量操作处理器声明
#ifndef BATCH_HANDLE_H
#define BATCH_HANDLE_H

#include "handle_utils.h"
#include "router.h"

/**
 * @brief POST /batch - 批量执行多个操作（同一事务内）
 * 
 * 支持在同一个事务内执行多个操作，保证原子性。
 * - 写事务：不限制操作类型，但限制总时长（超时报错）
 * - 只读事务：检查操作中是否包含写操作，包含则报错
 */
int handle_batch_operations(void *params, void *cbdata);

#endif // BATCH_HANDLE_H
