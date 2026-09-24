// src/handlers/obj_handle.c - 对象相关 HTTP 处理器
#include "cJSON.h"
#include "error_response.h"
#include "handle_utils.h"
#include "lmjcore.h"
#include "nested_value.h"
#include "router.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==================== 对象处理器 ====================

int handle_obj_create(void *params, void *cbdata) {
  handle_params_t *hp = (handle_params_t *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 检查是否已有事务（批量操作场景）
  lmjcore_txn *txn = NULL;
  int auto_commit = 1;  // 是否自动提交事务

  if (hp->txn && !hp->auto_manage_txn) {
    // 使用已有事务（批量操作场景）
    txn = hp->txn;
    auto_commit = 0;
  } else {
    // 开启写事务
    int rc = lmjcore_txn_begin(hp->env, NULL, 0, &txn);
    if (rc != LMJCORE_SUCCESS || !txn) {
      RETURN_ERROR_TXN_FAILED("begin", response);
    }
  }

  // 检查事务超时
  CHECK_TXN_TIMEOUT(hp, response, txn);

  // 创建对象
  lmjcore_ptr obj_ptr;
  int rc = lmjcore_obj_create(txn, obj_ptr);
  if (rc != LMJCORE_SUCCESS) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    json_response_lmjcore_error(response, rc);
    return -1;
  }

  // 提交事务（仅当自动管理时）
  if (auto_commit) {
    rc = lmjcore_txn_commit(txn);
    if (rc != LMJCORE_SUCCESS) {
      lmjcore_txn_abort(txn);
      RETURN_ERROR_TXN_FAILED("commit", response);
    }
  }

  // 将指针转换为字符串
  char ptr_str[LMJCORE_PTR_STRING_LEN + 1];
  lmjcore_ptr_to_string(obj_ptr, ptr_str, sizeof(ptr_str));

  // 构建响应
  return json_response_set(response, HTTP_STATUS_CREATED, json_new_ptr(ptr_str));
}

