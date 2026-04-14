// include/lmjcore_handle.h - LMJCore HTTP 处理器声明
// 模块化设计：具体实现分散在 src/handlers/ 目录下的各个文件中
#ifndef LMJCORE_HANDLE_H
#define LMJCORE_HANDLE_H

#include "handle_utils.h"
#include "../thirdparty/URLRouter/include/router.h"

// ==================== 对象处理器 ====================
// 实现文件：src/handlers/obj_handle.c

/**
 * @brief POST /obj - 创建空对象
 */
int handle_obj_create(void *params, void *cbdata);

/**
 * @brief GET /obj/{ptr} - 获取完整对象
 */
int handle_obj_get(void *params, void *cbdata);

/**
 * @brief GET /obj/{ptr}/{member} - 获取成员值
 */
int handle_obj_member_get(void *params, void *cbdata);

/**
 * @brief PUT /obj/{ptr}/{member} - 设置成员值
 */
int handle_obj_member_put(void *params, void *cbdata);

/**
 * @brief DELETE /obj/{ptr}/{member} - 删除成员
 */
int handle_obj_member_del(void *params, void *cbdata);

/**
 * @brief GET /obj/query - 链式查询
 */
int handle_obj_query(void *params, void *cbdata);

// ==================== 集合处理器 ====================
// 实现文件：src/handlers/set_handle.c

/**
 * @brief POST /set - 创建空集合
 */
int handle_set_create(void *params, void *cbdata);

/**
 * @brief GET /set/{ptr} - 获取完整集合
 */
int handle_set_get(void *params, void *cbdata);

/**
 * @brief POST /set/{ptr}/elements - 添加元素
 */
int handle_set_add(void *params, void *cbdata);

/**
 * @brief DELETE /set/{ptr}/elements - 删除元素
 */
int handle_set_remove(void *params, void *cbdata);

// ==================== 工具处理器 ====================
// 实现文件：src/handlers/utils_handle.c

/**
 * @brief GET /ptr/{ptr}/exist - 检查指针是否存在
 */
int handle_ptr_exist(void *params, void *cbdata);

/**
 * @brief GET /health - 健康检查
 */
int handle_health(void *params, void *cbdata);

#endif // LMJCORE_HANDLE_H
