// src/routes.c - 路由注册
#include "routes.h"
#include "lmjcore_handle.h"
#include "zlog.h"
#include <stdio.h>

int register_all_routes(router_t *router) {
  if (!router) {
    return -1;
  }

  // ==================== 批量操作 ====================

  // GET /batch - 只读批量操作
  if (router_register(router, HTTP_GET, "/$'batch'",
                      handle_batch_get, NULL) != 0) {
    dzlog_error("Failed to register GET /batch");
    return -1;
  }

  // POST /batch - 写操作批量操作
  if (router_register(router, HTTP_POST, "/$'batch'",
                      handle_batch_post, NULL) != 0) {
    dzlog_error("Failed to register POST /batch");
    return -1;
  }

  // ==================== 对象操作 ====================

  if (router_register(router, HTTP_POST, "/$'obj'", handle_obj_create, NULL) !=
      0) {
    dzlog_error("Failed to register POST /obj");
    return -1;
  }

  // POST /obj/init - 创建对象并填充成员（同一事务，原子操作）
  if (router_register(router, HTTP_POST, "/$'obj'/$'init'",
                      handle_obj_init, NULL) != 0) {
    dzlog_error("Failed to register POST /obj/init");
    return -1;
  }

  if (router_register(router, HTTP_GET, "/$'obj'/${}", handle_obj_get, NULL) !=
      0) {
    dzlog_error("Failed to register GET /obj/{ptr}");
    return -1;
  }

  if (router_register(router, HTTP_GET, "/$'obj'/${}/${}",
                      handle_obj_member_get, NULL) != 0) {
    dzlog_error("Failed to register GET /obj/{ptr}/{member}");
    return -1;
  }

  if (router_register(router, HTTP_PUT, "/$'obj'/${}/${}",
                      handle_obj_member_put, NULL) != 0) {
    dzlog_error("Failed to register PUT /obj/{ptr}/{member}");
    return -1;
  }

  if (router_register(router, HTTP_DELETE, "/$'obj'/${}/${}",
                      handle_obj_member_del, NULL) != 0) {
    dzlog_error("Failed to register DELETE /obj/{ptr}/{member}");
    return -1;
  }

  if (router_register(router, HTTP_DELETE, "/$'obj'/${}",
                      handle_obj_del, NULL) != 0) {
    dzlog_error("Failed to register DELETE /obj/{ptr}");
    return -1;
  }

  if (router_register(router, HTTP_GET, "/$'obj'/$'query'/${}",
                      handle_obj_query, NULL) != 0) {
    dzlog_error("Failed to register GET /obj/query");
    return -1;
  }

  // ==================== 集合操作 ====================

  if (router_register(router, HTTP_POST, "/$'set'", handle_set_create, NULL) !=
      0) {
    dzlog_error("Failed to register POST /set");
    return -1;
  }

  // POST /set/init - 创建集合并填充元素（同一事务，原子操作）
  if (router_register(router, HTTP_POST, "/$'set'/$'init'",
                      handle_set_init, NULL) != 0) {
    dzlog_error("Failed to register POST /set/init");
    return -1;
  }

  if (router_register(router, HTTP_GET, "/$'set'/${}", handle_set_get, NULL) !=
      0) {
    dzlog_error("Failed to register GET /set/{ptr}");
    return -1;
  }

  if (router_register(router, HTTP_POST, "/$'set'/${}/$'elements'",
                      handle_set_add, NULL) != 0) {
    dzlog_error("Failed to register POST /set/{ptr}/elements");
    return -1;
  }

  if (router_register(router, HTTP_DELETE, "/$'set'/${}/$'elements'",
                      handle_set_remove, NULL) != 0) {
    dzlog_error("Failed to register DELETE /set/{ptr}/elements");
    return -1;
  }

  if (router_register(router, HTTP_DELETE, "/$'set'/${}",
                      handle_set_del, NULL) != 0) {
    dzlog_error("Failed to register DELETE /set/{ptr}");
    return -1;
  }

  // ==================== 工具操作 ====================

  if (router_register(router, HTTP_GET, "/$'ptr'/${}/$'exist'",
                      handle_ptr_exist, NULL) != 0) {
    dzlog_error("Failed to register GET /ptr/{ptr}/exist");
    return -1;
  }

  if (router_register(router, HTTP_GET, "/$'health'", handle_health, NULL) !=
      0) {
    dzlog_error("Failed to register GET /health");
    return -1;
  }

  dzlog_info("Routes registered successfully:");
  dzlog_info("  GET    /batch (readonly)");
  dzlog_info("  POST   /batch (read-write)");
  dzlog_info("  POST   /obj");
  dzlog_info("  POST   /obj/init");
  dzlog_info("  GET    /obj/{ptr}");
  dzlog_info("  GET    /obj/{ptr}/{member}");
  dzlog_info("  PUT    /obj/{ptr}/{member}");
  dzlog_info("  DELETE /obj/{ptr}/{member}");
  dzlog_info("  DELETE /obj/{ptr}");
  dzlog_info("  GET    /obj/query");
  dzlog_info("  POST   /set");
  dzlog_info("  POST   /set/init");
  dzlog_info("  GET    /set/{ptr}");
  dzlog_info("  POST   /set/{ptr}/elements");
  dzlog_info("  DELETE /set/{ptr}/elements");
  dzlog_info("  DELETE /set/{ptr}");
  dzlog_info("  GET    /ptr/{ptr}/exist");
  dzlog_info("  GET    /health");

  return 0;
}