int handle_obj_get(void *params, void *cbdata) {
  handle_params_t *hp = (handle_params_t *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 获取指针参数
  const char *ptr_str = route_params_get(hp->params, 0);
  if (!ptr_str) {
    RETURN_ERROR_MISSING_PARAM("ptr", response);
  }

  // 转换指针
  lmjcore_ptr obj_ptr;
  if (lmjcore_ptr_from_string(ptr_str, obj_ptr) != LMJCORE_SUCCESS) {
    RETURN_ERROR_INVALID_PTR(response);
  }

  // 检查是否已有事务（批量操作场景）
  lmjcore_txn *txn = NULL;
  int auto_commit = 1;  // 是否自动提交事务

  if (hp->txn && !hp->auto_manage_txn) {
    // 使用已有事务（批量操作场景）
    txn = hp->txn;
    auto_commit = 0;
  } else {
    // 开启读事务
    int rc = lmjcore_txn_begin(hp->env, NULL, LMJCORE_TXN_READONLY, &txn);
    if (rc != LMJCORE_SUCCESS || !txn) {
      RETURN_ERROR_TXN_FAILED("begin", response);
    }
  }

  // 检查事务超时
  CHECK_TXN_TIMEOUT(hp, response, txn);

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, obj_ptr);
  if (exists != 1) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    RETURN_ERROR_NOT_FOUND("Object", response);
  }

  // 分配缓冲区读取对象内容
  size_t total_value_len = 0;
  size_t total_value_count = 0;
  size_t total_member_len = 0;
  size_t member_count = 0;

  lmjcore_obj_stat_values(txn, obj_ptr, &total_value_len, &total_value_count);
  lmjcore_obj_stat_members(txn, obj_ptr, &total_member_len, &member_count);

  // 估算缓冲区大小
  size_t buf_size = sizeof(lmjcore_result_obj) +
                    member_count * sizeof(lmjcore_member_descriptor) +
                    total_member_len + total_value_len + 1024;

  uint8_t *result_buf = (uint8_t *)malloc(buf_size);
  if (!result_buf) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    RETURN_ERROR_NO_MEMORY(response);
  }

  lmjcore_result_obj *result_head = NULL;
  int rc = lmjcore_obj_get(txn, obj_ptr, result_buf, buf_size, &result_head);

  // 读事务完成，仅在自动管理时中止
  if (auto_commit) {
    lmjcore_txn_abort(txn);
  }

  if (rc != LMJCORE_SUCCESS) {
    free(result_buf);
    json_response_lmjcore_error(response, rc);
    return -1;
  }

  // 构建 JSON 响应：{"ptr":"...","members":[{"name","value","type"}...],"count":N}
  char ptr_out[LMJCORE_PTR_STRING_LEN + 1];
  lmjcore_ptr_to_string(obj_ptr, ptr_out, sizeof(ptr_out));

  cJSON *members = NULL;
  cJSON *root = json_new_entity(ptr_out, "members", &members);
  if (!root) {
    free(result_buf);
    RETURN_ERROR_NO_MEMORY(response);
  }

  size_t built_count = 0;
  for (size_t i = 0; i < result_head->member_count; i++) {
    lmjcore_member_descriptor *desc = &result_head->members[i];

    // 结果缓冲区中的成员名不带结尾符，需按长度复制后再交给 cJSON
    size_t name_len = desc->member_name.value_len;
    if (name_len > LMJCORE_MAX_MEMBER_NAME_LEN) {
      name_len = LMJCORE_MAX_MEMBER_NAME_LEN;
    }
    char member_name[LMJCORE_MAX_MEMBER_NAME_LEN + 1];
    memcpy(member_name, result_buf + desc->member_name.value_offset, name_len);
    member_name[name_len] = '\0';

    // 解码成员值
    char *value_str = NULL;
    api_value_type_t value_type = VALUE_TYPE_NULL;
    const uint8_t *value_data =
        (const uint8_t *)(result_buf + desc->member_value.value_offset);

    if (lmjcore_decode_value(value_data, desc->member_value.value_len,
                             &value_str, &value_type) != LMJCORE_SUCCESS) {
      continue; // 无法解码的成员跳过，保证响应始终为合法 JSON
    }

    cJSON *item = json_new_value_entry("name", member_name, value_str,
                                       value_type_to_string(value_type));
    free(value_str);

    if (!json_array_append(members, item)) {
      cJSON_Delete(root);
      free(result_buf);
      RETURN_ERROR_NO_MEMORY(response);
    }
    built_count++;
  }

  if (json_entity_set_count(root, built_count) != 0) {
    cJSON_Delete(root);
    free(result_buf);
    RETURN_ERROR_NO_MEMORY(response);
  }

  free(result_buf);
  return json_response_set(response, HTTP_STATUS_OK, root);
}

