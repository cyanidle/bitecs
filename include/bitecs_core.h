#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
#define _BITECS_NOEXCEPT noexcept
extern "C" {
#else
#define _BITECS_NOEXCEPT
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
#define BITECS_BITS_IN_DICT 64
#define BITECS_MAX_COMPONENTS (BITECS_GROUP_SIZE * BITECS_BITS_IN_DICT)
#define BITECS_COMPONENTS_CHUNK_ALIGN 16

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
    uint32_t flags; // can have "dirty" flag?
} bitecs_Entity;

typedef struct bitecs_Ranks {
    bitecs_dict_t select_dict_masks[BITECS_GROUPS_COUNT];
    int group_ranks[BITECS_GROUPS_COUNT];
    int groups_count;
} bitecs_Ranks;

typedef struct bitecs_registry bitecs_registry;

bitecs_registry* bitecs_registry_new(void) _BITECS_NOEXCEPT;
void bitecs_registry_delete(bitecs_registry* reg) _BITECS_NOEXCEPT;

// for loading stuff in background:
// 1) create clone with same registered components from main
// 2) do stuff with it (create entts + components on them)
// 3) merge it into main one
bool bitecs_registry_clone_settings(bitecs_registry* reg, bitecs_registry* out) _BITECS_NOEXCEPT;
bool bitecs_registry_merge_other(bitecs_registry* reg, bitecs_registry* from) _BITECS_NOEXCEPT;

typedef struct bitecs_ComponentMeta {
    // sizeof(T) of a single component
    size_t typesize;
    // frequency: 1-9. How frequent is this component.
    int frequency;
    void (*deleter)(void* begin, bitecs_index_t count);
} bitecs_ComponentMeta;

bool bitecs_component_define(bitecs_registry* reg, bitecs_comp_id_t id, bitecs_ComponentMeta meta) _BITECS_NOEXCEPT;

typedef bool (*bitecs_RangeCreator)(void* udata, bitecs_comp_id_t id, void* begin, void* end) _BITECS_NOEXCEPT;

bool bitecs_entt_create_batch(
    bitecs_registry* reg, bitecs_index_t count,
    bitecs_EntityPtr* outBegin,
    const int* comps, int ncomps,
    bitecs_RangeCreator creator, void* udata) _BITECS_NOEXCEPT;

typedef bool (*bitecs_SingleCreator)(void* udata, bitecs_comp_id_t id, void* component) _BITECS_NOEXCEPT;

// all except reg can be null
bool bitecs_entt_create(
    bitecs_registry* reg, bitecs_EntityPtr* outPtr,
    const int* comps, int ncomps,
    bitecs_SingleCreator creator, void* udata) _BITECS_NOEXCEPT;

bool bitecs_entt_destroy(bitecs_registry* reg, bitecs_EntityPtr ptr) _BITECS_NOEXCEPT;
bool bitecs_entt_destroy_batch(bitecs_registry* reg, bitecs_index_t ptr, bitecs_index_t count) _BITECS_NOEXCEPT;

// returns void* to be used for initialization. or null if already exists/error
void* bitecs_entt_add_component(bitecs_registry* reg, bitecs_EntityPtr ptr, bitecs_comp_id_t id) _BITECS_NOEXCEPT;
bool bitecs_entt_remove_component(bitecs_registry* reg, bitecs_EntityPtr ptr, bitecs_comp_id_t id) _BITECS_NOEXCEPT;
void* bitecs_entt_get_component(bitecs_registry* reg, bitecs_EntityPtr ptr, bitecs_comp_id_t id) _BITECS_NOEXCEPT;

typedef void (*bitecs_RangeSystem)(void* udata, void** begins, bitecs_index_t count) _BITECS_NOEXCEPT;

void bitecs_system_run(
    bitecs_registry* reg,
    const int* components, int ncomps,
    bitecs_RangeSystem system, void* udata) _BITECS_NOEXCEPT;

typedef struct bitecs_SystemStepCtx
{
    void** ptrStorage; //should have space for ncomps: void*
    const int* components;
    int ncomps;
    bitecs_RangeSystem system;
    void* udata;
    bitecs_SparseMask query;
    bitecs_Ranks ranks;
    bitecs_index_t cursor;
} bitecs_SystemStepCtx;

bool bitecs_system_step(bitecs_registry* reg, bitecs_SystemStepCtx* ctx) _BITECS_NOEXCEPT;

bitecs_index_t bitecs_entts_count(const bitecs_registry* reg) _BITECS_NOEXCEPT;

void bitecs_ranks_get(bitecs_Ranks* out, bitecs_dict_t dict) _BITECS_NOEXCEPT;

bitecs_index_t bitecs_query_match(
    bitecs_index_t cursor,
    const bitecs_SparseMask* query, const bitecs_Ranks* ranks,
    const bitecs_Entity* entts, bitecs_index_t count) _BITECS_NOEXCEPT;

bitecs_index_t bitecs_query_miss(
    bitecs_index_t cursor,
    const bitecs_SparseMask* query, const bitecs_Ranks* ranks,
    const bitecs_Entity* entts, bitecs_index_t count) _BITECS_NOEXCEPT;

// idxs must be sorted!
bool bitecs_mask_from_array(bitecs_SparseMask *maskOut, const int *idxs, int idxs_count) _BITECS_NOEXCEPT;
bool bitecs_mask_set(bitecs_SparseMask* mask, int index, bool state) _BITECS_NOEXCEPT;
bool bitecs_mask_get(const bitecs_SparseMask* mask, int index) _BITECS_NOEXCEPT;

typedef int bitecs_BitsStorage[128];
// returns N bits, that got written to storage*
int bitecs_mask_into_array(
    const bitecs_SparseMask* mask, const bitecs_Ranks* ranks, bitecs_BitsStorage* storage) _BITECS_NOEXCEPT;

void _bitecs_sanity_test(bitecs_SparseMask* out) _BITECS_NOEXCEPT;

#ifdef __cplusplus
}
#endif
