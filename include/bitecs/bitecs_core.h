// MIT License. See LICENSE file for details
// Copyright (c) 2025 Доронин Алексей

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __GNUC__
#define _BITECS_FLATTEN __attribute((flatten))
#define _BITECS_NODISCARD __attribute__ ((__warn_unused_result__))
#else
#define _BITECS_FLATTEN
#define _BITECS_NODISCARD
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BITECS_INDEX_T
#define BITECS_INDEX_T uint32_t
#endif

#define BITECS_GROUP_SIZE 32
#define BITECS_GROUP_SHIFT 5
#define BITECS_GROUPS_COUNT 4
#define BITECS_FREQUENCY_ADJUST 5
#define BITECS_BITS_IN_DICT 64
#define BITECS_MAX_COMPONENTS (BITECS_GROUP_SIZE * BITECS_BITS_IN_DICT)
#define BITECS_COMPONENTS_CHUNK_ALIGN 16


typedef __uint128_t bitecs_mask_t;
typedef uint64_t bitecs_dict_t;
typedef BITECS_INDEX_T bitecs_index_t;
typedef uint32_t bitecs_generation_t;
typedef uint32_t bitecs_flags_t;
typedef int bitecs_comp_id_t;

typedef struct
{
    bitecs_generation_t generation;
    bitecs_index_t index;
} bitecs_EntityPtr;

typedef struct
{
    bitecs_mask_t bits;
    bitecs_dict_t dict;
} bitecs_SparseMask;

typedef struct
{
    // 4 groups of 32 bits. 4-128 active on single entt at the same time
    bitecs_mask_t components;
    // which groups of 32 bits are active out of 64 total (max components registered: 2048)
    bitecs_dict_t dict;
    // generation make all EntityPtr weak references (check if this actually is still alive entt)
    bitecs_generation_t generation;
    // user-defined flags
    bitecs_flags_t flags;
} bitecs_Entity;

typedef struct
{
    const bitecs_mask_t r_components;
    const bitecs_dict_t r_dict;
    const bitecs_generation_t r_generation;
    bitecs_flags_t flags;
} bitecs_EntityProxy;

typedef struct {
    bitecs_dict_t select_dict_masks[BITECS_GROUPS_COUNT];
    int group_ranks[BITECS_GROUPS_COUNT];
    int groups_count;
} bitecs_Ranks;

typedef struct bitecs_registry bitecs_registry;

_BITECS_NODISCARD
bitecs_registry* bitecs_registry_new(void);
void bitecs_registry_delete(bitecs_registry* reg);

// for loading stuff in background:
// 1) create clone with same registered components from main
// 2) do stuff with it (create entts + components on them)
// 3) merge it into main one
_BITECS_NODISCARD
bool bitecs_registry_clone_settings(bitecs_registry* reg, bitecs_registry* out);
_BITECS_NODISCARD
bool bitecs_registry_merge_other(bitecs_registry* reg, bitecs_registry* from);

typedef enum {
    bitecs_freq1 = 1,
    bitecs_freq2,
    bitecs_freq3,
    bitecs_freq4,
    bitecs_freq5,
    bitecs_freq6,
    bitecs_freq7,
    bitecs_freq8,
    bitecs_freq9,
} bitecs_Frequency;

typedef struct {
    // sizeof(T) of a single component
    size_t typesize;
    // frequency: 1-9. How frequent is this component.
    bitecs_Frequency frequency;
    void (*deleter)(void* begin, bitecs_index_t count);
} bitecs_ComponentMeta;

_BITECS_NODISCARD
bool bitecs_component_define(bitecs_registry* reg, bitecs_comp_id_t id, bitecs_ComponentMeta meta);


typedef struct
{
    const int* components;
    int ncomps;
    bitecs_flags_t flags;
    bitecs_SparseMask mask;
    bitecs_Ranks ranks;
    const bitecs_Entity* outEntts;
    bitecs_index_t outIndex;
    bitecs_index_t _cursor;
} bitecs_QueryCtx;

typedef struct {
    bitecs_QueryCtx query;
    bitecs_index_t count;
} bitecs_CreateCtx;

_BITECS_NODISCARD
bool bitecs_entt_create(bitecs_registry* reg, bitecs_CreateCtx* ctx, void** ptrs, size_t* batchSize);
void bitecs_entt_destroy(bitecs_registry* reg, bitecs_EntityPtr ptr);
void bitecs_entt_destroy_batch(bitecs_registry* reg, const bitecs_EntityPtr* ptrs, size_t nptrs);

// returns void* to be used for initialization. or null if already exists/error
_BITECS_NODISCARD
void* bitecs_entt_add_component(bitecs_registry* reg, bitecs_EntityPtr ptr, bitecs_comp_id_t id);
_BITECS_NODISCARD
bool bitecs_entt_remove_component(bitecs_registry* reg, bitecs_EntityPtr ptr, bitecs_comp_id_t id);
_BITECS_NODISCARD
void* bitecs_entt_get_component(bitecs_registry* reg, bitecs_EntityPtr ptr, bitecs_comp_id_t id);

// @warning: do not store this pointer. May be relocated at any time.
_BITECS_NODISCARD bitecs_EntityProxy* bitecs_entt_deref(bitecs_registry* reg, bitecs_EntityPtr ptr);

_BITECS_NODISCARD
bool bitecs_system_step(bitecs_registry* reg, bitecs_QueryCtx* ctx, void** ptrs, size_t* count);

void bitecs_ranks_get(bitecs_Ranks* out, bitecs_dict_t dict);

// idxs must be sorted!
_BITECS_NODISCARD
bool bitecs_mask_from_array(bitecs_SparseMask *maskOut, const int *idxs, int idxs_count);
_BITECS_NODISCARD
bool bitecs_mask_set(bitecs_SparseMask* mask, int index, bool state);
_BITECS_NODISCARD
bool bitecs_mask_get(const bitecs_SparseMask* mask, int index);

typedef int bitecs_BitsStorage[128];
// returns N bits, that got written to storage*
_BITECS_NODISCARD
int bitecs_mask_into_array(const bitecs_SparseMask* mask, const bitecs_Ranks* ranks, bitecs_BitsStorage* storage);

void _bitecs_sanity_test(bitecs_SparseMask* out);

#ifdef __cplusplus
}
#endif