int handle_obj_member_get(void *params, void *cbdata) {
  handle_params_t *hp = (handle_params_t *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 获取参数：ptr 和 member_name
  const char *ptr_str = route_params_get(hp->params, 0);
  const char *member_name_encoded = route_params_get(hp->params, 1);

  if (!ptr_str || !member_name_encoded) {
    RETURN_ERROR_MISSING_PARAM("ptr or member", response);
  }

  // URL 解码成员名
  char member_name[512];
  size_t member_name_len = strlen(member_name_encoded);
  if (url_decode(member_name_encoded, member_name_len, member_name,
                 sizeof(member_name)) < 0) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 转换指针
  lmjcore_ptr obj_ptr;
  if (lmjcore_ptr_from_string(ptr_str, obj_ptr) != LMJCORE_SUCCESS) {
    RETURN_ERROR_INVALID_PTR(response);
  }

  // 检查是否已有事务（批量操作场景）
  lmjcore_txn *txn = NULL;
  int auto_commit = 1;  // 是否自动提交事务

  if (hp->txn && !hp->auto_manage_txn) {
    // 使用已有事务（批量操作场景）
    txn = hp->txn;
    auto_commit = 0;
  } else {
    // 开启读事务
    int rc = lmjcore_txn_begin(hp->env, NULL, LMJCORE_TXN_READONLY, &txn);
    if (rc != LMJCORE_SUCCESS || !txn) {
      RETURN_ERROR_TXN_FAILED("begin", response);
    }
  }

  // 检查事务超时
  CHECK_TXN_TIMEOUT(hp, response, txn);

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, obj_ptr);
  if (exists != 1) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    RETURN_ERROR_NOT_FOUND("Object", response);
  }

  // 分配缓冲区读取成员值
  size_t value_buf_size = 4096;
  uint8_t *value_buf = (uint8_t *)malloc(value_buf_size);
  if (!value_buf) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    RETURN_ERROR_NO_MEMORY(response);
  }

  size_t value_len = 0;
  int rc = lmjcore_obj_member_get(txn, obj_ptr, (const uint8_t *)member_name,
                              strlen(member_name), value_buf, value_buf_size,
                              &value_len);

  // 读事务完成，仅在自动管理时中止
  if (auto_commit) {
    lmjcore_txn_abort(txn);
  }

  if (rc == LMJCORE_ERROR_MEMBER_NOT_FOUND) {
    free(value_buf);
    RETURN_ERROR_MEMBER_NOT_FOUND(response);
  }

  if (rc != LMJCORE_SUCCESS) {
    free(value_buf);
    json_response_lmjcore_error(response, rc);
    return -1;
  }

  // 解码值
  char *value_str = NULL;
  api_value_type_t value_type = VALUE_TYPE_NULL;
  rc = lmjcore_decode_value(value_buf, value_len, &value_str, &value_type);
  free(value_buf);

  if (rc != LMJCORE_SUCCESS) {
    free(value_str);
    json_response_lmjcore_error(response, rc);
    return -1;
  }

  // 构建响应：{"member":"...","value":"...","type":"..."}
  cJSON *root =
      json_new_value_entry("member", member_name, value_str,
                           value_type_to_string(value_type));
  free(value_str);

  return json_response_set(response, HTTP_STATUS_OK, root);
}

int handle_obj_member_put(void *params, void *cbdata) {
  handle_params_t *hp = (handle_params_t *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 获取参数：ptr 和 member_name
  const char *ptr_str = route_params_get(hp->params, 0);
  const char *member_name_encoded = route_params_get(hp->params, 1);

  if (!ptr_str || !member_name_encoded) {
    RETURN_ERROR_MISSING_PARAM("ptr or member", response);
  }

  // URL 解码成员名
  char member_name[512];
  size_t member_name_len = strlen(member_name_encoded);
  if (url_decode(member_name_encoded, member_name_len, member_name,
                 sizeof(member_name)) < 0) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 解析请求体获取 value
  cJSON *body = cJSON_Parse(hp->body);
  if (!body) {
    RETURN_ERROR_BODY_PARSE(response);
  }

  cJSON *value_item = cJSON_GetObjectItemCaseSensitive(body, "value");
  if (!value_item || !cJSON_IsString(value_item)) {
    cJSON_Delete(body);
    RETURN_ERROR_BODY_PARSE(response);
  }

  const char *value_str = value_item->valuestring;
  size_t value_len = strlen(value_str);

  // 转换指针
  lmjcore_ptr obj_ptr;
  if (lmjcore_ptr_from_string(ptr_str, obj_ptr) != LMJCORE_SUCCESS) {
    cJSON_Delete(body);
    RETURN_ERROR_INVALID_PTR(response);
  }

  // 检查是否已有事务（批量操作场景）
  lmjcore_txn *txn = NULL;
  int auto_commit = 1;  // 是否自动提交事务

  if (hp->txn && !hp->auto_manage_txn) {
    // 使用已有事务（批量操作场景）
    txn = hp->txn;
    auto_commit = 0;
  } else {
    // 开启写事务
    int rc = lmjcore_txn_begin(hp->env, NULL, 0, &txn);
    if (rc != LMJCORE_SUCCESS || !txn) {
      cJSON_Delete(body);
      RETURN_ERROR_TXN_FAILED("begin", response);
    }
  }

  // 检查事务超时
  CHECK_TXN_TIMEOUT(hp, response, txn);

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, obj_ptr);
  if (exists != 1) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    cJSON_Delete(body);
    RETURN_ERROR_NOT_FOUND("Object", response);
  }

  // 编码值
  size_t encoded_size = 1 + LMJCORE_PTR_LEN + value_len + 16;
  uint8_t *encoded_value = (uint8_t *)malloc(encoded_size);
  if (!encoded_value) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    cJSON_Delete(body);
    RETURN_ERROR_NO_MEMORY(response);
  }

  size_t encoded_len = 0;
  int rc = lmjcore_encode_value(value_str, value_len, encoded_value, encoded_size,
                            &encoded_len);

  if (rc != LMJCORE_SUCCESS) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    free(encoded_value);
    cJSON_Delete(body);
    json_response_lmjcore_error(response, rc);
    return -1;
  }

  // 设置成员值（使用解码后的成员名）
  rc = lmjcore_obj_member_put(txn, obj_ptr, (const uint8_t *)member_name,
                              strlen(member_name), encoded_value, encoded_len);
  free(encoded_value);

  if (rc != LMJCORE_SUCCESS) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    cJSON_Delete(body);
    json_response_lmjcore_error(response, rc);
    return -1;
  }

  // 提交事务（仅当自动管理时）
  if (auto_commit) {
    rc = lmjcore_txn_commit(txn);
    if (rc != LMJCORE_SUCCESS) {
      lmjcore_txn_abort(txn);
      cJSON_Delete(body);
      RETURN_ERROR_TXN_FAILED("commit", response);
    }
  }

  cJSON_Delete(body);
  return json_response_success(response, HTTP_STATUS_OK);
}

