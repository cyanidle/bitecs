#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef __uint128_t bitecs_mask_t;
typedef uint64_t bitecs_dict_t;

#define BITECS_GROUP_SIZE 32
#define BITECS_GROUP_SHIFT 5
#define BITECS_GROUPS_COUNT 4
#define BITECS_FREQUENCY_ADJUST 3
#define BITECS_MAX_COMPONENTS 2048

typedef struct bitecs_Entity
{
    uint32_t generation;
    uint32_t flags;
    bitecs_dict_t dict; // which groups of 32 bits are active out of 64 total
    bitecs_mask_t components; // 4 groups of 16 bits
} bitecs_Entity;

typedef struct bitecs_Ranks {
    bitecs_dict_t masks[BITECS_GROUPS_COUNT];
    uint8_t ranks[BITECS_GROUPS_COUNT];
    int popcount;
} bitecs_Ranks;

typedef struct bitecs_registry bitecs_registry;

bitecs_registry* bitecs_registry_new();
void bitecs_registry_delete(bitecs_registry* reg);
// frequency: 1-9. How frequent is this component. Position may be very frequent -> is a 9
bool bitecs_registry_add_component(bitecs_registry* reg, int index, size_t tsize, int frequency);

typedef void (*bitecs_RangeSystem)(float deltaTime, void* begin, void* end);

// void** ptrStorage: should be an array of void*[N*2]: N - number of components queried at the same time
void bitecs_registry_run_system(bitecs_dict_t dict, bitecs_mask_t query, void** ptrStorage, bitecs_RangeSystem system);

void bitecs_get_ranks(bitecs_dict_t dict, bitecs_Ranks* out);

size_t bitecs_query_match(size_t cursor, bitecs_dict_t dict, bitecs_mask_t query, const bitecs_Entity* entts, size_t count);
size_t bitecs_query_miss(size_t cursor, bitecs_dict_t dict, bitecs_mask_t query, const bitecs_Entity* entts, size_t count);


bool bitecs_flag_set(int index, bool state, bitecs_dict_t* dict, bitecs_mask_t* mask);
bool bitecs_flag_get(int index, bitecs_dict_t dict, bitecs_mask_t mask);


#ifdef __cplusplus
}
#endif
