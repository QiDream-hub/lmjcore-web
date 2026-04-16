// src/routes.c - 路由注册
#include "routes.h"
#include "lmjcore_handle.h"
#include <stdio.h>

/**
 * @brief 注册所有 API 路由
 *
 * @param router 路由器实例
 * @return int 0 成功，-1 失败
 */
int register_all_routes(router_t *router) {
  if (!router) {
    return -1;
  }

  // ==================== 对象操作 ====================

  // POST /obj - 创建空对象
  if (router_register(router, HTTP_POST, "/$'obj'", handle_obj_create, NULL) !=
      0) {
    fprintf(stderr, "Failed to register POST /obj\n");
    return -1;
  }

  // GET /obj/{ptr} - 获取完整对象
  if (router_register(router, HTTP_GET, "/$'obj'/${}", handle_obj_get, NULL) !=
      0) {
    fprintf(stderr, "Failed to register GET /obj/{ptr}\n");
    return -1;
  }

  // GET /obj/{ptr}/{member} - 获取成员值
  if (router_register(router, HTTP_GET, "/$'obj'/${}/${}",
                      handle_obj_member_get, NULL) != 0) {
    fprintf(stderr, "Failed to register GET /obj/{ptr}/{member}\n");
    return -1;
  }

  // PUT /obj/{ptr}/{member} - 设置成员值
  if (router_register(router, HTTP_PUT, "/$'obj'/${}/${}",
                      handle_obj_member_put, NULL) != 0) {
    fprintf(stderr, "Failed to register PUT /obj/{ptr}/{member}\n");
    return -1;
  }

  // DELETE /obj/{ptr}/{member} - 删除成员
  if (router_register(router, HTTP_DELETE, "/$'obj'/${}/${}",
                      handle_obj_member_del, NULL) != 0) {
    fprintf(stderr, "Failed to register DELETE /obj/{ptr}/{member}\n");
    return -1;
  }

  // GET /obj/query - 链式查询
  if (router_register(router, HTTP_GET, "/$'obj'/$'query?path='${}",
                      handle_obj_query, NULL) != 0) {
    fprintf(stderr, "Failed to register GET /obj/query\n");
    return -1;
  }

  // ==================== 集合操作 ====================

  // POST /set - 创建空集合
  if (router_register(router, HTTP_POST, "/$'set'", handle_set_create, NULL) !=
      0) {
    fprintf(stderr, "Failed to register POST /set\n");
    return -1;
  }

  // GET /set/{ptr} - 获取完整集合
  if (router_register(router, HTTP_GET, "/$'set'/${}", handle_set_get, NULL) !=
      0) {
    fprintf(stderr, "Failed to register GET /set/{ptr}\n");
    return -1;
  }

  // POST /set/{ptr}/elements - 添加元素
  if (router_register(router, HTTP_POST, "/$'set'/${}/$'elements'",
                      handle_set_add, NULL) != 0) {
    fprintf(stderr, "Failed to register POST /set/{ptr}/elements\n");
    return -1;
  }

  // DELETE /set/{ptr}/elements - 删除元素
  if (router_register(router, HTTP_DELETE, "/$'set'/${}/$'elements'",
                      handle_set_remove, NULL) != 0) {
    fprintf(stderr, "Failed to register DELETE /set/{ptr}/elements\n");
    return -1;
  }

  // ==================== 工具操作 ====================

  // GET /ptr/{ptr}/exist - 检查指针是否存在
  if (router_register(router, HTTP_GET, "/$'ptr'/${}/$'exist'",
                      handle_ptr_exist, NULL) != 0) {
    fprintf(stderr, "Failed to register GET /ptr/{ptr}/exist\n");
    return -1;
  }

  // GET /health - 健康检查
  if (router_register(router, HTTP_GET, "/$'health'", handle_health, NULL) !=
      0) {
    fprintf(stderr, "Failed to register GET /health\n");
    return -1;
  }

  printf("Routes registered successfully:\n");
  printf("  POST   /obj\n");
  printf("  GET    /obj/{ptr}\n");
  printf("  GET    /obj/{ptr}/{member}\n");
  printf("  PUT    /obj/{ptr}/{member}\n");
  printf("  DELETE /obj/{ptr}/{member}\n");
  printf("  GET    /obj/query\n");
  printf("  POST   /set\n");
  printf("  GET    /set/{ptr}\n");
  printf("  POST   /set/{ptr}/elements\n");
  printf("  DELETE /set/{ptr}/elements\n");
  printf("  GET    /ptr/{ptr}/exist\n");
  printf("  GET    /health\n");

  return 0;
}