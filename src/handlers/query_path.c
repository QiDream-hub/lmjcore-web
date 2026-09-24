// src/handlers/query_path.c - 链式查询路径的解析与深度访问实现
#include "query_path.h"

#include "error_codes.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ==================== 内部：路径解析 ====================

typedef struct {
  char **segments; // segments[0] = 34 位指针字符串，其余为已解码成员名
  size_t count;
} query_segments_t;

static void segments_free(char **segments, size_t count) {
  if (!segments) {
    return;
  }
  for (size_t i = 0; i < count; i++) {
    free(segments[i]);
  }
  free(segments);
}

/**
 * @brief 构造失败位置（"<ptr>/<seg>/…"，最多到 upto 段）
 */
static char *build_at(char **segments, size_t count, size_t upto) {
  if (!segments || count == 0) {
    return NULL;
  }
  if (upto >= count) {
    upto = count - 1;
  }

  size_t total = 0;
  for (size_t i = 0; i <= upto; i++) {
    total += strlen(segments[i]) + 1; // +1 为分隔符或结尾
  }

  char *at = (char *)malloc(total + 1);
  if (!at) {
    return NULL;
  }

  at[0] = '\0';
  for (size_t i = 0; i <= upto; i++) {
    if (i > 0) {
      strcat(at, "/");
    }
    strcat(at, segments[i]);
  }
  return at;
}

static void err_set(query_error_t *err, char **segments, size_t count,
                    size_t index) {
  if (!err) {
    return;
  }
  err->at = build_at(segments, count, index);
  err->segment = index;
}

/** @brief 记录探测到的实际存储类型（value[0] 类型标记） */
static void err_set_type(query_error_t *err, api_value_type_t type) {
  if (!err) {
    return;
  }
  err->has_found_type = true;
  err->found_type = type;
}

/**
 * @brief 存储类型标记 -> API 类型
 *
 * 项目约定：value 的首字节标识存储类型（RAW / PTR / NULL）。
 */
static bool tag_to_api_type(uint8_t tag, api_value_type_t *out) {
  switch (tag) {
  case LMJCORE_VALUE_TYPE_RAW:
    *out = VALUE_TYPE_RAW;
    return true;
  case LMJCORE_VALUE_TYPE_NULL:
    *out = VALUE_TYPE_NULL;
    return true;
  case LMJCORE_VALUE_TYPE_PTR:
    *out = VALUE_TYPE_REF; // 具体 obj/set 由引用载荷首字节决定
    return true;
  default:
    return false;
  }
}

/**
 * @brief 拆分并解码查询路径
 *
 * 先按 '.' 切分（此时成员名中的字面点仍是 %2E），再逐段 url_decode，
 * 因此含 '.' 的成员名可以正常寻址。空段一律视为语法错误。
 */
static int segments_parse(const char *path, size_t max_depth,
                          query_segments_t *out) {
  out->segments = NULL;
  out->count = 0;

  if (!path || max_depth == 0) {
    return LMJCORE_ERROR_PATH_PARSE;
  }

  const char *first_dot = strchr(path, '.');
  if (!first_dot) {
    return LMJCORE_ERROR_PATH_PARSE; // 至少需要一个成员段
  }

  size_t ptr_len = (size_t)(first_dot - path);
  if (ptr_len != LMJCORE_PTR_STRING_LEN) {
    return LMJCORE_ERROR_PATH_PARSE;
  }

  // 统计成员段数量并检查深度（先计数，避免为超长路径分配内存）
  size_t member_count = 0;
  for (const char *p = first_dot + 1;;) {
    const char *dot = strchr(p, '.');
    size_t len = dot ? (size_t)(dot - p) : strlen(p);
    if (len == 0) {
      return LMJCORE_ERROR_PATH_PARSE; // 空段
    }
    member_count++;
    if (member_count > max_depth) {
      return LMJCORE_ERROR_PATH_TOO_DEEP;
    }
    if (!dot) {
      break;
    }
    p = dot + 1;
  }

  size_t total = member_count + 1;
  char **segments = (char **)calloc(total, sizeof(char *));
  if (!segments) {
    return LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED;
  }

  segments[0] = (char *)malloc(ptr_len + 1);
  if (!segments[0]) {
    segments_free(segments, 0);
    return LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED;
  }
  memcpy(segments[0], path, ptr_len);
  segments[0][ptr_len] = '\0';

  size_t index = 1;
  for (const char *p = first_dot + 1; index < total;) {
    const char *dot = strchr(p, '.');
    size_t len = dot ? (size_t)(dot - p) : strlen(p);

    char *buf = (char *)malloc(len + 1);
    if (!buf) {
      segments_free(segments, index);
      return LMJCORE_ERROR_MEMORY_ALLOCATION_FAILED;
    }
    if (url_decode(p, len, buf, len + 1) < 0) {
      free(buf);
      segments_free(segments, index);
      return LMJCORE_ERROR_PATH_URL_DECODE;
    }
    segments[index++] = buf;

    if (!dot) {
      break;
    }
    p = dot + 1;
  }

  out->segments = segments;
  out->count = total;
  return LMJCORE_SUCCESS;
}

// ==================== 公开 API ====================

void query_options_init(query_options_t *options, size_t max_depth,
                        size_t max_value_bytes) {
  if (!options) {
    return;
  }
  options->max_depth =
      (max_depth > 0) ? max_depth : (size_t)QUERY_MAX_DEPTH_DEFAULT;
  options->max_value_bytes = (max_value_bytes > 0)
                                 ? max_value_bytes
                                 : (size_t)QUERY_MAX_VALUE_BYTES_DEFAULT;
}

