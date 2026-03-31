#ifndef CHAIN_QUERY_H
#define CHAIN_QUERY_H

#include "../thirdparty/LMJCore/core/include/lmjcore.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 查询结果类型
typedef enum {
  QUERY_RESULT_VALUE,    // 普通值
  QUERY_RESULT_PTR,      // 指针
  QUERY_RESULT_SET_ALL,  // 集合全部元素
  QUERY_RESULT_NOT_FOUND // 未找到
} query_result_type_t;

// 查询结果
typedef struct {
  query_result_type_t type;
  uint8_t *data;
  size_t data_len;
  lmjcore_ptr ptr_value; // 当 type == PTR 时有效
} query_result_t;

// 链式查询上下文
typedef struct {
  lmjcore_txn *txn;
  lmjcore_ptr root_ptr;
  char *path;
  int max_depth;
  query_result_t *result;
} chain_query_ctx_t;

// 解析链式查询
int chain_query_parse(lmjcore_txn *txn, const lmjcore_ptr root_ptr,
                      const char *path, query_result_t *result);

// 获取集合所有元素
int chain_query_get_set_all(lmjcore_txn *txn, const lmjcore_ptr set_ptr,
                            uint8_t **out_data, size_t *out_len);

// 释放查询结果
void chain_query_result_free(query_result_t *result);

// 路径验证
bool chain_query_is_valid_path(const char *path);

// 路径分段
int chain_query_split_path(const char *path, char **segments, int max_segments);

#endif // CHAIN_QUERY_H