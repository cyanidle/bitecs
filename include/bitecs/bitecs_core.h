#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __GNUC__
#define _BITECS_NODISCARD __attribute__ ((__warn_unused_result__))
#else
#define _BITECS_NODISCARD
#endif

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
#define BITECS_BITS_IN_DICT 64
#define BITECS_MAX_COMPONENTS (BITECS_GROUP_SIZE * BITECS_BITS_IN_DICT)
#define BITECS_COMPONENTS_CHUNK_ALIGN 16

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
    bitecs_mask_t components; // 4 groups of 16 bits
    bitecs_dict_t dict; // which groups of 32 bits are active out of 64 total
    bitecs_generation_t generation;
    uint32_t flags; // can have "dirty" flag?
} bitecs_Entity;

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
    bitecs_rare = 1,
    bitecs_freq2,
    bitecs_freq3,
    bitecs_freq4,
    bitecs_freq5,
    bitecs_freq6,
    bitecs_freq7,
    bitecs_freq8,
    bitecs_frequent,
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

typedef struct {
    bitecs_index_t beginIndex;
    const bitecs_Entity* entts;
} bitecs_CallbackContext;

typedef void (*bitecs_Callback)(void* udata, bitecs_CallbackContext* ctx, void** begins, bitecs_index_t count);

_BITECS_NODISCARD
bool bitecs_entt_create(
    bitecs_registry* reg, bitecs_index_t count,
    const int* comps, int ncomps,
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


void bitecs_system_run(
    bitecs_registry* reg,
    const int* components, int ncomps,
    bitecs_Callback system, void* udata);

_BITECS_NODISCARD
bool bitecs_check_components(bitecs_registry* reg, const int* components, int ncomps);

typedef struct
{
    void** ptrStorage; //should have space for void*[ncomps]
    const int* components;
    int ncomps;
    bitecs_Callback system;
    void* udata;
    bitecs_SparseMask query;
    bitecs_Ranks ranks;
    bitecs_index_t cursor;
} bitecs_SystemStepCtx;

_BITECS_NODISCARD
bool bitecs_system_step(bitecs_registry* reg, bitecs_SystemStepCtx* ctx);

void bitecs_ranks_get(bitecs_Ranks* out, bitecs_dict_t dict);

_BITECS_NODISCARD
bitecs_index_t bitecs_query_match(
    bitecs_index_t cursor,
    const bitecs_SparseMask* query, const bitecs_Ranks* ranks,
    const bitecs_Entity* entts, bitecs_index_t count);

_BITECS_NODISCARD
bitecs_index_t bitecs_query_miss(
    bitecs_index_t cursor,
    const bitecs_SparseMask* query, const bitecs_Ranks* ranks,
    const bitecs_Entity* entts, bitecs_index_t count);

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
int bitecs_mask_into_array(
    const bitecs_SparseMask* mask, const bitecs_Ranks* ranks, bitecs_BitsStorage* storage);

void _bitecs_sanity_test(bitecs_SparseMask* out);

#ifdef __cplusplus
}
#endif
