#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef __uint128_t bitecs_mask_t;
typedef uint64_t bitecs_dict_t;
typedef uint32_t bitecs_index_t;
typedef uint32_t bitecs_generation_t;
typedef int bitecs_comp_id_t;

#define BITECS_GROUP_SIZE 32
#define BITECS_GROUP_SHIFT 5
#define BITECS_GROUPS_COUNT 4
#define BITECS_FREQUENCY_ADJUST 3
#define BITECS_MAX_COMPONENTS 2048

typedef struct bitecs_EntityPtr
{
    bitecs_generation_t generation;
    bitecs_index_t index;
} bitecs_EntityPtr;

typedef struct bitecs_SparseMask
{
    bitecs_mask_t bits;
    bitecs_dict_t dict;
} bitecs_SparseMask;

typedef struct bitecs_Entity
{
    bitecs_mask_t components; // 4 groups of 16 bits
    bitecs_dict_t dict; // which groups of 32 bits are active out of 64 total
    bitecs_generation_t generation;
    uint32_t flags;
} bitecs_Entity;

typedef struct bitecs_Ranks {
    bitecs_dict_t select_dict_masks[BITECS_GROUPS_COUNT];
    uint8_t ranks[BITECS_GROUPS_COUNT];
    int popcount;
} bitecs_Ranks;

typedef struct bitecs_registry bitecs_registry;

bitecs_registry* bitecs_registry_new(void);
void bitecs_registry_delete(bitecs_registry* reg);

typedef struct bitecs_ComponentMeta {
    size_t typesize;
    int frequency;
    void (*deleter)(void* begin, void* end);
} bitecs_ComponentMeta;

// frequency: 1-9. How frequent is this component. Position may be very frequent -> is a 9
bool bitecs_registry_define_component(bitecs_registry* reg, bitecs_comp_id_t index, bitecs_ComponentMeta meta);

typedef void(*bitecs_RangeCreator)(void* udata, bitecs_comp_id_t id, void* begin, void* end, bitecs_index_t beginIndex);

bool bitecs_entt_create_batch(
    bitecs_registry* reg, bitecs_index_t count,
    bitecs_dict_t dict, bitecs_mask_t mask,
    bitecs_RangeCreator creator, void* udata);

typedef void(*bitecs_SingleCreator)(void* udata, bitecs_comp_id_t id, void* component);

// all except reg can be null
bool bitecs_entt_create(
    bitecs_registry* reg, bitecs_EntityPtr* outPtr,
    bitecs_dict_t dict, bitecs_mask_t mask,
    bitecs_SingleCreator creator, void* udata);

// returns void* to be used for initialization. or null if already exists/error
void* bitecs_entt_add_component(bitecs_registry* reg, bitecs_EntityPtr entt, bitecs_comp_id_t id);
void bitecs_entt_remove_component(bitecs_registry* reg, bitecs_EntityPtr entt, bitecs_comp_id_t id);
void* bitecs_entt_get_component(bitecs_registry* reg, bitecs_EntityPtr entt, bitecs_comp_id_t id);

typedef void (*bitecs_RangeSystem)(void* udata, void** begins, void** ends);

// void** ptrStorage: should be an array of void*[N*2]: N - number of components queried at the same time
void bitecs_system_run(const bitecs_SparseMask* query, bitecs_RangeSystem system, void* udata);

void bitecs_get_ranks(bitecs_dict_t dict, bitecs_Ranks* out);

size_t bitecs_query_match(
    size_t cursor,
    const bitecs_SparseMask* query, const bitecs_Ranks* ranks,
    const bitecs_Entity* entts, size_t count);

size_t bitecs_query_miss(
    size_t cursor,
    const bitecs_SparseMask* query, const bitecs_Ranks* ranks,
    const bitecs_Entity* entts, size_t count);

// idxs must be sorted!
bool bitecs_mask_from_array(bitecs_SparseMask *maskOut, int* idxs, int idxs_count);
bool bitecs_mask_set(int index, bitecs_SparseMask* mask, bool state);
bool bitecs_mask_get(int index, const bitecs_SparseMask* mask);

void _bitecs_sanity_test(bitecs_SparseMask* out);


// TODO: registry merge operation

#ifdef __cplusplus
}
#endif
