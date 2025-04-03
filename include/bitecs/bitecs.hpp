// MIT License. See LICENSE file for details
// Copyright (c) 2025 Доронин Алексей

#pragma once

#include "bitecs_impl.hpp"
#include <cstddef>
#include <algorithm>
#include <stdexcept>
#include <utility>
#include <cassert>
#include <climits>
#include "bitecs_core.h"

namespace bitecs
{

struct SparseMask
{
    bitecs_SparseMask mask = {};
    SparseMask() = default;
    SparseMask(const int* bits, int nbits) {
        if (!bitecs_mask_from_array(&mask, bits, nbits)) {
            throw std::runtime_error("Cannot construct sparse bitmask");
        }
    }
};

template<typename...Comps>
constexpr int ComponentIdsFor[sizeof...(Comps)] = {component_id<Comps>...};

template<typename...Comps>
bitecs_QueryCtx MakeQueryFor() {
    static_assert(impl::no_duplicates<Comps...>());
    static constexpr auto _data = impl::prepare_comps<Comps...>();
    static_assert(impl::count_groups(_data.data(), _data.size()) <= BITECS_GROUPS_COUNT);
    bitecs_QueryCtx query{};
    query.mask = SparseMask(_data.data(), _data.size()).mask;
    query.components = ComponentIdsFor<Comps...>;
    query.ncomps = _data.size();
    (void)bitecs_ranks_get(&query.ranks, query.mask.dict);
    return query;
}

template<typename...Comps>
struct QueryFor {
    static inline const bitecs_QueryCtx value = MakeQueryFor<Comps...>();
};

template<typename Fn, typename...Comps>
using if_compatible_callback = std::enable_if_t<
    std::is_invocable_v<Fn, Comps&...>
    || std::is_invocable_v<Fn, EntityPtr, Comps&...>
    || std::is_invocable_v<Fn, EntityProxy*, Comps&...>
    >;

struct Registry
{
    bitecs_registry* reg;

    Registry(Registry const&) = delete;
    Registry(Registry&& o) : reg(std::exchange(o.reg, nullptr)) {}

    Registry() {
        reg = bitecs_registry_new();
    }
    ~Registry() {
        bitecs_registry_delete(reg);
    }

    template<typename T>
    bool DefineComponent(bitecs_Frequency freq = bitecs_Frequency::bitecs_freq5) {
        bitecs_ComponentMeta meta {impl::is_empty<T> ? 0 : sizeof(T), freq, nullptr};
        if (!std::is_trivially_destructible_v<T>) {
            meta.deleter = impl::deleter_for<T>;
        }
        return bitecs_component_define(reg, component_id<T>, meta);
    }
    template<typename...Comps, typename Fn, size_t...Is>
    void DoRunSystem(std::index_sequence<Is...>, bitecs_flags_t flags, Fn& f) {
        bitecs_QueryCtx query = QueryFor<Comps...>::value;
        query.flags = flags;
        void* ptrs[sizeof...(Comps)];
        size_t selected;
        while (bitecs_system_step(reg, &query, ptrs, &selected)) {
            for (size_t i = 0; i < selected; ++i) {
                if constexpr (std::is_invocable_v<Fn, EntityPtr, Comps&...>) {
                    EntityPtr ptr;
                    ptr.generation = query.outEntts[i].generation;
                    ptr.index = query.outIndex + i;
                    f(ptr, *(static_cast<Comps*>(ptrs[Is]) + (impl::is_empty<Comps> ? 0 : i))...);
                } else if constexpr (std::is_invocable_v<Fn, EntityProxy*, Comps&...>) {
                    f(query.outEntts + i, *(static_cast<Comps*>(ptrs[Is]) + (impl::is_empty<Comps> ? 0 : i))...);
                } else {
                    f(*(static_cast<Comps*>(ptrs[Is]) + (impl::is_empty<Comps> ? 0 : i))...);
                }
            }
        }
    }

    template<typename...Comps, typename Fn, typename = if_compatible_callback<Fn, Comps...>>
    void RunSystem(bitecs_flags_t flags, Fn& f) {
        DoRunSystem<Comps...>(std::index_sequence_for<Comps...>{}, flags, f);
    }

    template<typename...Comps, typename Fn, typename = if_compatible_callback<Fn, Comps...>>
    void RunSystem(bitecs_flags_t flags, Fn&& f) {
        DoRunSystem<Comps...>(std::index_sequence_for<Comps...>{}, flags, f);
    }

    template<typename...Comps, typename Fn, typename = if_compatible_callback<Fn, Comps...>>
    void RunSystem(Fn&& f) {
        DoRunSystem<Comps...>(std::index_sequence_for<Comps...>{}, 0, f);
    }

