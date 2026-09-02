// src/handlers/nested_value.c - 嵌套值统一创建工具实现
// 将任意 JSON 节点在当前事务内转换为可存储的编码值；
// JSON 对象/数组会递归创建嵌套 obj/set 实体并返回指针引用。
#include "nested_value.h"

#include "handle_utils.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==================== 内部工具 ====================

/**
 * @brief 在错误消息后追加一层上下文（原因在前，截断时原因仍可见）
 * 例如：... " (in member 'x')"，逐层追加形成 "原因 (in 内层) (in 外层) ..."
 */
static int fail_in(int rc, const char *ctx, char *err_buf, size_t err_buf_size) {
  if (err_buf && err_buf_size > 0) {
    char tmp[768];
    snprintf(tmp, sizeof(tmp), "%s (in %s)", err_buf, ctx);
    snprintf(err_buf, err_buf_size, "%s", tmp);
  }
  return rc;
}

/**
 * @brief 将 JSON 数字节点转换为文本（与 cJSON 的数字输出规则一致）
 */
static void number_to_text(const cJSON *node, char *buf, size_t buf_size) {
  if (node->valuedouble == (double)node->valueint) {
    snprintf(buf, buf_size, "%d", node->valueint);
  } else {
    snprintf(buf, buf_size, "%.17g", node->valuedouble);
  }
}

/**
 * @brief 在错误缓冲区中写入一个失败原因
 */
static void set_reason(char *err_buf, size_t err_buf_size, const char *fmt,
                       ...) {
  if (!err_buf || err_buf_size == 0) {
    return;
  }
  va_list args;
  va_start(args, fmt);
  vsnprintf(err_buf, err_buf_size, fmt, args);
  va_end(args);
}

// ==================== 公开：成员名校验 ====================

int lmjcore_nested_member_name_check(const char *name, size_t name_len,
                                     char *err_buf, size_t err_buf_size) {
  if (!name || name_len == 0) {
    if (err_buf && err_buf_size > 0) {
      snprintf(err_buf, err_buf_size, "member name must not be empty");
    }
    return LMJCORE_ERROR_INVALID_PARAM;
  }
  if (name_len > LMJCORE_MAX_MEMBER_NAME_LEN) {
    if (err_buf && err_buf_size > 0) {
      snprintf(err_buf, err_buf_size, "member name too long (max %d)",
               (int)LMJCORE_MAX_MEMBER_NAME_LEN);
    }
    return LMJCORE_ERROR_INVALID_PARAM;
  }
  return LMJCORE_SUCCESS;
}

// ==================== 递归编码 ====================

/**
 * @brief 标量节点编码（string 自动识别；number/bool 转文本后存 raw；null 存空值）
 */
static int encode_leaf_value(const cJSON *node, char *err_buf,
                             size_t err_buf_size, uint8_t **out_enc,
                             size_t *out_enc_len) {
  const char *text = NULL;
  size_t text_len = 0;
  char num_buf[64];

  if (cJSON_IsNull(node)) {
    text = "";
    text_len = 0;
  } else if (cJSON_IsString(node)) {
    text = node->valuestring ? node->valuestring : "";
    text_len = strlen(text);
  } else if (cJSON_IsNumber(node)) {
    number_to_text(node, num_buf, sizeof(num_buf));
    text = num_buf;
    text_len = strlen(text);
  } else if (cJSON_IsTrue(node)) {
    text = "true";
    text_len = 4;
  } else if (cJSON_IsFalse(node)) {
    text = "false";
    text_len = 5;
  } else {
    set_reason(err_buf, err_buf_size, "unsupported JSON value type");
    return LMJCORE_ERROR_INVALID_PARAM;
  }

  // 分配编码缓冲区并复用自动识别编码（raw/ref/null）
  size_t enc_cap = 1 + LMJCORE_PTR_LEN + text_len + 16;
  uint8_t *enc = (uint8_t *)malloc(enc_cap);
  if (!enc) {
    set_reason(err_buf, err_buf_size, "out of memory");
    return LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED;
  }

  size_t enc_len = 0;
  int rc = lmjcore_encode_value(text, text_len, enc, enc_cap, &enc_len);
  if (rc != LMJCORE_SUCCESS) {
    free(enc);
    set_reason(err_buf, err_buf_size, "%s", lmjcore_strerror(rc));
    return rc;
  }

  *out_enc = enc;
  *out_enc_len = enc_len;
  return LMJCORE_SUCCESS;
}

/**
 * @brief 构造 REF 编码（指向新创建的实体）
 */
static int build_ref_encoding(const lmjcore_ptr ptr, uint8_t **out_enc,
                              size_t *out_enc_len, char *err_buf,
                              size_t err_buf_size) {
  uint8_t *enc = (uint8_t *)malloc(1 + LMJCORE_PTR_LEN);
  if (!enc) {
    set_reason(err_buf, err_buf_size, "out of memory");
    return LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED;
  }
  enc[0] = LMJCORE_VALUE_TYPE_PTR;
  memcpy(enc + 1, ptr, LMJCORE_PTR_LEN);
  *out_enc = enc;
  *out_enc_len = 1 + LMJCORE_PTR_LEN;
  return LMJCORE_SUCCESS;
}

/**
 * @brief 递归处理：JSON 对象 -> 嵌套 obj
 */
