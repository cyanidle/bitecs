#include "bitecs_private.h"

typedef struct component_list
{
    size_t* nalives;
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
                list->meta.deleter(chunk, (char*)chunk + chunk_sizeof(list));
            }
            free(chunk);
        }
    }
    free(list->nalives);
    free(list->chunks);
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

static index_t select_up_to_chunk(component_list* list, index_t begin, index_t count, void** outBegin, void** outEnd)
{
    index_t chunk = begin >> components_shift(list);
    index_t i = begin & fill_up_to(components_shift(list));
    *outBegin = (char*)list->chunks[chunk] + i * list->meta.typesize;
    index_t chunkTail = components_in_chunk(list) - i;
    count = count > chunkTail ? chunkTail : count;
    *outEnd = (char*)*outBegin + count * list->meta.typesize;
    return count;
}

bool bitecs_system_step(bitecs_registry *reg, bitecs_SystemStepCtx* ctx)
{
    index_t begin = bitecs_query_match(ctx->cursor, &ctx->query, &ctx->ranks, reg->entities, reg->entities_count - ctx->cursor);
    if (unlikely(begin == reg->entities_count)) return false;
    index_t end = bitecs_query_miss(begin, &ctx->query, &ctx->ranks, reg->entities, reg->entities_count - begin);
    while (end > begin) {
        index_t count = end - begin;
        index_t smallestRange = ~(index_t)0;
        void** rangesBegins = ctx->ptrStorage;
        void** rangesEnds = ctx->ptrStorage + ctx->ncomps;
        for (int i = 0; i < ctx->ncomps; ++i) {
            int comp = ctx->components[i];
            index_t selected = select_up_to_chunk(reg->components[comp], begin, count, rangesBegins++, rangesEnds++);
            smallestRange = selected < smallestRange ? selected : smallestRange;
        }
        ctx->system(ctx->udata, ctx->ptrStorage, ctx->ptrStorage + ctx->ncomps);
        begin += smallestRange;
    }
    ctx->cursor = end;
    return end != reg->entities_count;
}

void bitecs_system_run(bitecs_registry *reg, const int *components, int ncomps, bitecs_RangeSystem system, void *udata)
{
    if (unlikely(!ncomps)) return;
    bitecs_SystemStepCtx ctx = {0};
    bitecs_mask_from_array(&ctx.query, components, ncomps);
    bitecs_get_ranks(ctx.query.dict, &ctx.ranks);
    ctx.ptrStorage = alloca(sizeof(void*) * ncomps * 2);
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
        index_t newSize = upto;
        void** newChunks = malloc(sizeof(void*) * newSize);
        if (!newChunks) return false;
        size_t* newCounts = malloc(sizeof(size_t) * newSize);
        if (!newCounts) {
            free(newChunks);
            return false;
        }
        memcpy(newChunks, list->chunks, sizeof(void*) * list->nchunks);
        memcpy(newCounts, list->nalives, sizeof(size_t) * list->nchunks);
        memset(newChunks, 0, sizeof(void*) * (newSize - list->nchunks));
        memset(newCounts, 0, sizeof(size_t) * (newSize - list->nchunks));
        list->chunks = newChunks;
        list->nalives = newCounts;
        list->nchunks = newSize;
    }
    return true;
}


static bool component_add_range(component_list* list, index_t* index, index_t* count, void** begin, void** end)
{
    index_t chunk = *index >> components_shift(list);
    index_t i = *index & fill_up_to(components_shift(list));
    index_t diff = components_in_chunk(list) - i;
    *count -= *count > diff ? diff : *count;
    *index += *index > diff ? diff : *index;
    if (unlikely(!list->chunks[chunk])) {
        list->chunks[chunk] = aligned_alloc(BITECS_COMPONENTS_CHUNK_ALIGN, chunk_sizeof(list));
        if (unlikely(!list->chunks[chunk])) {
            *begin = *end = NULL;
            return false;
        }
    }
    list->nalives[chunk] += diff;
    *begin = (char*)list->chunks[chunk] + i;
    *end = (char*)list->chunks[chunk] + diff;
    return *count > 0; //does it need another iter
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
    void* end;
    if (reserve_chunks(list, index, count)) {
        (void)component_add_range(list, &index, &count, &begin, &end);
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
    //TODO
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

bool bitecs_entt_destroy_batch(bitecs_registry *reg, bitecs_index_t ptr, index_t count)
{

}

bool bitecs_entt_destroy(bitecs_registry *reg, bitecs_EntityPtr ptr)
{

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

index_t bitecs_entts_count(const bitecs_registry *reg)
{
    return reg->entities_count;
}
