#include "bitecs_private.h"

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
        if (list->chunks[i]) {
            void* chunk = list->chunks[i];
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
    bitecs_generation_t generation;
    component_list* components[BITECS_MAX_COMPONENTS];
};

bool bitecs_component_define(bitecs_registry* reg, bitecs_comp_id_t id, bitecs_ComponentMeta meta)
{
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

static index_t select_up_to_chunk(component_list* list, index_t begin, index_t count, void** outBegin)
{
    index_t chunk = begin >> components_shift(list);
    index_t i = begin & fill_up_to(components_shift(list));
    *outBegin = (char*)list->chunks[chunk] + i * list->meta.typesize;
    index_t chunkTail = components_in_chunk(list) - i;
    return count > chunkTail ? chunkTail : count;
}

bool bitecs_system_step(bitecs_registry *reg, bitecs_SystemStepCtx* ctx)
{
    index_t begin = bitecs_query_match(ctx->cursor, &ctx->query, &ctx->ranks, reg->entities, reg->entities_count - ctx->cursor);
    if (unlikely(begin == reg->entities_count)) return false;
    index_t end = bitecs_query_miss(begin, &ctx->query, &ctx->ranks, reg->entities, reg->entities_count - begin);
    while (end > begin) {
        index_t count = end - begin;
        index_t smallestRange = ~(index_t)0;
        void** begins = ctx->ptrStorage;
        for (int i = 0; i < ctx->ncomps; ++i) {
            int comp = ctx->components[i];
            index_t selected = select_up_to_chunk(reg->components[comp], begin, count, begins++);
            smallestRange = selected < smallestRange ? selected : smallestRange;
        }
        ctx->system(ctx->udata, ctx->ptrStorage, smallestRange);
        begin += smallestRange;
    }
    ctx->cursor = end;
    return end != reg->entities_count;
}

void bitecs_system_run(bitecs_registry *reg, const int *components, int ncomps, bitecs_RangeSystem system, void *udata)
{
    if (unlikely(!ncomps)) return;
    bitecs_SystemStepCtx ctx = {0};
    if (!bitecs_mask_from_array(&ctx.query, components, ncomps)) {
        return;
    }
    bitecs_ranks_get(&ctx.ranks, ctx.query.dict);
    ctx.ptrStorage = alloca(sizeof(void*) * ncomps);
    ctx.system = system;
    ctx.udata = udata;
    ctx.components = components;
    ctx.ncomps = ncomps;
    while (bitecs_system_step(reg, &ctx)) {
        // pass
    }
}

static Entity* deref(Entity* entts, index_t count, bitecs_EntityPtr ptr)
{
    return ptr.index < count && entts[ptr.index].generation == ptr.generation
        ? entts + ptr.index
        : NULL;
}

static bool reserve_chunks(component_list* list, index_t index, index_t count)
{
    index_t chunk = index >> components_shift(list);
    index_t totalChunks = count >> components_shift(list);
    index_t upto = chunk + totalChunks;
    if (list->nchunks <= upto) {
        index_t newSize = upto + 1;
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
        memset(newChunks, 0, sizeof(void*) * (newSize - list->nchunks));
        if (list->nalives) {
            memcpy(newAlives, list->nalives, sizeof(index_t) * list->nchunks);
            free(list->nalives);
        }
        memset(newAlives, 0, sizeof(index_t) * (newSize - list->nchunks));
        list->chunks = newChunks;
        list->nalives = newAlives;
        list->nchunks = newSize;
    }
    return true;
}


static bool component_add_range(component_list* list, index_t* index, index_t* count, void** begin, index_t* added)
{
    if (!*count) return false;
    index_t chunk = *index >> components_shift(list);
    index_t i = *index & fill_up_to(components_shift(list));
    index_t diff = components_in_chunk(list) - i;
    diff = diff > *count ? *count : diff;
    *count -= diff;
    *index += diff;
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
    return true; //does it need another iter
}

void *bitecs_entt_add_component(bitecs_registry *reg, bitecs_EntityPtr ptr, bitecs_comp_id_t id)
{
    component_list* list = reg->components[id];
    if (!list) return NULL;
    Entity* e = deref(reg->entities, reg->entities_count, ptr);
    if (!e) return NULL;
    mask_t wasDict = e->dict;
    mask_t wasComponents = e->components;
    { //try to add to bitmask
        if (!bitecs_mask_set((SparseMask*)e, id, true)) return NULL;
        if (wasComponents == e->components) return NULL;
    }
    index_t index = ptr.index;
    index_t count = 1;
    void* begin = NULL;
    index_t added;
    if (reserve_chunks(list, index, count)) {
        (void)component_add_range(list, &index, &count, &begin, &added);
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
    Entity* e = deref(reg->entities, reg->entities_count, ptr);
    return e && bitecs_mask_get((SparseMask*)e, id) ? deref_comp(reg->components[id], ptr.index) : NULL;
}

bool bitecs_entt_remove_component(bitecs_registry *reg, bitecs_EntityPtr ptr, bitecs_comp_id_t id)
{
    Entity* e = deref(reg->entities, reg->entities_count, ptr);
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
    (void)bitecs_mask_set((SparseMask*)e, id, false);
    return true;
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

bool bitecs_entt_create(
    bitecs_registry *reg, index_t count,
    bitecs_EntityPtr *first,
    const int* comps, int ncomps,
    bitecs_RangeCreator creator, void* udata)
{
    index_t found;
    if (!take_free(&reg->freeList, count, &found)) {
        if (unlikely(!reserve_entts(reg, reg->entities_count + count))) {
            return false;
        }
        found = reg->entities_count;
    }
    SparseMask mask;
    if (unlikely(!bitecs_mask_from_array(&mask, comps, ncomps))) return false;
    for (index_t i = found; i < found + count; ++i) {
        reg->entities[i].components = mask.bits;
        reg->entities[i].dict = mask.dict;
        reg->entities[i].generation = reg->generation;
    }
    for (int i = 0; i < ncomps; ++i) {
        int comp = comps[i];
        component_list* list = reg->components[comp];
        if (unlikely(!list)) return false;
        if (unlikely(!reserve_chunks(list, found, count))) return false;
        index_t cursor_index = found;
        index_t cursor_count = count;
        void* begin;
        index_t added;
        while(component_add_range(list, &cursor_index, &cursor_count, &begin, &added)) {
            creator(udata, comp, begin, added);
        }
    }
    reg->entities_count += count;
    if (first) {
        *first = (bitecs_EntityPtr){reg->generation, found};
    }
    return true;
}

static void do_destroy_batch(bitecs_registry *reg, bitecs_index_t ptr, index_t count)
{
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
    add_free(&reg->freeList, ptr, count);
}

void bitecs_entt_destroy_batch(bitecs_registry *reg, const bitecs_EntityPtr *ptrs, size_t nptrs)
{
    const index_t max = ~(bitecs_index_t)0;
    reg->generation++;
    bitecs_index_t begin = max;
    bitecs_index_t count = 0;
    for (size_t i = 0; i < nptrs; ++i) {
        const bitecs_EntityPtr* ptr = ptrs + i;
        Entity* e = deref(reg->entities, reg->entities_count, *ptr);
        if (e) {
            e->generation = reg->generation;
            if (!count) {
                begin = ptr->index;
                continue;
            }
            count++;
            if (ptr->index != ptr->index + count) {
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

bool bitecs_entt_destroy(bitecs_registry *reg, bitecs_EntityPtr ptr)
{
    Entity* e = deref(reg->entities, reg->entities_count, ptr);
    if (!e) {
        return false;
    }
    reg->generation++;
    e->generation = reg->generation;
    do_destroy_batch(reg, ptr.index, 1);
    return true;
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

bool bitecs_check_components(bitecs_registry *reg, const int *components, int ncomps)
{
    for (int i = 0; i < ncomps; ++i) {
        if (!reg->components[components[i]]) {
            return false;
        }
    }
    return true;
}
