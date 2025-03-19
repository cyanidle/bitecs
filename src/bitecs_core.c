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
            res->select_dict_masks[i] = fill_up_to(out);
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
    mask_t value_mask = (mask_t)fill_up_to(BITECS_GROUP_SIZE);
    mask_t value_mask_shifted = value_mask << value_mask_offset;
    mask_t value = mask & value_mask_shifted;
    return value << shift;
}

static mask_t adjust_for(bitecs_dict_t diff, mask_t qmask, const bitecs_dict_t* restrict rankMasks) {
    mask_t r0 = relocate_part(diff, qmask, 0, rankMasks);
    mask_t r1 = relocate_part(diff, qmask, 1, rankMasks);
    mask_t r2 = relocate_part(diff, qmask, 2, rankMasks);
    mask_t r3 = relocate_part(diff, qmask, 3, rankMasks);
    return r0 | r1 | r2 | r3;
}

static bool needs_adjust(bitecs_dict_t diff, const bitecs_dict_t* restrict rankMasks)
{
    return diff & rankMasks[BITECS_GROUPS_COUNT - 1];
    //if any relocations (at least on biggest mask) -> dicts are incompatible
}

size_t bitecs_query_match(
    size_t cursor, const bitecs_SparseMask* query,
    const bitecs_Ranks *ranks, const bitecs_Entity* entts, size_t count)
{
    for (;cursor < count; ++count) {
        const Entity* entt = entts + cursor;
        if ((entt->dict & query->dict) != query->dict) continue;
        bitecs_dict_t diff = entt->dict ^ query->dict;
        mask_t mask = needs_adjust(diff, ranks->select_dict_masks)
                          ? adjust_for(diff, query->bits, ranks->select_dict_masks)
                          : query->bits;
        if ((entt->components & mask) == mask) {
            return cursor;
        }
    }
    return cursor;
}

size_t bitecs_query_miss(
    size_t cursor, const bitecs_SparseMask* query,
    const bitecs_Ranks *ranks, const bitecs_Entity* entts, size_t count)
{
    for (;cursor < count; ++count) {
        const Entity* entt = entts + cursor;
        if ((entt->dict & query->dict) != query->dict) return cursor;
        bitecs_dict_t diff = entt->dict ^ query->dict;
        mask_t mask = needs_adjust(diff, ranks->select_dict_masks)
                          ? adjust_for(diff, query->bits, ranks->select_dict_masks)
                          : query->bits;
        if ((entt->components & mask) != mask) {
            return cursor;
        }
        // we cannot save adjusted version to avoid frequents adjusts -> adjust works one way
        // we may save it, but fallback to original as we encouter missmatch (to confirm it)
    }
    return cursor;
}

bool bitecs_mask_set(int index, bitecs_SparseMask* mask, bool state)
{
    int group = index >> BITECS_GROUP_SHIFT;
    int bit = index & fill_up_to(BITECS_GROUP_SHIFT);
    if (unlikely(!(mask->dict & ((bitecs_dict_t)1 << group)))) {
        Ranks ranks;
        bitecs_get_ranks(mask->dict, &ranks);
        if (unlikely(ranks.popcount == BITECS_GROUPS_COUNT)) {
            return false;
        }
        bitecs_dict_t newDict = mask->dict | ((bitecs_dict_t)1 << group);
        bitecs_dict_t diff = newDict ^ mask->dict;
        mask->bits = adjust_for(diff, mask->bits, ranks.select_dict_masks);
        mask->dict = newDict;
    }
    int groupIndex = __builtin_popcount(mask->dict & fill_up_to(group));
    int shift = groupIndex * BITECS_GROUP_SIZE + bit;
    bitecs_mask_t selector = (bitecs_mask_t)1 << shift;
    bitecs_mask_t res;
    if (state) {
        res = mask->bits | selector;
    } else {
        res = mask->bits & ~selector;
    }
    mask->bits = res;
    return true;
}

bool bitecs_mask_get(int index, const bitecs_SparseMask* mask)
{
    int group = index >> BITECS_GROUP_SHIFT;
    int bit = index & fill_up_to(BITECS_GROUP_SHIFT);
    int groupIndex = __builtin_popcount(mask->dict & fill_up_to(group));
    int shift = groupIndex * BITECS_GROUP_SIZE + bit;
    bitecs_dict_t temp_dict = (bitecs_dict_t)1 << group;
    bool dict_match = mask->dict & temp_dict;
    bool bits_match = mask->bits & (mask_t)1 << shift;
    return dict_match && bits_match;
}