int handle_obj_member_del(void *params, void *cbdata) {
  handle_params_t *hp = (handle_params_t *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 获取参数：ptr 和 member_name
  const char *ptr_str = route_params_get(hp->params, 0);
  const char *member_name_encoded = route_params_get(hp->params, 1);

  if (!ptr_str || !member_name_encoded) {
    RETURN_ERROR_MISSING_PARAM("ptr or member", response);
  }

  // URL 解码成员名
  char member_name[512];
  size_t member_name_len = strlen(member_name_encoded);
  if (url_decode(member_name_encoded, member_name_len, member_name,
                 sizeof(member_name)) < 0) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 转换指针
  lmjcore_ptr obj_ptr;
  if (lmjcore_ptr_from_string(ptr_str, obj_ptr) != LMJCORE_SUCCESS) {
    RETURN_ERROR_INVALID_PTR(response);
  }

  // 检查是否已有事务（批量操作场景）
  lmjcore_txn *txn = NULL;
  int auto_commit = 1;  // 是否自动提交事务

  if (hp->txn && !hp->auto_manage_txn) {
    // 使用已有事务（批量操作场景）
    txn = hp->txn;
    auto_commit = 0;
  } else {
    // 开启写事务
    int rc = lmjcore_txn_begin(hp->env, NULL, 0, &txn);
    if (rc != LMJCORE_SUCCESS || !txn) {
      RETURN_ERROR_TXN_FAILED("begin", response);
    }
  }

  // 检查事务超时
  CHECK_TXN_TIMEOUT(hp, response, txn);

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, obj_ptr);
  if (exists != 1) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    RETURN_ERROR_NOT_FOUND("Object", response);
  }

  // 删除成员（使用解码后的成员名）
  int rc = lmjcore_obj_member_del(txn, obj_ptr, (const uint8_t *)member_name,
                              strlen(member_name));

  if (rc == LMJCORE_ERROR_MEMBER_NOT_FOUND) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    RETURN_ERROR_MEMBER_NOT_FOUND(response);
  }

  if (rc != LMJCORE_SUCCESS) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    json_response_lmjcore_error(response, rc);
    return -1;
  }

  // 提交事务（仅当自动管理时）
  if (auto_commit) {
    rc = lmjcore_txn_commit(txn);
    if (rc != LMJCORE_SUCCESS) {
      lmjcore_txn_abort(txn);
      RETURN_ERROR_TXN_FAILED("commit", response);
    }
  }

  return json_response_success(response, HTTP_STATUS_OK);
}

