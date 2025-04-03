// MIT License. See LICENSE file for details
// Copyright (c) 2025 Доронин Алексей
#include "bitecs/bitecs_core.h"
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)
#define popcnt32(x) __builtin_popcount(x)
#define dict_popcnt(x) __builtin_popcountl(x)
#define clz(x) __builtin_clz(x)
#define ctz(x) __builtin_ctz(x)

typedef bitecs_mask_t mask_t;
typedef bitecs_index_t index_t;
typedef bitecs_dict_t dict_t;
typedef bitecs_flags_t flags_t;
typedef bitecs_generation_t generation_t;
typedef bitecs_Ranks Ranks;
typedef bitecs_Entity Entity;
typedef bitecs_SparseMask SparseMask;

static inline dict_t fill_up_to(int bit) {
    return ((dict_t)(1) << bit) - (dict_t)1;
}

typedef struct component_list
{
    index_t* nalives;
    void** chunks;
    size_t nchunks;
    bitecs_ComponentMeta meta;
} component_list;

static int components_shift(component_list* list) {
    return list->meta.frequency + BITECS_FREQUENCY_ADJUST;
}

static size_t components_in_chunk(component_list* list) {
    return (size_t)1 << components_shift(list);
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
    for (size_t i = 0; i < list->nchunks; ++i) {
        void* chunk = list->chunks[i];
        if (chunk) {
            if (list->meta.deleter) {
                list->meta.deleter(chunk, components_in_chunk(list));
            }
            free(chunk);
        }
    }
    free(list->nalives);
    free(list->chunks);
    free(list);
}

struct bitecs_registry
{
    index_t entities_count;
    index_t entities_cap;
    Entity* entities;
    struct FreeList* freeList;
    index_t total_free;
    bitecs_generation_t generation;
    component_list* components[BITECS_MAX_COMPONENTS];
};

typedef struct FreeList
{
    index_t index;
    index_t count;
    struct FreeList* prev;
    struct FreeList* next;
} FreeList;


_BITECS_NODISCARD
static bool take_free(bitecs_registry* reg, index_t count, index_t* outIndex) {
    FreeList** _list = &reg->freeList;
    FreeList* list = *_list;
    if (!list) return false;
    while(list) {
        if (list->count > count) {
            *outIndex = list->index;
            list->count -= count;
            list->index += count;
            reg->total_free -= count;
            return true;
        } else if (list->count == count) {
            *outIndex = list->index;
            if (list->prev) list->prev->next = list->next;
            if (list->next) list->next->prev = list->prev;
            *_list = list->next;
            free(list);
            reg->total_free -= count;
            return true;
        } else {
            list = list->next;
        }
    }
    return false;
}

static void add_free(bitecs_registry* reg, index_t index, index_t count) {
    FreeList** _list = &reg->freeList;
    FreeList* old = *_list;
    while(old) {
        if (old->index + old->count == index) {
            old->count += count;
            reg->total_free += count;
            return;
        } else if (index + count == old->index) {
            old->index -= count;
            old->count += count;
            reg->total_free += count;
            return;
        }
        old = old->next;
    }
    old = *_list;
    FreeList* New = *_list = malloc(sizeof(FreeList));
    if (!New) return;
    New->count = count;
    New->prev = 0;
    New->next = old;
    New->index = index;
    if (old) {
        old->prev = New;
    }
    reg->total_free += count;
}

