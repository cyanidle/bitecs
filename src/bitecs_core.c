#include "bitecs_core.h"
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

#define popcnt(x) __builtin_popcount(x)

typedef bitecs_mask_t mask_t;
typedef bitecs_index_t index_t;
typedef bitecs_dict_t dict_t;
typedef bitecs_generation_t generation_t;
typedef bitecs_Ranks Ranks;
typedef bitecs_Entity Entity;
typedef bitecs_SparseMask SparseMask;

static dict_t fill_up_to(int bit) {
    return ((dict_t)(1) << bit) - (dict_t)1;
}

void bitecs_get_ranks(dict_t dict, bitecs_Ranks* res)
{
    *res = (bitecs_Ranks){0};
    int out = 0;
    while(dict) {
        if (dict & 1) {
            int i = res->groups_count++;
            res->group_ranks[i] = out;
            res->select_dict_masks[i] = fill_up_to(out);
        }
        dict >>= 1;
        out++;
    }
}

static mask_t relocate_part(dict_t dictDiff, mask_t mask, int index, const dict_t* restrict rankMasks) {
    dict_t select_mask = rankMasks[index];
    int shift = popcnt(dictDiff & select_mask) * BITECS_GROUP_SIZE;
    int value_mask_offset = index * BITECS_GROUP_SIZE;
    mask_t value_mask = (mask_t)fill_up_to(BITECS_GROUP_SIZE);
    mask_t value_mask_shifted = value_mask << value_mask_offset;
    mask_t value = mask & value_mask_shifted;
    return value << shift;
}

static mask_t adjust_for(dict_t diff, mask_t qmask, const dict_t* restrict rankMasks) {
    mask_t r0 = relocate_part(diff, qmask, 0, rankMasks);
    mask_t r1 = relocate_part(diff, qmask, 1, rankMasks);
    mask_t r2 = relocate_part(diff, qmask, 2, rankMasks);
    mask_t r3 = relocate_part(diff, qmask, 3, rankMasks);
    return r0 | r1 | r2 | r3;
}

static bool needs_adjust(dict_t diff, const dict_t* restrict rankMasks)
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
        dict_t diff = entt->dict ^ query->dict;
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
        dict_t diff = entt->dict ^ query->dict;
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

