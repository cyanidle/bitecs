#ifndef BITECS_PRIVATE_H
#define BITECS_PRIVATE_H

#include "bitecs_core.h"
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)
#define popcnt(x) __builtin_popcount(x)
#define clz(x) __builtin_clz(x)
#define ctz(x) __builtin_ctz(x)

typedef bitecs_mask_t mask_t;
typedef bitecs_index_t index_t;
typedef bitecs_dict_t dict_t;
typedef bitecs_generation_t generation_t;
typedef bitecs_Ranks Ranks;
typedef bitecs_Entity Entity;
typedef bitecs_SparseMask SparseMask;

static inline dict_t fill_up_to(int bit) {
    return ((dict_t)(1) << bit) - (dict_t)1;
}

#endif // BITECS_PRIVATE_H
