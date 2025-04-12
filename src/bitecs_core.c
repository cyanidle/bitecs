// MIT License. See LICENSE file for details
// Copyright (c) 2025 Доронин Алексей
#include "bitecs/bitecs_core.h"
#include <assert.h>
#include <limits.h>
#include <stdatomic.h>
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
typedef bitecs_generation_t generation_t;
typedef bitecs_Ranks Ranks;
typedef bitecs_Entity Entity;
typedef bitecs_SparseMask SparseMask;

const dict_t dead_entt = ~(dict_t)0;

static inline dict_t fill_up_to(int bit) {
    return ((dict_t)(1) << bit) - (dict_t)1;
}

typedef struct
{
    struct _chunk_header {
        index_t nalives;
    } header;
    char _pad[sizeof(Entity) - sizeof(struct _chunk_header)];
    char storage[];
} Chunk;

typedef struct component_list
{
    Chunk** chunks;
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
    return components_in_chunk(list) * list->meta.typesize + sizeof(Chunk);
}

static component_list* components_new(bitecs_ComponentMeta meta) {
    component_list* res = malloc(sizeof(component_list));
    if (!res) return res;
    *res = (component_list){0};
    res->meta = meta;
    return res;
}

static void components_destroy_trivial(component_list* list)
{
    if (!list) return;
    for (size_t i = 0; i < list->nchunks; ++i) {
        Chunk* chunk = list->chunks[i];
        if (chunk) {
            free(chunk);
        }
    }
    if (list->chunks) {
        free(list->chunks);
    }
    free(list);
}

static void components_destroy(bitecs_registry* reg, component_list* list)
{
    assert(false && "todo");
}

typedef struct FreeList
{
    index_t index;
    index_t count;
    struct FreeList* prev;
    struct FreeList* next;
} FreeList;


_BITECS_NODISCARD
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
            *outIndex = list->index;
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