bool bitecs_component_define(bitecs_registry* reg, bitecs_comp_id_t id, bitecs_ComponentMeta meta)
{
    assert(meta.typesize >= 0);
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

static index_t select_up_to_chunk(component_list* list, index_t begin, index_t count, bitecs_ptrs_t outBegin)
{
    if (!list->meta.typesize) {
        *outBegin = 0;
        return count;
    }
    index_t chunk = begin >> components_shift(list);
    index_t i = begin & fill_up_to(components_shift(list));
    char* chunkBegin = list->chunks[chunk];
    assert(chunkBegin && "Entity mask says component exists. It does not");
    *outBegin = chunkBegin + i * list->meta.typesize;
    index_t chunkTail = components_in_chunk(list) - i;
    return count > chunkTail ? chunkTail : count;
}


static index_t query_match(index_t cursor, flags_t flags, const SparseMask* mask, const Ranks* ranks, const Entity* entts, index_t count);
static index_t query_miss(index_t cursor, flags_t flags, const SparseMask* mask, const Ranks* ranks, const Entity* entts, index_t count);

bool bitecs_system_step(bitecs_registry *reg, bitecs_QueryCtx* ctx, bitecs_ptrs_t ptrs, size_t * __restrict__ outCount)
{
    index_t begin = query_match(ctx->_cursor, ctx->flags, &ctx->mask, &ctx->ranks, reg->entities, reg->entities_count);
    if (unlikely(begin == reg->entities_count)) return false;
    index_t end = query_miss(begin, ctx->flags, &ctx->mask, &ctx->ranks, reg->entities, reg->entities_count);
    if (end <= begin) return false;
    index_t count = end - begin;
    index_t smallestRange = count;
    for (int i = 0; i < ctx->ncomps; ++i) {
        int comp = ctx->components[i];
        index_t selected = select_up_to_chunk(reg->components[comp], begin, count, ptrs++);
        smallestRange = selected < smallestRange ? selected : smallestRange;
    }
    ctx->outEntts = (bitecs_EntityProxy*)reg->entities + begin;
    ctx->outIndex = begin;
    ctx->_cursor = begin + smallestRange;
    *outCount = smallestRange;
    return true;
}

static Entity* deref(bitecs_registry* reg, bitecs_EntityPtr ptr)
{
    return ptr.index < reg->entities_count && reg->entities[ptr.index].generation == ptr.generation
        ? reg->entities + ptr.index
        : NULL;
}

static bool reserve_chunks(component_list* list, index_t index, index_t count)
{
    if (!list->meta.typesize) return true;
    index_t chunk = (index + count) >> components_shift(list);
    if (list->nchunks <= chunk) {
        index_t newSize = chunk + 1;
        void** newChunks = malloc(sizeof(void*) * newSize);
        if (!newChunks) return false;
        index_t* newAlives = malloc(sizeof(index_t) * newSize);
        if (!newAlives) {
            free(newChunks);
            return false;
        }
        if (list->chunks) {
            memcpy(newChunks, list->chunks, sizeof(void*) * list->nchunks);
            free(list->chunks);
        }
        memset(newChunks + list->nchunks, 0, sizeof(void*) * (newSize - list->nchunks));
        if (list->nalives) {
            memcpy(newAlives, list->nalives, sizeof(index_t) * list->nchunks);
            free(list->nalives);
        }
        memset(newAlives + list->nchunks, 0, sizeof(index_t) * (newSize - list->nchunks));
        list->chunks = newChunks;
        list->nalives = newAlives;
        list->nchunks = newSize;
    }
    return true;
}

static bool component_add_range(component_list* list, index_t index, index_t count, bitecs_ptrs_t begin, index_t* added)
{
    if (unlikely(!count)) return false;
    if (!list->meta.typesize) {
        *begin = NULL;
        *added = count;
        return true;
    }
    index_t chunk = index >> components_shift(list);
    index_t i = index & fill_up_to(components_shift(list));
    index_t diff = components_in_chunk(list) - i;
    diff = diff > count ? count : diff;
    if (unlikely(!list->chunks[chunk])) {
        list->chunks[chunk] = aligned_alloc(BITECS_COMPONENTS_CHUNK_ALIGN, chunk_sizeof(list));
        if (unlikely(!list->chunks[chunk])) {
            *begin = NULL;
            *added = 0;
            return false;
        }
    }
    list->nalives[chunk] += diff;
    *begin = (char*)list->chunks[chunk] + i * list->meta.typesize;
    *added = diff;
    return true;
}

void *bitecs_entt_add_component(bitecs_registry *reg, bitecs_EntityPtr ptr, bitecs_comp_id_t id)
{
    component_list* list = reg->components[id];
    if (!list) return NULL;
    Entity* e = deref(reg, ptr);
    if (!e) return NULL;
    mask_t wasDict = e->dict;
    mask_t wasComponents = e->components;
    { //try to add to bitmask
        if (!bitecs_mask_set((SparseMask*)e, id, true)) return NULL;
        if (wasComponents == e->components) return NULL;
    }
    void* begin = NULL;
    index_t added;
    if (reserve_chunks(list, ptr.index, 1)) {
        // no need to check here! begin wont get reassigned
        (void)component_add_range(list, ptr.index, 1, &begin, &added);
    }
    if (unlikely(!begin)) {
        e->dict = wasDict;
        e->components = wasComponents;
    }
    return begin;
}

static void* deref_comp(component_list* list, index_t index)
{
    index_t chunk = index >> components_shift(list);
    index_t i = index & fill_up_to(components_shift(list));
    return (char*)list->chunks[chunk] + list->meta.typesize * i;
}

void *bitecs_entt_get_component(bitecs_registry *reg, bitecs_EntityPtr ptr, bitecs_comp_id_t id)
{
    Entity* e = deref(reg, ptr);
    return e && bitecs_mask_get((SparseMask*)e, id) ? deref_comp(reg->components[id], ptr.index) : NULL;
}

bool bitecs_entt_remove_component(bitecs_registry *reg, bitecs_EntityPtr ptr, bitecs_comp_id_t id)
{
    Entity* e = deref(reg, ptr);
    if (unlikely(!e)) return false;
    if (!bitecs_mask_get((SparseMask*)e, id)) return false;
    component_list* list = reg->components[id];
    index_t chunk = ptr.index >> components_shift(list);
    index_t i = ptr.index & fill_up_to(components_shift(list));
    void* comp = (char*)list->chunks[chunk] + i * list->meta.typesize;
    if (list->meta.deleter) {
        list->meta.deleter(comp, 1);
    }
    if (list->nalives[chunk]-- == 1) {
        free(list->chunks[chunk]);
    }
    return bitecs_mask_set((SparseMask*)e, id, false);
}

static bool reserve_entts(bitecs_registry *reg, index_t count)
{
    if (count > reg->entities_cap) {
        index_t newCap = reg->entities_cap * 1.7;
        if (newCap < count) newCap = count;
        Entity* newEnts = aligned_alloc(BITECS_COMPONENTS_CHUNK_ALIGN, sizeof(Entity) * newCap);
        if (unlikely(!newEnts)) return false;
        if (reg->entities) {
            memcpy(newEnts, reg->entities, sizeof(Entity) * reg->entities_count);
            free(reg->entities);
        }
        reg->entities = newEnts;
        reg->entities_cap = newCap;
    }
    return true;
}

bool bitecs_entt_create(bitecs_registry* reg, bitecs_CreateCtx* cctx, bitecs_ptrs_t ptrs, size_t* __restrict__ batchSize)
{
    bitecs_QueryCtx* ctx = &cctx->query;
    if (unlikely(!cctx->count)) return false;
    if (!ctx->_cursor) {
        if (!take_free(reg, cctx->count, &ctx->_cursor)) {
            // todo: check if total free is too high: make query with part of wanted count
            if (unlikely(!reserve_entts(reg, reg->entities_count + cctx->count))) {
                return false;
            }
            ctx->_cursor = reg->entities_count;
        }
    }
    for (int i = 0; i < ctx->ncomps; ++i) {
        int comp = ctx->components[i];
        component_list* list = reg->components[comp];
        if (unlikely(!list)) return false;
        if (unlikely(!reserve_chunks(list, ctx->_cursor, cctx->count))) return false;
    }
    for (index_t i = ctx->_cursor; i < ctx->_cursor + cctx->count; ++i) {
        reg->entities[i].components = ctx->mask.bits;
        reg->entities[i].dict = ctx->mask.dict;
        reg->entities[i].generation = reg->generation;
        reg->entities[i].flags = ctx->flags;
    }
    index_t smallestRange = cctx->count;
    for (int i = 0; i < ctx->ncomps; ++i) {
        int comp = ctx->components[i];
        component_list* list = reg->components[comp];
        index_t added;
        bool ok = component_add_range(list, ctx->_cursor, cctx->count, ptrs + i, &added);
        if (unlikely(!ok)) return false; // already created leak here?
        smallestRange = added < smallestRange ? added : smallestRange;
    }
    ctx->outEntts = (bitecs_EntityProxy*)reg->entities + ctx->_cursor;
    ctx->outIndex = ctx->_cursor;
    *batchSize = smallestRange;
    cctx->count -= smallestRange;
    ctx->_cursor += smallestRange;
    reg->entities_count += smallestRange;
    return true;
}

static void do_destroy_batch(bitecs_registry *reg, bitecs_index_t ptr, index_t count)
{
    // todo: it may be better to expand the mask (instead of dumb iteration) to get active components
    // not sure. needs testing
    for (int i = 0; i < BITECS_MAX_COMPONENTS; ++i) {
        component_list* list = reg->components[i];
        if (!list) continue;
        index_t cursor = ptr;
        index_t cursor_count = count;
        do {
            void* begin;
            index_t part = select_up_to_chunk(list, cursor, cursor_count, &begin);
            if (list->meta.deleter) {
                list->meta.deleter(begin, part);
            }
            cursor += part;
            cursor_count -= part;
        } while(cursor_count);
    }
    add_free(reg, ptr, count);
}

void bitecs_entt_destroy_batch(bitecs_registry *reg, const bitecs_EntityPtr *ptrs, size_t nptrs)
{
    const index_t max = ~(bitecs_index_t)0;
    reg->generation++;
    bitecs_index_t begin = max;
    bitecs_index_t count = 0;
    for (size_t i = 0; i < nptrs; ++i) {
        const bitecs_EntityPtr* ptr = ptrs + i;
        Entity* e = deref(reg, *ptr);
        if (e) {
            e->generation = reg->generation;
            if (!count) {
                begin = ptr->index;
                continue;
            }
            count++;
            if (ptr->index != begin + count) {
                do_destroy_batch(reg, begin, count);
                count = 0;
                begin = ptr->index;
            }
        } else if (count) {
            do_destroy_batch(reg, begin, count);
            count = 0;
        }
    }
    if (count) {
        do_destroy_batch(reg, begin, count);
    }
}

void bitecs_entt_destroy(bitecs_registry *reg, bitecs_EntityPtr ptr)
{
    Entity* e = deref(reg, ptr);
    if (unlikely(!e)) return;
    reg->generation++;
    e->generation = reg->generation;
    do_destroy_batch(reg, ptr.index, 1);
}

// clone/merge

bool bitecs_registry_merge_other(bitecs_registry *reg, bitecs_registry *from)
{
    if (unlikely(!reserve_entts(reg, reg->entities_count + from->entities_count))) return false;
    // migrate all components to index + reg->count;
    // reg->entities_count += from->entities_count;
    assert(false && "Not implemented");
    return false;
}

bool bitecs_registry_clone_settings(bitecs_registry *reg, bitecs_registry *out)
{
    for (int i = 0; i < BITECS_MAX_COMPONENTS; ++i) {
        out->components[i] = reg->components[i];
    }
    return false;
}

bitecs_EntityProxy* bitecs_entt_deref(bitecs_registry *reg, bitecs_EntityPtr ptr)
{
    return (bitecs_EntityProxy*)deref(reg, ptr);
}

void bitecs_ranks_get(bitecs_Ranks* res, dict_t dict)
{
    assert(dict_popcnt(dict) <= BITECS_GROUPS_COUNT);
    *res = (bitecs_Ranks){0};
    int rank = 0;
    while(dict) {
        int trailing = ctz(dict);
        rank += trailing;
        int i = res->groups_count++;
        res->group_ranks[i] = rank;
        res->select_dict_masks[i] = fill_up_to(rank);
        dict >>= trailing + 1;
        rank++;
    }
}

static mask_t relocate_part(dict_t dictDiff, mask_t mask, int index, const dict_t* restrict rankMasks) {
    dict_t select_mask = rankMasks[index];
    int shift = dict_popcnt(dictDiff & select_mask) * BITECS_GROUP_SIZE;
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

static bool needs_adjust(dict_t diff, const bitecs_Ranks *ranks)
{
    return diff & ranks->select_dict_masks[ranks->groups_count - 1];
    // if any relocations (at least on biggest mask) -> dicts are incompatible
}

__attribute((noinline))
static index_t query_match(
    bitecs_index_t cursor, flags_t flags, const SparseMask* query,
    const Ranks* ranks, const bitecs_Entity* entts, index_t count)
{
    for (;cursor < count; ++cursor) {
        const Entity* entt = entts + cursor;
        dict_t edict = entt->dict;
        dict_t qdict = query->dict;
        if ((entt->flags & flags) != flags) continue;
        if ((edict & qdict) != qdict) continue;
        dict_t diff = edict ^ qdict;
        bool adjust = needs_adjust(diff, ranks);
        mask_t mask = query->bits;
        if (adjust) {
            mask = adjust_for(diff, query->bits, ranks->select_dict_masks);
        }
        mask_t ecomps = entt->components;
        if ((ecomps & mask) == mask) {
            return cursor;
        }
    }
    return cursor;
}

__attribute((noinline))
static index_t query_miss(
    bitecs_index_t cursor, flags_t flags, const SparseMask* query,
    const Ranks* ranks, const bitecs_Entity* entts, index_t count)
{
    for (;cursor < count; ++cursor) {
        const Entity* entt = entts + cursor;
        if ((entt->flags & flags) != flags) return cursor;
        dict_t diff = entt->dict ^ query->dict;
        bool adjust = needs_adjust(diff, ranks);
        mask_t mask = query->bits;
        if (unlikely(adjust)) {
            mask = adjust_for(diff, query->bits, ranks->select_dict_masks);
        }
        if ((entt->components & mask) != mask) {
            return cursor;
        }
    }
    return cursor;
}

// static index_t query_miss(
//     bitecs_index_t cursor, flags_t flags, const SparseMask* query,
//     const Ranks* ranks, const bitecs_Entity* entts, index_t count)
// {
//     SparseMask adjusted;
//     const SparseMask* current = query;
//     for (;cursor < count; ++cursor) {
//         const Entity* entt = entts + cursor;
//     again:
//         if ((entt->flags & flags) != flags) return cursor;
//         if ((entt->dict & current->dict) != current->dict) {
//             // missmatch on adjusted query is not definitive!
//             if (current != query) {
//                 current = query;
//                 goto again;
//             }
//             return cursor;
//         }
//         dict_t diff = entt->dict ^ current->dict;
//         bool adjust = needs_adjust(diff, ranks);
//         mask_t mask = current->bits;
//         if (unlikely(adjust)) {
//             mask = adjust_for(diff, current->bits, ranks->select_dict_masks);
//             adjusted.bits = mask;
//             adjusted.dict = entt->dict;
//             current = &adjusted;
//         }
//         if ((entt->components & mask) != mask) {
//             return cursor;
//         }
//     }
//     return cursor;
// }

bool bitecs_mask_set(bitecs_SparseMask* mask, int index, bool state)
{
    int group = index >> BITECS_GROUP_SHIFT;
    int bit = index & fill_up_to(BITECS_GROUP_SHIFT);
    if (unlikely(!(mask->dict & ((dict_t)1 << group)))) {
        Ranks ranks;
        bitecs_ranks_get(&ranks, mask->dict);
        if (unlikely(ranks.groups_count == BITECS_GROUPS_COUNT)) {
            return false;
        }
        dict_t newDict = mask->dict | ((dict_t)1 << group);
        dict_t diff = newDict ^ mask->dict;
        mask->bits = adjust_for(diff, mask->bits, ranks.select_dict_masks);
        mask->dict = newDict;
    }
    int groupIndex = dict_popcnt(mask->dict & fill_up_to(group));
    int shift = groupIndex * BITECS_GROUP_SIZE + bit;
    bitecs_mask_t selector = (bitecs_mask_t)1 << shift;
    bitecs_mask_t res;
    if (state) {
        res = mask->bits | selector;
    } else {
        res = mask->bits & ~selector;
        // todo: adjust dict if was last bit
    }
    mask->bits = res;
    return true;
}

bool bitecs_mask_get(const bitecs_SparseMask* mask, int index)
{
    int group = index >> BITECS_GROUP_SHIFT;
    int bit = index & fill_up_to(BITECS_GROUP_SHIFT);
    int groupIndex = dict_popcnt(mask->dict & fill_up_to(group));
    int shift = groupIndex * BITECS_GROUP_SIZE + bit;
    dict_t temp_dict = (dict_t)1 << group;
    bool dict_match = mask->dict & temp_dict;
    bool bits_match = mask->bits & (mask_t)1 << shift;
    return dict_match && bits_match;
}

bool bitecs_mask_from_array(bitecs_SparseMask *maskOut, const int *idxs, int idxs_count)
{
#ifndef NDEBUG
    {
        int _last = 0;
        for (int i = 0; i < idxs_count; ++i) {
            assert(idxs[i] >= 0 && "bitecs_mask_from_array(): Invalid input");
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
        if (unlikely(group > BITECS_BITS_IN_DICT)) {
            return false;
        }
        dict_t newDict = maskOut->dict | (dict_t)1 << group;
        int groupIndex = dict_popcnt(newDict) - 1;
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

void _bitecs_sanity_test(bitecs_SparseMask *out)
{
    out->bits = (mask_t)1 << 95;
}

static void expand_one(int bitOffset, uint32_t part, int offset, bitecs_BitsStorage *storage) {
    int bit = 0;
    int out = 0;
    while(part) {
        int trailing = ctz(part);
        bit += trailing;
        (*storage)[offset + out++] = bitOffset + bit;
        part >>= trailing + 1;
        bit++;
    }
}

int bitecs_mask_into_array(const bitecs_SparseMask *mask, const bitecs_Ranks *ranks, bitecs_BitsStorage *storage)
{
    const uint32_t* groups = (const uint32_t*)&mask->bits;
    int pcnt0 = popcnt32(groups[0]);
    int pcnt1 = popcnt32(groups[1]);
    int pcnt2 = popcnt32(groups[2]);
    int pcnt3 = popcnt32(groups[3]);
    expand_one(ranks->group_ranks[0] << BITECS_GROUP_SHIFT, groups[0], 0, storage);
    expand_one(ranks->group_ranks[1] << BITECS_GROUP_SHIFT, groups[1], pcnt0, storage);
    expand_one(ranks->group_ranks[2] << BITECS_GROUP_SHIFT, groups[2], pcnt0 + pcnt1, storage);
    expand_one(ranks->group_ranks[3] << BITECS_GROUP_SHIFT, groups[3], pcnt0 + pcnt1 + pcnt2, storage);
    return pcnt0 + pcnt1 + pcnt2 + pcnt3;
}