int handle_obj_del(void *params, void *cbdata) {
  handle_params_t *hp = (handle_params_t *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env || !hp->params) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 获取指针参数
  const char *ptr_str = route_params_get(hp->params, 0);
  if (!ptr_str) {
    RETURN_ERROR_MISSING_PARAM("ptr", response);
  }

  // 转换指针
  lmjcore_ptr obj_ptr;
  if (lmjcore_ptr_from_string(ptr_str, obj_ptr) != LMJCORE_SUCCESS) {
    RETURN_ERROR_INVALID_PTR(response);
  }

  // 检查是否已有事务（批量操作场景）
  lmjcore_txn *txn = NULL;
  int auto_commit = 1;  // 是否自动提交事务

  if (hp->txn && !hp->auto_manage_txn) {
    // 使用已有事务（批量操作场景）
    txn = hp->txn;
    auto_commit = 0;
  } else {
    // 开启写事务
    int rc = lmjcore_txn_begin(hp->env, NULL, 0, &txn);
    if (rc != LMJCORE_SUCCESS || !txn) {
      RETURN_ERROR_TXN_FAILED("begin", response);
    }
  }

  // 检查事务超时
  CHECK_TXN_TIMEOUT(hp, response, txn);

  // 检查实体是否存在
  int exists = lmjcore_entity_exist(txn, obj_ptr);
  if (exists != 1) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    RETURN_ERROR_NOT_FOUND("Object", response);
  }

  // 删除对象
  int rc = lmjcore_obj_del(txn, obj_ptr);
  if (rc != LMJCORE_SUCCESS) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    json_response_lmjcore_error(response, rc);
    return -1;
  }

  // 提交事务（仅当自动管理时）
  if (auto_commit) {
    rc = lmjcore_txn_commit(txn);
    if (rc != LMJCORE_SUCCESS) {
      lmjcore_txn_abort(txn);
      RETURN_ERROR_TXN_FAILED("commit", response);
    }
  }

  return json_response_success(response, HTTP_STATUS_OK);
}

// ==================== 对象初始化处理器（原子创建 + 嵌套成员） ====================

/**
 * @brief POST /obj/init - 在同一事务内创建对象并填充成员值（支持嵌套创建）
 *
 * 请求体直接为 JSON 对象：{ "<成员名>": <任意 JSON 值>, ... }
 * 成员值由嵌套工具统一处理：
 *   - JSON object -> 同一事务内创建嵌套 obj（存 REF）
 *   - JSON array  -> 同一事务内创建嵌套 set（存 REF）
 *   - JSON string -> 自动识别：01/02 开头 34 位 -> REF；"" 或 "null" -> null；其余 -> raw
 *   - JSON null   -> null；number/bool -> raw（其 JSON 文本）
 *
 * 成功：HTTP 201，返回 {"ptr":"...","member_count":N}
 * 失败：自动回滚并返回失败原因 {"error":"..."}（含逐层路径）；
 *       若运行于批量(batch)共享事务中，不自行提交/回滚，由调用方统一处理
 */
