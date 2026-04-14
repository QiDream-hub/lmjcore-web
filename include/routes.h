#ifndef ROUTES_H
#define ROUTES_H

#include "../thirdparty/URLRouter/include/router.h"
/**
 * @brief 注册所有 API 路由
 *
 * @param router 路由器实例
 * @return int 0 成功，-1 失败
 */
int register_all_routes(router_t *router);

#endif