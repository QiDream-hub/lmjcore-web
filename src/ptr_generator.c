#include "../include/ptr_generator.h"
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <uuid/uuid.h>

// 简单的递增计数器生成器（仅用于测试）
static uint64_t g_counter = 0;

int lmjcore_ptr_generator_counter(void *ctx, uint8_t out[LMJCORE_PTR_LEN]) {
  memset(out, 0, LMJCORE_PTR_LEN);
  out[0] = *(uint8_t *)ctx;

  // 使用时间戳 + 计数器
  uint64_t timestamp = (uint64_t)time(NULL);
  uint64_t counter = __sync_fetch_and_add(&g_counter, 1);

  memcpy(out + 1, &timestamp, 8);
  memcpy(out + 9, &counter, 8);

  return LMJCORE_SUCCESS;
}

// UUIDv4 生成器
int lmjcore_ptr_generator_uuid(void *ctx, uint8_t out[LMJCORE_PTR_LEN]) {
  if (LMJCORE_PTR_LEN < 17) {
    return LMJCORE_ERROR_BUFFER_TOO_SMALL;
  }

  (void)ctx;

  uuid_t uuid;
  uuid_generate_random(uuid);

  // 复制 UUID (16 字节)
  memcpy(out + 1, uuid, 16);

  return LMJCORE_SUCCESS;
}