bool bitecs_mask_set(bitecs_SparseMask* mask, int index, bool state)
{
    int group = index >> BITECS_GROUP_SHIFT;
    int bit = index & fill_up_to(BITECS_GROUP_SHIFT);
    if (unlikely(!(mask->dict & ((dict_t)1 << group)))) {
        Ranks ranks;
        bitecs_get_ranks(mask->dict, &ranks);
        if (unlikely(ranks.groups_count == BITECS_GROUPS_COUNT)) {
            return false;
        }
        dict_t newDict = mask->dict | ((dict_t)1 << group);
        dict_t diff = newDict ^ mask->dict;
        mask->bits = adjust_for(diff, mask->bits, ranks.select_dict_masks);
        mask->dict = newDict;
    }
    int groupIndex = popcnt(mask->dict & fill_up_to(group));
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
    int groupIndex = popcnt(mask->dict & fill_up_to(group));
    int shift = groupIndex * BITECS_GROUP_SIZE + bit;
    dict_t temp_dict = (dict_t)1 << group;
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
        dict_t newDict = maskOut->dict | (dict_t)1 << group;
        int groupIndex = popcnt(newDict) - 1;
        if (unlikely(groupIndex == BITECS_GROUPS_COUNT)) {
            return false;
        }
        maskOut->dict = newDict;
        int bit = value & fill_up_to(BITECS_GROUP_SHIFT);
        int shift = groupIndex * BITECS_GROUP_SIZE + bit;
        maskOut->bits |= (mask_t)1 << shift;
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

typedef struct FreeList
{
    index_t index;
    index_t count;
    struct FreeList* prev;
    struct FreeList* next;
} FreeList;


static bool take_free(FreeList** _list, index_t count, index_t* outIndex) {
    FreeList* list = *_list;
    if (!list) return false;
    while(list) {
        if (list->count > count) {
            *outIndex = list->index;
            list->count -= count;
            list->index += count;
            return true;
        } else if (list->count == count) {
            if (list->prev) list->prev->next = list->next;
            if (list->next) list->next->prev = list->prev;
            *_list = list->next;
            free(list);
            return true;
        } else {
            list = list->next;
        }
    }
    return false;
}

static bool add_free(FreeList** _list, index_t index, index_t count) {
    FreeList* old = *_list;
    while(old) {
        if (old->index + old->count == index) {
            old->count += count;
            return true;
        }
        old = old->next;
    }
    old = *_list;
    FreeList* New = *_list = malloc(sizeof(FreeList));
    if (!New) return false;
    New->count = count;
    New->prev = 0;
    New->next = old;
    New->index = index;
    if (old) {
        old->prev = New;
    }
    return true;
}

struct bitecs_registry
{
    Entity* entities;
    FreeList* freeList;
    index_t entities_count;
    index_t entities_cap;
    bitecs_generation_t generation;
    component_list* components[BITECS_MAX_COMPONENTS];
};

bool bitecs_component_define(bitecs_registry* reg, bitecs_comp_id_t id, bitecs_ComponentMeta meta)
{
    assert(meta.frequency >= 1 && meta.frequency <= 9);
    if (reg->components[id]) return false;
    reg->components[id] = components_new(meta);
    return (bool)reg->components[id];
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
    FreeList* list = reg->freeList;
    while (list) {
        FreeList* next = list->next;
        free(list);
        list = next;
    }
    *reg = (bitecs_registry){0};
    free(reg);
}

void bitecs_system_run(bitecs_registry *reg, const bitecs_SparseMask *query, bitecs_RangeSystem system, void *udata)
{
    Ranks ranks;
    bitecs_get_ranks(query->dict, &ranks);
    if (!ranks.groups_count) return;
    void** ptrStorage = alloca(sizeof(void*) * ranks.groups_count * 2);
    // TODO:
}

static Entity* deref(Entity* entts, index_t count, bitecs_EntityPtr ptr)
{
    return ptr.index < count && entts[ptr.index].generation == ptr.generation
        ? entts + ptr.index
        : NULL;
}

void *bitecs_entt_add_component(bitecs_registry *reg, bitecs_EntityPtr ptr, bitecs_comp_id_t id)
{
    Entity* e = deref(reg->entities, reg->entities_count, ptr);
    { //try to add to bitmask
        if (!e) return NULL;
        mask_t was = e->components;
        if (!bitecs_mask_set((SparseMask*)e, id, true)) return NULL;
        if (was == e->components) return NULL;
    }
    // todo: add single component to lists[id] (reuse batch addition op)
    // if fail revert mask + ret NULL
}

void *bitecs_entt_get_component(bitecs_registry *reg, bitecs_EntityPtr ptr, bitecs_comp_id_t id)
{

}

bool bitecs_entt_remove_component(bitecs_registry *reg, bitecs_EntityPtr ptr, bitecs_comp_id_t id)
{

}

//__builtin_ffs(int): Returns one plus the index of the least significant 1-bit of x, or if x is zero, returns zero.
//__builtin_clz(uint): leading zeroes
//__builtin_ctz(uint): trailing zeroes

bool bitecs_entt_create_batch(
    bitecs_registry *reg, index_t count,
    bitecs_EntityPtr *outBegin,
    const bitecs_SparseMask* query,
    bitecs_RangeCreator creator, void* udata)
{
    index_t found;
    if (!take_free(&reg->freeList, count, &found)) {
        // todo: resize entts vector
    }
    //todo: init with query
    *outBegin = (bitecs_EntityPtr){reg->generation, found};
    // TODO
}

typedef struct _single_entt_ctx
{
    bitecs_SingleCreator creator;
    void *udata;
} _single_entt_ctx;

static void _single_entt_helper(void* udata, bitecs_comp_id_t id, void* begin, void* end)
{
    _single_entt_ctx* ctx = udata;
    (void)end;
    ctx->creator(ctx->udata, id, begin);
}

bool bitecs_entt_create(
    bitecs_registry *reg, bitecs_EntityPtr *outPtr,
    const bitecs_SparseMask* query,
    bitecs_SingleCreator creator, void *udata)
{
    _single_entt_ctx ctx = {creator, udata};
    return bitecs_entt_create_batch(reg, 1, outPtr, query, _single_entt_helper, &ctx);
}

void _bitecs_sanity_test(bitecs_SparseMask *out)
{
    out->bits = (mask_t)1 << 95;
}

static void expand_one(int rank, uint32_t part, int offset, bitecs_BitsStorage *storage) {
    int bit = 0;
    int out = 0;
    while(part) {
        if (part & 1) {
            (*storage)[offset + out++] = rank * BITECS_GROUP_SIZE + bit;
        }
        part >>= 1;
        bit++;
    }
}

int bitecs_mask_into_array(const bitecs_SparseMask *mask, const bitecs_Ranks *ranks, bitecs_BitsStorage *storage)
{
    const uint32_t* groups = (const uint32_t*)&mask->bits;
    int pcnt0 = popcnt(groups[0]);
    int pcnt1 = popcnt(groups[1]);
    int pcnt2 = popcnt(groups[2]);
    int pcnt3 = popcnt(groups[3]);
    expand_one(ranks->group_ranks[0], groups[0], 0, storage);
    expand_one(ranks->group_ranks[1], groups[1], pcnt0, storage);
    expand_one(ranks->group_ranks[2], groups[2], pcnt0 + pcnt1, storage);
    expand_one(ranks->group_ranks[3], groups[3], pcnt0 + pcnt1 + pcnt2, storage);
    return pcnt0 + pcnt1 + pcnt2 + pcnt3;
}
