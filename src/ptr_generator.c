#include "../thirdparty/LMJCore/core/include/lmjcore.h"
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

  out[0] = *(uint8_t *)ctx;

  uuid_t uuid;
  uuid_generate_random(uuid);

  // 复制 UUID (16 字节)
  memcpy(out + 1, uuid, 16);

  return LMJCORE_SUCCESS;
}

// 自定义生成器示例
static uint64_t g_instance_id = 0;

int lmjcore_ptr_generator_with_instance(void *ctx,
                                        uint8_t out[LMJCORE_PTR_LEN]) {
  memset(out, 0, LMJCORE_PTR_LEN);
  out[0] = *(uint8_t *)ctx;

  // 格式: [type][instance_id(1B)][timestamp(8B)][counter(7B)]
  uint8_t instance_id = g_instance_id % 256;
  out[1] = instance_id;

  uint64_t timestamp = (uint64_t)time(NULL);
  memcpy(out + 2, &timestamp, 8);

  static uint64_t counter = 0;
  uint64_t cnt = __sync_fetch_and_add(&counter, 1);
  memcpy(out + 10, &cnt, 7);

  return LMJCORE_SUCCESS;
}

// 设置实例 ID
void lmjcore_ptr_set_instance_id(uint64_t id) { g_instance_id = id; }