    template<typename...Comps, size_t...Is>
    EntityPtr DoEntt(std::index_sequence<Is...>, flags_t flags, Comps&...comps) {
        bitecs_CreateCtx ctx;
        ctx.query = QueryFor<Comps...>::value;
        ctx.query.flags = flags;
        ctx.count = 1;
        void* storage[sizeof...(Comps)];
        size_t created;
        if (bitecs_entt_create(reg, &ctx, storage, &created)) {
            ((new (storage[Is]) Comps(std::move(comps))), ...);
            return EntityPtr{ctx.query.outEntts->generation, ctx.query.outIndex};
        } else {
            throw std::runtime_error("Could not create entity");
        }
    }
    template<typename...Comps>
    EntityPtr EnttWithFlags(flags_t flags, Comps...comps) {
        return DoEntt(std::index_sequence_for<Comps...>{}, flags, comps...);
    }
    template<typename...Comps>
    EntityPtr Entt(Comps...comps) {
        return DoEntt(std::index_sequence_for<Comps...>{}, 0, comps...);
    }
    template<typename...Comps, size_t...Is, typename Fn>
    void DoEntts(std::index_sequence<Is...>, index_t count, flags_t flags, Fn& populate) {
        bitecs_CreateCtx ctx;
        ctx.query = QueryFor<Comps...>::value;
        ctx.query.flags = flags;
        ctx.count = count;
        void* ptrs[sizeof...(Comps)];
        size_t created;
        while(bitecs_entt_create(reg, &ctx, ptrs, &created)) {
            for (index_t i = 0; i < created; ++i) {
                if constexpr (std::is_invocable_v<Fn, EntityPtr, Comps&...>) {
                    EntityPtr ptr;
                    ptr.generation = ctx.query.outEntts[i].generation;
                    ptr.index = ctx.query.outIndex + i;
                    populate(ptr, (*new(static_cast<Comps*>(ptrs[Is]) + (impl::is_empty<Comps> ? 0 : i)) Comps{})...);
                } else if constexpr (std::is_invocable_v<Fn, EntityProxy*, Comps&...>) {
                    f(ctx.query.outEntts + i, (*new(static_cast<Comps*>(ptrs[Is]) + (impl::is_empty<Comps> ? 0 : i)) Comps{})...);
                } else {
                    populate((*new(static_cast<Comps*>(ptrs[Is]) + (impl::is_empty<Comps> ? 0 : i)) Comps{})...);
                }
            }
        }
    }
    template<typename...Comps, typename Fn, typename = if_compatible_callback<Fn, Comps...>>
    void Entts(index_t count, Fn& populate) {
        DoEntts<Comps...>(std::index_sequence_for<Comps...>{}, count, 0, populate);
    }
    template<typename...Comps, typename Fn, typename = if_compatible_callback<Fn, Comps...>>
    void Entts(index_t count, Fn&& populate) {
        DoEntts<Comps...>(std::index_sequence_for<Comps...>{}, count, 0, populate);
    }
    template<typename...Comps, typename Fn, typename = if_compatible_callback<Fn, Comps...>>
    void EnttsWithFlags(index_t count, flags_t flags, Fn&& populate) {
        DoEntts<Comps...>(std::index_sequence_for<Comps...>{}, count, flags, populate);
    }
    template<typename...Comps, typename Fn, typename = if_compatible_callback<Fn, Comps...>>
    void EnttsWithFlags(index_t count, flags_t flags, Fn& populate) {
        DoEntts<Comps...>(std::index_sequence_for<Comps...>{}, count, flags, populate);
    }
    template<typename...Comps>
    void EnttsFromArrays(index_t count, Comps*...init) {
        auto populate = [&](Comps&...out){
            ((out = std::move(*init++)), ...);
        };
        return DoEntts<Comps...>(std::index_sequence_for<Comps...>{}, count, 0, populate);
    }

    void Destroy(EntityPtr entt) {
        bitecs_entt_destroy(reg, entt);
    }

    void DestroyBatch(const EntityPtr* entt, size_t count) {
        bitecs_entt_destroy_batch(reg, entt, count);
    }

    template<typename Comp, typename...Args>
    Comp& AddComponent(EntityPtr entt, Args&&...args) {
        auto* c = static_cast<Comp*>(bitecs_entt_add_component(reg, entt, component_id<Comp>));
        if (!c) {
            throw std::runtime_error("Could not add component");
        }
        return *(new (c) Comp(std::forward<Args>(args)...));
    }

    template<typename Comp>
    Comp& GetComponent(EntityPtr entt) {
        auto* c = static_cast<Comp*>(bitecs_entt_get_component(reg, entt, component_id<Comp>));
        if (!c) {
            throw std::runtime_error("Could not get component");
        }
        return *c;
    }

    template<typename Comp>
    void RemoveComponent(EntityPtr entt) {
        if (!bitecs_entt_remove_component(reg, entt, component_id<Comp>)) {
            throw std::runtime_error("Could not remove component");
        }
    }
};


}
