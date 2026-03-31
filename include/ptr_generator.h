#include "../thirdparty/LMJCore/core/include/lmjcore.h"
// ptr_generator.h
#ifndef PTR_GENERATOR_H
#define PTR_GENERATOR_H

// 函数声明
int lmjcore_ptr_generator_counter(void *ctx, uint8_t out[LMJCORE_PTR_LEN]);
int lmjcore_ptr_generator_uuid(void *ctx, uint8_t out[LMJCORE_PTR_LEN]);

#endif