bool bitecs_mask_from_array(bitecs_SparseMask *maskOut, int *idxs, int idxs_count)
{
#ifndef NDEBUG
    {
        int _last = 0;
        for (int i = 0; i < idxs_count; ++i) {
            assert(idxs[i] > 0 && "bitecs_mask_from_array(): Invalid input");
            assert(_last <= idxs[i] && "bitecs_mask_from_array(): Unsorted input");
            _last = idxs[i];
        }
    }
#endif
    maskOut->dict = 0;
    maskOut->bits = 0;
    for (int i = 0; i < idxs_count; ++i) {
        int value = idxs[i];
        int group = value >> BITECS_GROUP_SHIFT;
        if (unlikely(group > BITECS_GROUP_SIZE)) {
            return false;
        }
        bitecs_dict_t newDict = maskOut->dict | (bitecs_dict_t)1 << group;
        int groupIndex = __builtin_popcount(newDict) - 1;
        if (unlikely(groupIndex == BITECS_GROUPS_COUNT)) {
            return false;
        }
        maskOut->dict = newDict;
        int bit = value & fill_up_to(BITECS_GROUP_SHIFT);
        int shift = groupIndex * BITECS_GROUP_SIZE + bit;
        maskOut->bits |= (bitecs_mask_t)1 << shift;
    }
    return true;
}

typedef struct component_list
{
    size_t* components_in_chunks;
    void** chunks; //array of memory_blocks of size: chunk_sizeof(this)
    size_t chunks_count;
    bitecs_ComponentMeta meta;
} component_list;

static size_t components_in_chunk(component_list* list) {
    return (size_t)1 << (list->meta.frequency + BITECS_FREQUENCY_ADJUST);
}

static size_t chunk_sizeof(component_list* list) {
    return components_in_chunk(list) * list->meta.typesize;
}

static component_list* components_new(bitecs_ComponentMeta meta) {
    component_list* res = malloc(sizeof(component_list));
    if (!res) return res;
    *res = (component_list){0};
    res->meta = meta;
    return res;
}

static void components_destroy(component_list* list)
{
    if (!list) return;
    for (size_t i = 0; i < list->chunks_count; ++i) {
        if (list->chunks[i]) {
            void* chunk = list->chunks[i];
            if (list->meta.deleter) {
                list->meta.deleter(chunk, (char*)chunk + chunk_sizeof(list));
            }
            free(chunk);
        }
    }
}


struct bitecs_registry
{
    Entity* entities;
    size_t entities_count;
    component_list* components[BITECS_MAX_COMPONENTS];
};

bool bitecs_registry_define_component(bitecs_registry* reg, bitecs_comp_id_t index, bitecs_ComponentMeta meta)
{
    assert(meta.frequency >= 1 && meta.frequency <= 9);
    if (reg->components[index]) return false;
    reg->components[index] = components_new(meta);
    return (bool)reg->components[index];
}

bitecs_registry* bitecs_registry_new(void)
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
        components_destroy(reg->components[i]);
    }
    *reg = (bitecs_registry){0};
    free(reg);
}

void bitecs_system_run(const bitecs_SparseMask *query, bitecs_RangeSystem system, void *udata)
{
    Ranks ranks;
    bitecs_get_ranks(query->dict, &ranks);
    // TODO:
}

void *bitecs_entt_add_component(bitecs_registry *reg, bitecs_EntityPtr entt, bitecs_comp_id_t id)
{

}

void *bitecs_entt_get_component(bitecs_registry *reg, bitecs_EntityPtr entt, bitecs_comp_id_t id)
{

}

void bitecs_entt_remove_component(bitecs_registry *reg, bitecs_EntityPtr entt, bitecs_comp_id_t id)
{

}

bool bitecs_entt_create_batch(
    bitecs_registry *reg, bitecs_index_t count,
    bitecs_dict_t dict, bitecs_mask_t mask,
    bitecs_RangeCreator creator, void* udata)
{

}

bool bitecs_entt_create(
    bitecs_registry *reg, bitecs_EntityPtr *outPtr,
    bitecs_dict_t dict, bitecs_mask_t mask,
    bitecs_SingleCreator creator, void *udata)
{

}

void _bitecs_sanity_test(bitecs_SparseMask *out)
{
    out->bits = (mask_t)1 << 95;
}
