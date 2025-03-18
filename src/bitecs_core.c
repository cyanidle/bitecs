#include "bitecs_core.h"
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

static bitecs_dict_t fill_up_to(int bit) {
    return ((bitecs_dict_t)(1) << bit) - (bitecs_dict_t)1;
}

void bitecs_get_ranks(bitecs_dict_t dict, bitecs_Ranks* res)
{
    *res = (bitecs_Ranks){0};
    int out = 0;
    while(dict) {
        if (dict & 1) {
            int i = res->popcount++;
            res->ranks[i] = out;
            res->masks[i] = fill_up_to(out);
        }
        dict >>= 1;
        out++;
    }
}

typedef bitecs_mask_t mask_t;
typedef bitecs_Ranks Ranks;
typedef bitecs_Entity Entity;

static mask_t relocate_part(bitecs_dict_t dictDiff, mask_t mask, int index, const bitecs_dict_t* restrict rankMasks) {
    bitecs_dict_t select_mask = rankMasks[index];
    int shift = __builtin_popcount(dictDiff & select_mask) * BITECS_GROUP_SIZE;
    int value_mask_offset = index * BITECS_GROUP_SIZE;
    mask_t value_mask = (mask_t)fill_up_to(BITECS_GROUP_SIZE) << value_mask_offset;
    mask_t value = mask & value_mask;
    return value << shift;
}

static mask_t adjust_for(bitecs_dict_t dict, bitecs_dict_t qdict, mask_t qmask, const bitecs_dict_t* restrict rankMasks) {
    bitecs_dict_t diff = dict ^ qdict;
    mask_t r0 = relocate_part(diff, qmask, 0, rankMasks);
    mask_t r1 = relocate_part(diff, qmask, 1, rankMasks);
    mask_t r2 = relocate_part(diff, qmask, 2, rankMasks);
    mask_t r3 = relocate_part(diff, qmask, 3, rankMasks);
    return r0 | r1 | r2 | r3;
}

static bool needs_adjust(bitecs_dict_t dict, bitecs_dict_t qdict, const bitecs_dict_t* restrict rankMasks)
{
    bitecs_dict_t diff = dict ^ qdict;
    return diff & rankMasks[BITECS_GROUPS_COUNT - 1];
    //if any relocations (at least on biggest mask) -> dicts are incompatible
}

size_t bitecs_query_match(size_t cursor, bitecs_dict_t dict, bitecs_mask_t query, const bitecs_Entity* entts, size_t count)
{
    Ranks ranks;
    bitecs_get_ranks(dict, &ranks);
    for (;cursor < count; ++count) {
        const Entity* entt = entts + cursor;
        if ((entt->dict & dict) != dict) continue;
        mask_t mask = needs_adjust(entt->dict, dict, ranks.masks)
                          ? adjust_for(entt->dict, dict, query, ranks.masks)
                          : query;
        if ((entt->components & mask) == mask) {
            return cursor;
        }
    }
    return cursor;
}

size_t bitecs_query_miss(size_t cursor, bitecs_dict_t dict, bitecs_mask_t query, const bitecs_Entity* entts, size_t count)
{
    Ranks ranks;
    bitecs_get_ranks(dict, &ranks);
    for (;cursor < count; ++count) {
        const Entity* entt = entts + cursor;
        if ((entt->dict & dict) != dict) return cursor;
        mask_t mask = needs_adjust(entt->dict, dict, ranks.masks)
                          ? adjust_for(entt->dict, dict, query, ranks.masks)
                          : query;
        if ((entt->components & mask) != mask) {
            return cursor;
        }
        // we cannot save adjusted version to avoid frequents adjusts -> adjust works one way
        // we may save it, but fallback to original as we encouter missmatch (to confirm it)
    }
    return cursor;
}

bool bitecs_flag_set(int index, bool state, bitecs_dict_t* dict, bitecs_mask_t* mask)
{
    int group = index >> BITECS_GROUP_SHIFT;
    int bit = index & fill_up_to(BITECS_GROUP_SHIFT);
    if (unlikely(!(*dict & ((bitecs_dict_t)1 << group)))) {
        Ranks ranks;
        bitecs_get_ranks(*dict, &ranks);
        if (unlikely(ranks.popcount == 8)) {
            return false;
        }
        bitecs_dict_t newDict = *dict | ((bitecs_dict_t)1 << group);
        *mask = adjust_for(newDict, *dict, *mask, ranks.masks);
        *dict = newDict;
    }
    int groupIndex = __builtin_popcount(*dict & fill_up_to(group));
    int shift = groupIndex * BITECS_GROUP_SIZE + bit;
    if (state) {
        *mask |= (bitecs_dict_t)1 << shift;
    } else {
        *mask &= ~((bitecs_dict_t)1 << shift);
    }
    return true;
}

bool bitecs_flag_get(int index, bitecs_dict_t dict, bitecs_mask_t mask)
{
    int group = index >> BITECS_GROUP_SHIFT;
    int bit = index & fill_up_to(BITECS_GROUP_SHIFT);
    int groupIndex = __builtin_popcount(dict & fill_up_to(group));
    int shift = groupIndex * BITECS_GROUP_SIZE + bit;
    return mask & (bitecs_dict_t)1 << shift;
}

typedef struct component_list
{
    void** chunks; //array of memory_blocks of size: chunk_sizeof(this)
    size_t chunks_count;
    int frequency;
    int comp_sizeof;
} component_list;

static size_t components_in_chunk(component_list* list) {
    return (size_t)1 << (list->frequency + BITECS_FREQUENCY_ADJUST);
}

static size_t chunk_sizeof(component_list* list) {
    return components_in_chunk(list) * list->comp_sizeof;
}

static component_list* list_new(int freq) {
    component_list* res = malloc(sizeof(component_list));
    if (!res) return res;

    res->frequency = freq;
    return res;
}

static void list_destroy(component_list* list)
{
    if (!list) return;
    for (size_t i = 0; i < list->chunks_count; ++i) {
        if (list->chunks[i]) {
            free(list->chunks[i]);
        }
    }
}


struct bitecs_registry
{
    Entity* entities;
    size_t entities_count;
    component_list* components[BITECS_MAX_COMPONENTS];
};

bool bitecs_registry_add_component(bitecs_registry* reg, int index, size_t tsize, int frequency)
{
    assert(frequency >= 1 && frequency <= 9);
    if (reg->components[index]) return false;
}

bitecs_registry* bitecs_registry_new()
{
    bitecs_registry* result = malloc(sizeof(bitecs_registry));
    if (!result) return result;
    *result = (bitecs_registry){0};
    return result;
}


void bitecs_registry_delete(bitecs_registry* reg)
{
    if (!reg) return;
    if (reg->entities) {
        free(reg->entities);
    }
    for (int i = 0; i < BITECS_MAX_COMPONENTS; ++i) {
        list_destroy(reg->components[i]);
    }
    *reg = (bitecs_registry){0};
    free(reg);
}

void bitecs_registry_run_system(bitecs_dict_t dict, bitecs_mask_t query, void** ptrStorage, bitecs_RangeSystem system)
{
    Ranks ranks;
    bitecs_get_ranks(dict, &ranks);
    // TODO:
}