static int encode_obj_node(lmjcore_txn *txn, const cJSON *node, int depth,
                           char *err_buf, size_t err_buf_size,
                           uint8_t **out_enc, size_t *out_enc_len) {
  if (depth >= LMJCORE_NEST_DEPTH_MAX) {
    set_reason(err_buf, err_buf_size, "nesting depth exceeds max %d",
               (int)LMJCORE_NEST_DEPTH_MAX);
    return LMJCORE_ERROR_INVALID_PARAM;
  }

  // 1. 创建嵌套对象（空对象同样创建，可后续填充）
  lmjcore_ptr obj_ptr;
  int rc = lmjcore_obj_create(txn, obj_ptr);
  if (rc != LMJCORE_SUCCESS) {
    set_reason(err_buf, err_buf_size, "%s", lmjcore_strerror(rc));
    return rc;
  }

  // 2. 逐个递归填充成员
  const cJSON *kv = NULL;
  for (kv = node->child; kv; kv = kv->next) {
    const char *name = kv->string ? kv->string : "";
    size_t name_len = strlen(name);
    char ctx[560];
    snprintf(ctx, sizeof(ctx), "member '%.*s'", (int)name_len, name);

    // 成员名校验（空名/超长直接失败）
    rc = lmjcore_nested_member_name_check(name, name_len, err_buf,
                                          err_buf_size);
    if (rc != LMJCORE_SUCCESS) {
      return fail_in(rc, ctx, err_buf, err_buf_size);
    }

    // 递归编码成员值
    uint8_t *enc = NULL;
    size_t enc_len = 0;
    rc = lmjcore_json_encode_value(txn, kv, depth + 1, err_buf, err_buf_size,
                                   &enc, &enc_len);
    if (rc != LMJCORE_SUCCESS) {
      return fail_in(rc, ctx, err_buf, err_buf_size);
    }

    // 写入成员值
    rc = lmjcore_obj_member_put(txn, obj_ptr, (const uint8_t *)name, name_len,
                                enc, enc_len);
    free(enc);
    if (rc != LMJCORE_SUCCESS) {
      set_reason(err_buf, err_buf_size, "%s", lmjcore_strerror(rc));
      return fail_in(rc, ctx, err_buf, err_buf_size);
    }
  }

  // 3. 返回指向该嵌套对象的 REF 编码
  return build_ref_encoding(obj_ptr, out_enc, out_enc_len, err_buf,
                            err_buf_size);
}

/**
 * @brief 递归处理：JSON 数组 -> 嵌套 set
 */
static int encode_array_node(lmjcore_txn *txn, const cJSON *node, int depth,
                             char *err_buf, size_t err_buf_size,
                             uint8_t **out_enc, size_t *out_enc_len) {
  if (depth >= LMJCORE_NEST_DEPTH_MAX) {
    set_reason(err_buf, err_buf_size, "nesting depth exceeds max %d",
               (int)LMJCORE_NEST_DEPTH_MAX);
    return LMJCORE_ERROR_INVALID_PARAM;
  }

  // 1. 创建嵌套集合（空集合同样创建，可后续填充）
  lmjcore_ptr set_ptr;
  int rc = lmjcore_set_create(txn, set_ptr);
  if (rc != LMJCORE_SUCCESS) {
    set_reason(err_buf, err_buf_size, "%s", lmjcore_strerror(rc));
    return rc;
  }

  // 2. 逐个递归添加元素（重复元素自动去重）
  int index = 0;
  const cJSON *item = NULL;
  for (item = node->child; item; item = item->next) {
    uint8_t *enc = NULL;
    size_t enc_len = 0;
    rc = lmjcore_json_encode_value(txn, item, depth + 1, err_buf, err_buf_size,
                                   &enc, &enc_len);
    if (rc != LMJCORE_SUCCESS) {
      char ctx[64];
      snprintf(ctx, sizeof(ctx), "element [%d]", index);
      return fail_in(rc, ctx, err_buf, err_buf_size);
    }

    rc = lmjcore_set_add(txn, set_ptr, enc, enc_len);
    free(enc);
    if (rc == LMJCORE_ERROR_MEMBER_EXISTS) {
      rc = LMJCORE_SUCCESS;
    }
    if (rc != LMJCORE_SUCCESS) {
      char ctx[64];
      snprintf(ctx, sizeof(ctx), "element [%d]", index);
      set_reason(err_buf, err_buf_size, "%s", lmjcore_strerror(rc));
      return fail_in(rc, ctx, err_buf, err_buf_size);
    }
    index++;
  }

  // 3. 返回指向该嵌套集合的 REF 编码
  return build_ref_encoding(set_ptr, out_enc, out_enc_len, err_buf,
                            err_buf_size);
}

// ==================== 公开：JSON 节点 -> 编码值 ====================

int lmjcore_json_encode_value(lmjcore_txn *txn, const cJSON *node, int depth,
                              char *err_buf, size_t err_buf_size,
                              uint8_t **out_enc, size_t *out_enc_len) {
  if (!txn || !node || !out_enc || !out_enc_len) {
    return LMJCORE_ERROR_NULL_POINTER;
  }
  if (err_buf && err_buf_size > 0) {
    err_buf[0] = '\0';
  }

  *out_enc = NULL;
  *out_enc_len = 0;

  if (cJSON_IsObject(node)) {
    return encode_obj_node(txn, node, depth, err_buf, err_buf_size, out_enc,
                           out_enc_len);
  }
  if (cJSON_IsArray(node)) {
    return encode_array_node(txn, node, depth, err_buf, err_buf_size, out_enc,
                             out_enc_len);
  }
  return encode_leaf_value(node, err_buf, err_buf_size, out_enc, out_enc_len);
}
