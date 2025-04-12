// MIT License. See LICENSE file for details
// Copyright (c) 2025 Доронин Алексей

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __GNUC__
#define _BITECS_NOINLINE __attribute((noinline))
#define _BITECS_INLINE __attribute((always_inline))
#define _BITECS_FLATTEN __attribute((flatten))
#define _BITECS_NODISCARD __attribute((__warn_unused_result__))
#else
#define _BITECS_NOINLINE
#define _BITECS_INLINE
#define _BITECS_FLATTEN
#define _BITECS_NODISCARD
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BITECS_INDEX_T
#define BITECS_INDEX_T uint32_t
#endif

#define BITECS_GROUP_SIZE 16
#define BITECS_GROUP_SHIFT 4
#define BITECS_GROUPS_COUNT 4
#define BITECS_FREQUENCY_ADJUST 5
#define BITECS_BITS_IN_DICT 64
#define BITECS_MAX_COMPONENTS (BITECS_GROUP_SIZE * BITECS_BITS_IN_DICT)

typedef uint64_t bitecs_mask_t;
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
    bitecs_dict_t dict;
    bitecs_mask_t bits;
} bitecs_SparseMask;

typedef struct
{
    // which groups of 32 bits are active out of 64 total (max components registered: 2048)
    bitecs_dict_t dict;
    // 4 groups of 32 bits. 4-128 active on single entt at the same time
    bitecs_mask_t components;
    // generation make all EntityPtr weak references (check if this actually is still alive entt)
    bitecs_generation_t generation;
    // user-defined flags
    bitecs_flags_t flags;
} bitecs_Entity;

typedef struct
{
    const bitecs_dict_t dict;
    const bitecs_mask_t components;
    const bitecs_generation_t generation;
    bitecs_flags_t flags;
} bitecs_EntityProxy;

typedef struct {
    bitecs_dict_t select_dict_masks[BITECS_GROUPS_COUNT];
    int group_ranks[BITECS_GROUPS_COUNT];
    bitecs_dict_t highest_select_mask;
    int groups_count;
} bitecs_Ranks;

void bitecs_ranks_get(bitecs_Ranks* out, bitecs_dict_t dict);

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

// from will be consumed (all entities and components moved to reg)
// (but settings will stay the same)
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
    void (*relocater)(void* begin, bitecs_index_t count, void* out);
} bitecs_ComponentMeta;

_BITECS_NODISCARD
bool bitecs_component_define(bitecs_registry* reg, bitecs_comp_id_t id, bitecs_ComponentMeta meta);

typedef struct {
    bitecs_index_t index;
    bitecs_EntityProxy* entts;
} bitecs_CallbackContext;

typedef void* __restrict__ * __restrict__ bitecs_ptrs;
typedef void* __restrict__ bitecs_udata;

typedef void (*bitecs_Callback)(bitecs_udata udata, bitecs_CallbackContext* ctx, bitecs_ptrs begins, bitecs_index_t count);


typedef struct
{
    bitecs_SparseMask mask;
    const int* components;
    int ncomps;
} bitecs_ComponentsList;

_BITECS_NODISCARD
bool bitecs_entt_create(
    bitecs_registry* reg, bitecs_index_t count,
    const bitecs_ComponentsList* components,
    bitecs_Callback creator, void* udata);

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

typedef struct {
    bitecs_flags_t flags;
    const bitecs_ComponentsList* comps;
    bitecs_Callback system;
    void* udata;
} bitecs_SystemParams;

typedef struct bitecs_threadpool bitecs_threadpool;

void bitecs_system_run(bitecs_registry* reg, bitecs_SystemParams* params);

typedef struct {
    bitecs_SystemParams* params;
    size_t nsystems;
} bitecs_MultiSystemParams;

void bitecs_system_run_many(bitecs_registry* registry, bitecs_threadpool* tpool, bitecs_MultiSystemParams* systems);

_BITECS_NODISCARD bool bitecs_mask_from_array(bitecs_SparseMask *maskOut, const int *idxs, int idxs_count);
_BITECS_NODISCARD bool bitecs_mask_set(bitecs_SparseMask* mask, int index, bool state);
_BITECS_NODISCARD bool bitecs_mask_get(const bitecs_SparseMask* mask, int index);

typedef int bitecs_BitsStorage[128];
// returns N bits, that got written to storage*
_BITECS_NODISCARD
int bitecs_mask_into_array(const bitecs_SparseMask* mask, const bitecs_Ranks* ranks, int* storage);


typedef struct bitecs_cleanup_data bitecs_cleanup_data;

// Deferred cleanup API:
_BITECS_NODISCARD bitecs_cleanup_data* bitecs_cleanup_prepare(bitecs_registry* reg);
void bitecs_cleanup(bitecs_registry* reg, bitecs_cleanup_data* data);


bitecs_threadpool* bitecs_threadpool_new(size_t nthreads);
void bitecs_threadpool_delete(bitecs_threadpool* tpool);


#ifdef __cplusplus
}
#endif