_BITECS_NODISCARD
static bool add_free(FreeList** _list, index_t index, index_t count) {
    FreeList* old = *_list;
    while(old) {
        if (old->index + old->count == index) {
            old->count += count;
            return true;
        } else if (index + count == old->index) {
            old->index -= count;
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
    index_t total_free;
    bitecs_generation_t generation;
    component_list* components[BITECS_MAX_COMPONENTS];
    _Atomic(bool) chunks_cleanup_pending;
};

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
        component_list* list = reg->components[i];
        if (!list) continue;
        if (list->meta.deleter) {
            components_destroy(reg, list);
        } else {
            components_destroy_trivial(list);
        }
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

static index_t select_up_to_chunk(component_list* list, index_t begin, index_t count, bitecs_ptrs outBegin)
{
    if (unlikely(!list->meta.typesize)) {
        *outBegin = 0;
        return count;
    }
    index_t chunk = begin >> components_shift(list);
    index_t i = begin & fill_up_to(components_shift(list));
    char* chunkBegin = list->chunks[chunk]->storage;
    assert(chunkBegin && "Attempt to select from NULL chunk (mask of component lies?)");
    *outBegin = chunkBegin + i * list->meta.typesize;
    index_t chunkTail = components_in_chunk(list) - i;
    return count > chunkTail ? chunkTail : count;
}

typedef struct
{
    bitecs_SparseMask query;
    bitecs_Ranks ranks;
    bitecs_flags_t flags;
} QueryCtx;

typedef struct
{
    bitecs_ptrs ptrStorage; //should have space for void*[ncomps]
    const int* components;
    int ncomps;
    bitecs_Callback system;
    void* udata;
    QueryCtx queryContext;
    bitecs_index_t cursor;
    Entity* begin;
    bitecs_index_t count;
} StepCtx;

static bool bitecs_system_step(bitecs_registry* reg, StepCtx* ctx);

static bitecs_index_t bitecs_query_match(
        bitecs_index_t cursor, const QueryCtx* ctx,
        const bitecs_Entity* entts, bitecs_index_t count);

static bitecs_index_t bitecs_query_miss(
        bitecs_index_t cursor, const QueryCtx* ctx,
        const bitecs_Entity* entts, bitecs_index_t count);

static bool bitecs_system_step(bitecs_registry *reg, StepCtx* ctx)
{
    index_t offset = bitecs_query_match(ctx->cursor, &ctx->queryContext, ctx->begin, ctx->count);
    if (unlikely(offset == ctx->count)) return false;
    index_t end = bitecs_query_miss(offset, &ctx->queryContext, ctx->begin, ctx->count);
    bitecs_CallbackContext cb_ctx;
    while (end > offset) {
        index_t count = end - offset;
        index_t smallestRange = ~(index_t)0;
        bitecs_ptrs begins = ctx->ptrStorage;
        for (int i = 0; i < ctx->ncomps; ++i) {
            int comp = ctx->components[i];
            component_list* list = reg->components[comp];
            index_t selected = select_up_to_chunk(list, offset, count, begins++);
            smallestRange = selected < smallestRange ? selected : smallestRange;
        }
        cb_ctx.index = offset;
        cb_ctx.entts = (bitecs_EntityProxy*)ctx->begin + offset;
        ctx->system(ctx->udata, &cb_ctx, ctx->ptrStorage, smallestRange);
        offset += smallestRange;
    }
    ctx->cursor = end;
    return end != reg->entities_count;
}

void bitecs_system_run(bitecs_registry *reg, bitecs_SystemParams* params)
{
    if (unlikely(!params->comps->ncomps)) return;
    StepCtx ctx = {0};
    ctx.queryContext.flags = params->flags;
    ctx.queryContext.query = params->comps->mask;
    bitecs_ranks_get(&ctx.queryContext.ranks, ctx.queryContext.query.dict);
    ctx.ptrStorage = alloca(sizeof(void*) * params->comps->ncomps);
    ctx.system = params->system;
    ctx.udata = params->udata;
    ctx.components = params->comps->components;
    ctx.ncomps = params->comps->ncomps;
    ctx.begin = reg->entities;
    ctx.count = reg->entities_count;
    while (bitecs_system_step(reg, &ctx)) {
        // pass
    }
}

static Entity* deref(bitecs_registry* reg, bitecs_EntityPtr ptr)
{
    return ptr.index < reg->entities_count && reg->entities[ptr.index].generation == ptr.generation
        ? reg->entities + ptr.index
        : NULL;
}

static bool reserve_chunks(component_list* list, index_t index, index_t count)
{
    if (unlikely(!list->meta.typesize)) return true;
    index_t maxIndex = index + count;
    index_t chunk = maxIndex >> components_shift(list);
    if (list->nchunks <= chunk) {
        index_t newSize = chunk + 1;
        Chunk** newChunks = malloc(sizeof(Chunk*) * newSize);
        if (!newChunks) return false;
        if (list->chunks) {
            memcpy(newChunks, list->chunks, sizeof(Chunk*) * list->nchunks);
            free(list->chunks);
        }
        memset(newChunks + list->nchunks, 0, sizeof(Chunk*) * (newSize - list->nchunks));
        list->chunks = newChunks;
        list->nchunks = newSize;
    }
    return true;
}

static bool component_add_range(component_list* list, index_t index, index_t count, bitecs_ptrs begin, index_t* added)
{
    if (unlikely(!count)) return false;
    if (unlikely(!list->meta.typesize)) {
        *begin = NULL;
        *added = count;
        return true;
    }
    index_t chunk = index >> components_shift(list);
    index_t i = index & fill_up_to(components_shift(list));
    index_t diff = components_in_chunk(list) - i;
    diff = diff > count ? count : diff;
    Chunk* owner = list->chunks[chunk];
    if (unlikely(!owner)) {
        owner = malloc(chunk_sizeof(list));
        memset(owner, 0, sizeof(Chunk));
        if (unlikely(!owner)) {
            *begin = NULL;
            *added = 0;
            return false;
        }
    }
    list->chunks[chunk] = owner;
    owner->header.nalives += diff;
    *begin = owner->storage + i * list->meta.typesize;
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
    if (likely(reserve_chunks(list, ptr.index, 1))) {
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
    Chunk* owner = list->chunks[chunk];
    return owner->storage + list->meta.typesize * i;
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
    Chunk* owner = list->chunks[chunk];
    void* comp = owner->storage + i * list->meta.typesize;
    if (list->meta.deleter) {
        list->meta.deleter(comp, 1);
    }
    if (owner->header.nalives-- == 1) {
        atomic_store_explicit(&reg->chunks_cleanup_pending, true, memory_order_relaxed);
    }
    return bitecs_mask_set((SparseMask*)e, id, false);
}

static bool reserve_entts(bitecs_registry *reg, index_t count)
{
    if (count > reg->entities_cap) {
        index_t newCap = reg->entities_cap * 1.7;
        if (newCap < count) newCap = count;
        Entity* newEnts = malloc(sizeof(Entity) * newCap);
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

bool bitecs_entt_create(
    bitecs_registry *reg, index_t count,
    const bitecs_ComponentsList* components,
    bitecs_Callback creator, void* udata)
{
    if (unlikely(!count)) return true;
    index_t found;
    if (!take_free(&reg->freeList, count, &found)) {
        // prevent always allocating cases (high fragmentation of free spaces)
        // when total free slots is 3 times the wanted count, but could not fit -> split
        // wanted chunks by 2
        if (reg->total_free / count > 3) {
            index_t pivot = count / 2;
            return bitecs_entt_create(reg, pivot, components, creator, udata)
                   && bitecs_entt_create(reg, count - pivot, components, creator, udata);
        }
        if (unlikely(!reserve_entts(reg, reg->entities_count + count))) {
            return false;
        }
        found = reg->entities_count;
    } else {
        reg->total_free -= count;
    }
    for (int i = 0; i < components->ncomps; ++i) {
        int comp = components->components[i];
        component_list* list = reg->components[comp];
        if (unlikely(!list)) return false;
        if (unlikely(!reserve_chunks(list, found, count))) return false;
    }
    for (index_t i = found; i < found + count; ++i) {
        reg->entities[i].components = components->mask.bits;
        reg->entities[i].dict = components->mask.dict;
        reg->entities[i].generation = reg->generation;
        reg->entities[i].flags = 0;
    }
    index_t cursor = found;
    void** begins = alloca(sizeof(void*) * components->ncomps);
    bitecs_CallbackContext cb_ctx;
    while (count) {
        index_t smallestRange = count;
        for (int i = 0; i < components->ncomps; ++i) {
            int comp = components->components[i];
            component_list* list = reg->components[comp];
            index_t added;
            bool ok = component_add_range(list, cursor, count, begins + i, &added);
            if (unlikely(!ok)) return false; // already created leak here?
            smallestRange = added < smallestRange ? added : smallestRange;
        }
        cb_ctx.entts = (bitecs_EntityProxy*)reg->entities + cursor;
        cb_ctx.index = cursor;
        creator(udata, &cb_ctx, begins, smallestRange);
        count -= smallestRange;
        cursor += smallestRange;
        reg->entities_count += smallestRange;
    }
    return true;
}

static void do_destroy_batch(bitecs_registry *reg, bitecs_index_t ptr, index_t count)
{
    reg->chunks_cleanup_pending = true;
    dict_t wasDict = dead_entt;
    mask_t wasMask = 0;
    int ncomps = 0;
    index_t same_arch_begin = ptr;
    for (index_t i = ptr; i < ptr + count; ++i) {
        Entity* e = reg->entities + i;
        assert(e->dict != dead_entt);
        if (e->dict != wasDict || e->components != wasMask) {
            wasDict = e->dict;
            wasMask = e->components;
            bitecs_BitsStorage storage;
            Ranks ranks;
            bitecs_ranks_get(&ranks, e->dict);
            ncomps = bitecs_mask_into_array((const SparseMask*)e, &ranks, storage);
            for (int ci = 0; ci < ncomps; ++ci) {
                int comp = storage[ci];
                component_list* list = reg->components[comp];
                assert(list && "Attempt to delete entt with nonexistend component");
                index_t cursor = same_arch_begin;
                index_t cursor_count = i + 1 - same_arch_begin;
                do {
                    void* begin;
                    index_t selected = select_up_to_chunk(list, cursor, cursor_count, &begin);
                    if (list->meta.deleter) {
                        list->meta.deleter(begin, selected);
                    }
                    index_t chunk = cursor >> components_shift(list);
                    list->chunks[chunk]->header.nalives -= selected;
                    cursor += selected;
                    cursor_count -= selected;
                } while(cursor_count);
            }
            same_arch_begin = i;
        }
        e->generation = reg->generation;
        e->dict = dead_entt;
    }
    if (likely(add_free(&reg->freeList, ptr, count))) {
        reg->total_free += count;
    }
}

void bitecs_entt_destroy_batch(bitecs_registry *reg, const bitecs_EntityPtr *ptrs, size_t nptrs)
{
    reg->generation++;
    bitecs_index_t begin = 0;
    bitecs_index_t count = 0;
    for (size_t i = 0; i < nptrs; ++i) {
        const bitecs_EntityPtr* ptr = ptrs + i;
        Entity* e = deref(reg, *ptr);
        if (e) {
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
    do_destroy_batch(reg, ptr.index, 1);
}

// clone/merge

bool bitecs_registry_merge_other(bitecs_registry *reg, bitecs_registry *from)
{
    index_t was = reg->entities_count;
    index_t append = from->entities_count;
    if (unlikely(!reserve_entts(reg, was + append))) {
        return false;
    }
    memcpy(reg->entities + was, from->entities, sizeof(Entity) * append);
    for (int comp = 0; comp < BITECS_MAX_COMPONENTS; ++comp) {
        component_list* src = from->components[comp];
        if (src) {
            component_list* dest = reg->components[comp];
            bool ok = reserve_chunks(dest, was, append);
            if (unlikely(!ok)) {
                return false;
            }
        }
    }
    from->chunks_cleanup_pending = true;
    for (int comp = 0; comp < BITECS_MAX_COMPONENTS; ++comp) {
        component_list* src = from->components[comp];
        component_list* dest = reg->components[comp];
        assert((bool)src == (bool)dest && "Merging missmatching registry");
        if (!src) continue;
        index_t inputCursor = 0;
        index_t outputCursor = was;
        index_t count = append;
        while (count) {
            void* fromPtr;
            void* intoPtr;
            index_t selected = select_up_to_chunk(src, inputCursor, count, &fromPtr);
            index_t selectedDest;
            bool ok = component_add_range(dest, outputCursor, count, &intoPtr, &selectedDest);
            if (unlikely(!ok)) {
                return false; //oom
            }
            selected = selected < selectedDest ? selected : selectedDest;
            if (src->meta.typesize) {
                if (src->meta.relocater) {
                    src->meta.relocater(fromPtr, selected, intoPtr);
                } else {
                    memcpy(intoPtr, fromPtr, selected * src->meta.typesize);
                }
            }
            inputCursor += selected;
            outputCursor += selected;
            count -= selected;
        }
    }
    reg->entities_count += from->entities_count;
    from->entities_count = 0;
    return true;
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
    if (res->groups_count) {
        res->highest_select_mask = res->select_dict_masks[res->groups_count - 1];
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

static bool needs_adjust(dict_t diff, const Ranks* ranks) {
    return diff && diff & ranks->highest_select_mask;
}

static index_t bitecs_query_match(
    bitecs_index_t cursor, const QueryCtx* ctx,
    const bitecs_Entity* entts, index_t count)
{
    bitecs_flags_t flags = ctx->flags;
    const bitecs_SparseMask* query = &ctx->query;
    const bitecs_Ranks* ranks = &ctx->ranks;
    for (;cursor < count; ++cursor) {
        const Entity* entt = entts + cursor;
        dict_t edict = entt->dict;
        dict_t qdict = query->dict;
        if (unlikely(entt->dict == dead_entt)) continue;
        if ((entt->flags & flags) != flags) continue;
        if ((edict & qdict) != qdict) continue;
        dict_t diff = edict ^ qdict;
        mask_t mask = query->bits;
        if (unlikely(needs_adjust(diff, ranks))) {
            mask = adjust_for(diff, query->bits, ranks->select_dict_masks);
        }
        mask_t ecomps = entt->components;
        if ((ecomps & mask) == mask) {
            return cursor;
        }
    }
    return cursor;
}

static bitecs_index_t bitecs_query_miss(
    bitecs_index_t cursor, const QueryCtx* ctx,
    const bitecs_Entity* entts, bitecs_index_t count)
{
    bitecs_SparseMask adjusted;
    bitecs_flags_t flags = ctx->flags;
    const bitecs_Ranks* ranks = &ctx->ranks;
    const bitecs_SparseMask* orig_query = &ctx->query;
    const bitecs_SparseMask* query = orig_query;
    for (;cursor < count; ++cursor) {
        const Entity* entt = entts + cursor;
    again:
        if (unlikely(entt->dict == dead_entt)) return cursor;
        if ((entt->flags & flags) != flags) return cursor;
        if ((entt->dict & query->dict) != query->dict) {
            // missmatch on adjusted query is not definitive!
            if (query != orig_query) {
                query = orig_query;
                goto again;
            }
            return cursor;
        }
        dict_t diff = entt->dict ^ query->dict;
        mask_t mask = query->bits;
        if (unlikely(needs_adjust(diff, ranks))) {
            mask = adjust_for(diff, query->bits, ranks->select_dict_masks);
            adjusted.bits = mask;
            adjusted.dict = entt->dict;
            query = &adjusted;
        }
        if ((entt->components & mask) != mask) {
            return cursor;
        }
    }
    return cursor;
}

_BITECS_FLATTEN
bool bitecs_mask_set(bitecs_SparseMask* mask, int index, bool state)
{
    assert(index < BITECS_MAX_COMPONENTS);
    Ranks ranks;
    int group = index >> BITECS_GROUP_SHIFT;
    int bit = index & fill_up_to(BITECS_GROUP_SHIFT);
    if (unlikely(!(mask->dict & ((dict_t)1 << group)))) {
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
        bitecs_mask_t selectGroup = ((bitecs_mask_t)fill_up_to(BITECS_GROUP_SIZE)) << (groupIndex * BITECS_GROUP_SIZE);
        if (unlikely(!(res & selectGroup))) { //last bit in group
            mask->dict &= ~(1 << group); //unset group in dict
        }
    }
    mask->bits = res;
    return true;
}

_BITECS_FLATTEN
bool bitecs_mask_get(const bitecs_SparseMask* mask, int index)
{
    assert(index < BITECS_MAX_COMPONENTS);
    int group = index >> BITECS_GROUP_SHIFT;
    int bit = index & fill_up_to(BITECS_GROUP_SHIFT);
    int groupIndex = dict_popcnt(mask->dict & fill_up_to(group));
    int shift = groupIndex * BITECS_GROUP_SIZE + bit;
    dict_t temp_dict = (dict_t)1 << group;
    bool dict_match = mask->dict & temp_dict;
    bool bits_match = mask->bits & (mask_t)1 << shift;
    return dict_match && bits_match;
}

_BITECS_FLATTEN
bool bitecs_mask_from_array(bitecs_SparseMask *maskOut, const int *idxs, int idxs_count)
{
    maskOut->dict = 0;
    maskOut->bits = 0;
    for (int i = 0; i < idxs_count; ++i) {
        int idx = idxs[i];
        assert(idx < BITECS_MAX_COMPONENTS);
        int group = idx >> BITECS_GROUP_SHIFT;
        if (unlikely(group > BITECS_BITS_IN_DICT)) {
            return false;
        }
        dict_t newDict = maskOut->dict | (dict_t)1 << group;
        int groupIndex = dict_popcnt(newDict) - 1;
        if (unlikely(groupIndex == BITECS_GROUPS_COUNT)) {
            return false;
        }
        maskOut->dict = newDict;
        int bit = idx & fill_up_to(BITECS_GROUP_SHIFT);
        int shift = groupIndex * BITECS_GROUP_SIZE + bit;
        maskOut->bits |= (mask_t)1 << shift;
    }
    return true;
}

static void expand_one(int bitOffset, uint16_t part, int offset, int *storage) {
    int bit = 0;
    int out = 0;
    while(part) {
        int trailing = ctz(part);
        bit += trailing;
        storage[offset + out++] = bitOffset + bit;
        part >>= trailing + 1;
        bit++;
    }
}

_BITECS_FLATTEN
int bitecs_mask_into_array(const bitecs_SparseMask *mask, const bitecs_Ranks *ranks, int *storage)
{
    const uint16_t* groups = (const uint16_t*)&mask->bits;
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

typedef struct {
    int comp_id;
    size_t chunk;
} chunk_cleanup_data;

struct bitecs_cleanup_data {
    unsigned chunks_cap;
    unsigned nchunks;
    chunk_cleanup_data* chunks;
};

static bool add_to_cleanup(bitecs_cleanup_data* data, chunk_cleanup_data cd) {
    if (data->chunks_cap == data->nchunks) {
        unsigned newCap = data->chunks_cap ? data->chunks_cap * 2 : 2;
        chunk_cleanup_data* newData = malloc(sizeof(chunk_cleanup_data) * newCap);
        if (unlikely(!newData)) return false;
        if (data->chunks) {
            memcpy(newData, data->chunks, sizeof(chunk_cleanup_data) * data->nchunks);
        }
        free(data->chunks);
        data->chunks = newData;
        data->chunks_cap = newCap;
    }
    data->chunks[data->nchunks++] = cd;
    return true;
}

static void destroy_cleanup(bitecs_cleanup_data* data) {
    free(data->chunks);
    free(data);
}

bitecs_cleanup_data *bitecs_cleanup_prepare(bitecs_registry *reg)
{
    bitecs_cleanup_data* res = malloc(sizeof(bitecs_cleanup_data));
    if (!res) return NULL;
    *res = (bitecs_cleanup_data){0};
    if (reg->chunks_cleanup_pending) {
        for (int comp = 0; comp < BITECS_MAX_COMPONENTS; ++comp) {
            component_list* list = reg->components[comp];
            if (!list) continue;
            for (size_t ch = 0; ch < list->nchunks; ++ch) {
                Chunk* current = list->chunks[ch];
                if (current && !current->header.nalives) {
                    chunk_cleanup_data cd;
                    cd.comp_id = comp;
                    cd.chunk = ch;
                    if (!add_to_cleanup(res, cd)) goto err;
                }
            }
        }
    }
    return res;
err:
    destroy_cleanup(res);
    return NULL;
}

void bitecs_cleanup(bitecs_registry *reg, bitecs_cleanup_data *data)
{
    reg->chunks_cleanup_pending = false;
    for (size_t i = 0; i < data->nchunks; ++i) {
        chunk_cleanup_data* cdata = data->chunks + i;
        component_list* list = reg->components[cdata->comp_id];
        free(list->chunks[cdata->chunk]);
        list->chunks[cdata->chunk] = NULL;
    }
    destroy_cleanup(data);
}

void bitecs_system_run_many(bitecs_registry *registry, bitecs_threadpool *tpool, bitecs_MultiSystemParams *systems)
{

}

bitecs_threadpool *bitecs_threadpool_new(size_t nthreads)
{

}

void bitecs_threadpool_delete(bitecs_threadpool *tpool)
{

}
