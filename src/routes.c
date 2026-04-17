// src/routes.c - 路由注册
#include "routes.h"
#include "lmjcore_handle.h"
#include "log.h"
#include <stdio.h>

int register_all_routes(router_t *router) {
  if (!router) {
    return -1;
  }

  // ==================== 对象操作 ====================

  if (router_register(router, HTTP_POST, "/$'obj'", handle_obj_create, NULL) !=
      0) {
    LOG_ERROR("Failed to register POST /obj");
    return -1;
  }

  if (router_register(router, HTTP_GET, "/$'obj'/${}", handle_obj_get, NULL) !=
      0) {
    LOG_ERROR("Failed to register GET /obj/{ptr}");
    return -1;
  }

  if (router_register(router, HTTP_GET, "/$'obj'/${}/${}",
                      handle_obj_member_get, NULL) != 0) {
    LOG_ERROR("Failed to register GET /obj/{ptr}/{member}");
    return -1;
  }

  if (router_register(router, HTTP_PUT, "/$'obj'/${}/${}",
                      handle_obj_member_put, NULL) != 0) {
    LOG_ERROR("Failed to register PUT /obj/{ptr}/{member}");
    return -1;
  }

  if (router_register(router, HTTP_DELETE, "/$'obj'/${}/${}",
                      handle_obj_member_del, NULL) != 0) {
    LOG_ERROR("Failed to register DELETE /obj/{ptr}/{member}");
    return -1;
  }

  if (router_register(router, HTTP_GET, "/$'obj'/$'query'/${}",
                      handle_obj_query, NULL) != 0) {
    LOG_ERROR("Failed to register GET /obj/query");
    return -1;
  }

  // ==================== 集合操作 ====================

  if (router_register(router, HTTP_POST, "/$'set'", handle_set_create, NULL) !=
      0) {
    LOG_ERROR("Failed to register POST /set");
    return -1;
  }

  if (router_register(router, HTTP_GET, "/$'set'/${}", handle_set_get, NULL) !=
      0) {
    LOG_ERROR("Failed to register GET /set/{ptr}");
    return -1;
  }

  if (router_register(router, HTTP_POST, "/$'set'/${}/$'elements'",
                      handle_set_add, NULL) != 0) {
    LOG_ERROR("Failed to register POST /set/{ptr}/elements");
    return -1;
  }

  if (router_register(router, HTTP_DELETE, "/$'set'/${}/$'elements'",
                      handle_set_remove, NULL) != 0) {
    LOG_ERROR("Failed to register DELETE /set/{ptr}/elements");
    return -1;
  }

  // ==================== 工具操作 ====================

  if (router_register(router, HTTP_GET, "/$'ptr'/${}/$'exist'",
                      handle_ptr_exist, NULL) != 0) {
    LOG_ERROR("Failed to register GET /ptr/{ptr}/exist");
    return -1;
  }

  if (router_register(router, HTTP_GET, "/$'health'", handle_health, NULL) !=
      0) {
    LOG_ERROR("Failed to register GET /health");
    return -1;
  }

  LOG_INFO("Routes registered successfully:");
  LOG_INFO("  POST   /obj");
  LOG_INFO("  GET    /obj/{ptr}");
  LOG_INFO("  GET    /obj/{ptr}/{member}");
  LOG_INFO("  PUT    /obj/{ptr}/{member}");
  LOG_INFO("  DELETE /obj/{ptr}/{member}");
  LOG_INFO("  GET    /obj/query");
  LOG_INFO("  POST   /set");
  LOG_INFO("  GET    /set/{ptr}");
  LOG_INFO("  POST   /set/{ptr}/elements");
  LOG_INFO("  DELETE /set/{ptr}/elements");
  LOG_INFO("  GET    /ptr/{ptr}/exist");
  LOG_INFO("  GET    /health");

  return 0;
}