void query_error_free(query_error_t *err) {
  if (!err) {
    return;
  }
  free(err->at);
  err->at = NULL;
  err->segment = 0;
  err->has_found_type = false;
  err->found_type = VALUE_TYPE_NULL;
}

int query_path_execute(lmjcore_txn *txn, const char *path,
                       const query_options_t *options, char **out_value,
                       api_value_type_t *out_type, query_error_t *err) {
  if (err) {
    err->at = NULL;
    err->segment = 0;
    err->has_found_type = false;
    err->found_type = VALUE_TYPE_NULL;
  }
  if (!txn || !path || !out_value || !out_type) {
    return LMJCORE_ERROR_NULL_POINTER;
  }

  query_options_t opts;
  query_options_init(&opts, options ? options->max_depth : 0,
                     options ? options->max_value_bytes : 0);

  query_segments_t segs;
  int rc = segments_parse(path, opts.max_depth, &segs);
  if (rc != LMJCORE_SUCCESS) {
    // 语法类错误没有可定位的段
    return rc;
  }

  // 根段必须是对象指针
  lmjcore_ptr cur;
  rc = lmjcore_ptr_from_string(segs.segments[0], cur);
  if (rc != LMJCORE_SUCCESS) {
    err_set(err, segs.segments, segs.count, 0);
    segments_free(segs.segments, segs.count);
    return LMJCORE_ERROR_PATH_INVALID_PTR;
  }
  if (cur[0] != LMJCORE_OBJ) {
    err_set(err, segs.segments, segs.count, 0);
    segments_free(segs.segments, segs.count);
    if (cur[0] == LMJCORE_SET) {
      err_set_type(err, VALUE_TYPE_SET);
      return LMJCORE_ERROR_SET_NOT_SUPPORTED;
    }
    return LMJCORE_ERROR_PATH_INVALID_PTR;
  }

  // 中间段只需要判断是否为指针引用：
  //   - 项目约定 value[0] 是类型标记（RAW / PTR / NULL），读到值即以它为准；
  //   - lmjcore_obj_member_get 要么全拷贝、要么不拷贝，缓冲区不足时不回传长度，
  //     因此无法只读 1 字节类型标记；而 PTR 编码恰为 1 + 17 = 18 字节，
  //     故用 18 字节探测：一旦 BUFFER_TOO_SMALL，值必然比指针编码长，即不是引用；
  //   - 若将来新增更长的引用编码，此处不变量需同步调整。
  uint8_t probe[1 + LMJCORE_PTR_LEN];
  size_t last = segs.count - 1;
  size_t fail_index = 1;

  for (size_t i = 1; i <= last; i++) {
    const char *name = segs.segments[i];
    size_t name_len = strlen(name);

    if (i < last) {
      // ---- 中间段：必须是指向对象的引用 ----
      size_t got = 0;
      rc = lmjcore_obj_member_get(txn, cur, (const uint8_t *)name, name_len,
                                  probe, sizeof(probe), &got);
      if (rc == LMJCORE_ERROR_BUFFER_TOO_SMALL) {
        rc = LMJCORE_ERROR_TYPE_MISMATCH; // 比指针编码更长 -> 不是引用
      } else if (rc == LMJCORE_SUCCESS) {
        // 类型标记优先：value[0] 标识存储类型，仅 PTR 可继续下钻
        if (got < 1) {
          rc = LMJCORE_ERROR_INVALID_TYPE;
        } else if (probe[0] != LMJCORE_VALUE_TYPE_PTR) {
          api_value_type_t found;
          if (tag_to_api_type(probe[0], &found)) {
            err_set_type(err, found);
          }
          rc = LMJCORE_ERROR_TYPE_MISMATCH; // RAW / NULL：不是引用
        } else if (got < 1 + LMJCORE_PTR_LEN) {
          rc = LMJCORE_ERROR_INVALID_TYPE; // PTR 编码长度不足
        }
      }
      if (rc != LMJCORE_SUCCESS) {
        fail_index = i;
        goto fail;
      }

      // 引用载荷首字节是实体类型：只允许继续访问对象
      memcpy(cur, probe + 1, LMJCORE_PTR_LEN);
      if (cur[0] != LMJCORE_OBJ) {
        if (cur[0] == LMJCORE_SET) {
          err_set_type(err, VALUE_TYPE_SET);
          rc = LMJCORE_ERROR_SET_NOT_SUPPORTED;
        } else {
          rc = LMJCORE_ERROR_TYPE_MISMATCH;
        }
        fail_index = i;
        goto fail;
      }
      continue;
    }

    // ---- 叶子段：读取完整值并解码 ----
    uint8_t *value_buf = NULL;
    size_t value_len = 0;
    rc = lmjcore_obj_member_get_capped(txn, cur, name, name_len,
                                       opts.max_value_bytes, &value_buf,
                                       &value_len);
    if (rc != LMJCORE_SUCCESS) {
      fail_index = i;
      goto fail;
    }

    char *value = NULL;
    api_value_type_t type = VALUE_TYPE_NULL;
    rc = lmjcore_decode_value(value_buf, value_len, &value, &type);
    free(value_buf);
    if (rc != LMJCORE_SUCCESS) {
      fail_index = i;
      goto fail;
    }

    *out_value = value;
    *out_type = type;
  }

  segments_free(segs.segments, segs.count);
  return LMJCORE_SUCCESS;

fail:
  err_set(err, segs.segments, segs.count, fail_index);
  segments_free(segs.segments, segs.count);
  return rc;
}