int handle_obj_init(void *params, void *cbdata) {
  handle_params_t *hp = (handle_params_t *)params;
  http_response_t *response = (http_response_t *)cbdata;

  if (!hp || !hp->env) {
    RETURN_ERROR_INVALID_PARAM(response);
  }

  // 解析请求体：必须为 JSON 对象（成员映射）
  cJSON *root = hp->body ? cJSON_ParseWithOpts(hp->body, NULL, 0) : NULL;
  if (!root) {
    return json_response_error(response, HTTP_STATUS_BAD_REQUEST,
                               "Invalid JSON in request body");
  }

  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return json_response_error(
        response, HTTP_STATUS_BAD_REQUEST,
        "Request body must be a JSON object (member map)");
  }

  // 检查是否已有事务（批量操作场景）
  lmjcore_txn *txn = NULL;
  int auto_commit = 1;  // 是否自动提交事务

  if (hp->txn && !hp->auto_manage_txn) {
    // 使用已有事务（批量操作场景），回滚由调用方负责
    txn = hp->txn;
    auto_commit = 0;
  } else {
    // 开启写事务
    int rc = lmjcore_txn_begin(hp->env, NULL, 0, &txn);
    if (rc != LMJCORE_SUCCESS || !txn) {
      cJSON_Delete(root);
      RETURN_ERROR_TXN_FAILED("begin", response);
    }
  }

  // 检查事务超时
  CHECK_TXN_TIMEOUT(hp, response, txn);

  // 1. 创建根对象
  lmjcore_ptr obj_ptr;
  int rc = lmjcore_obj_create(txn, obj_ptr);
  if (rc != LMJCORE_SUCCESS) {
    if (auto_commit) {
      lmjcore_txn_abort(txn);
    }
    cJSON_Delete(root);
    json_response_lmjcore_error(response, rc);
    return -1;
  }

  // 2. 逐成员写入（值可为任意 JSON，嵌套对象/数组由工具自动创建）
  char err[1024];
  char reason[1152];
  int filled = 0;
  const cJSON *kv = NULL;
  for (kv = root->child; kv; kv = kv->next) {
    const char *name = kv->string ? kv->string : "";
    size_t name_len = strlen(name);

    // 成员名校验
    rc = lmjcore_nested_member_name_check(name, name_len, err, sizeof(err));
    if (rc != LMJCORE_SUCCESS) {
      snprintf(reason, sizeof(reason), "%s (in member '%.*s')", err,
               (int)name_len, name);
      if (auto_commit) {
        lmjcore_txn_abort(txn);
      }
      cJSON_Delete(root);
      return json_response_error(response, lmjcore_error_to_http_status(rc),
                                 "%s", reason);
    }

    // 递归编码成员值（标量或嵌套创建）
    uint8_t *enc = NULL;
    size_t enc_len = 0;
    rc = lmjcore_json_encode_value(txn, kv, 0, err, sizeof(err), &enc,
                                   &enc_len);
    if (rc != LMJCORE_SUCCESS) {
      snprintf(reason, sizeof(reason), "%s (in member '%.*s')", err,
               (int)name_len, name);
      if (auto_commit) {
        lmjcore_txn_abort(txn);
      }
      cJSON_Delete(root);
      return json_response_error(response, lmjcore_error_to_http_status(rc),
                                 "%s", reason);
    }

    // 写入成员值
    rc = lmjcore_obj_member_put(txn, obj_ptr, (const uint8_t *)name, name_len,
                                enc, enc_len);
    free(enc);
    if (rc != LMJCORE_SUCCESS) {
      snprintf(reason, sizeof(reason), "%s (in member '%.*s')",
               lmjcore_strerror(rc), (int)name_len, name);
      if (auto_commit) {
        lmjcore_txn_abort(txn);
      }
      cJSON_Delete(root);
      return json_response_error(response, lmjcore_error_to_http_status(rc),
                                 "%s", reason);
    }

    filled++;
  }

  // 3. 提交事务（仅当自动管理时）
  if (auto_commit) {
    rc = lmjcore_txn_commit(txn);
    if (rc != LMJCORE_SUCCESS) {
      lmjcore_txn_abort(txn);
      cJSON_Delete(root);
      RETURN_ERROR_TXN_FAILED("commit", response);
    }
  }

  cJSON_Delete(root);

  // 将指针转换为字符串并构建响应
  char ptr_str[LMJCORE_PTR_STRING_LEN + 1];
  lmjcore_ptr_to_string(obj_ptr, ptr_str, sizeof(ptr_str));

  return json_response_set(
      response, HTTP_STATUS_CREATED,
      json_new_counted_ptr(ptr_str, "member_count", (size_t)filled));
}
