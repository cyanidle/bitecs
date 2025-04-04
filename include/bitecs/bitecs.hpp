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
class Components
{
    static_assert(impl::no_duplicates<Comps...>());
    static constexpr auto _data = impl::prepare_comps<Comps...>();
    static_assert(impl::count_groups(_data.data(), _data.size()) <= BITECS_GROUPS_COUNT);
    static constexpr int _original[] = {component_id<Comps>...};
public:
    bitecs_ComponentsList list;
    Components() {
        list.mask = SparseMask(_data.data(), _data.size()).mask;
        list.components = _original;
        list.ncomps = _data.size();
    }
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
    template<typename...Comps, typename Fn, typename = if_compatible_callback<Fn, Comps...>>
    void RunSystem(bitecs_flags_t flags, Fn& f) {
        static const Components<Comps...> comps;
        constexpr auto* system = impl::system_thunk<Fn, std::index_sequence_for<Comps...>, Comps...>::call;
        bitecs_system_run(reg, flags, &comps.list, system, reinterpret_cast<void*>(&f));
    }

    template<typename...Comps, typename Fn, typename = if_compatible_callback<Fn, Comps...>>
    void RunSystem(bitecs_flags_t flags, Fn&& f) {
        RunSystem<Comps...>(flags, f);
    }

    template<typename...Comps, typename Fn, typename = if_compatible_callback<Fn, Comps...>>
    void RunSystem(Fn&& f) {
        RunSystem<Comps...>(0, f);
    }
    template<typename...Comps>
    EntityPtr Entt(Comps...comps) {
        static const Components<Comps...> c;
        EntityPtr res;
        void* temp[] = {&res, &comps...};
        using seq = std::index_sequence_for<Comps...>;
        using creator = impl::single_creator<seq, Comps...>;
        if (!bitecs_entt_create(reg, 1, &c.list, creator::call, &temp)) {
            throw std::runtime_error("Could not create entts");
        }
        return res;
    }
    template<typename...Comps, typename Fn, typename = if_compatible_callback<Fn, Comps...>>
    void Entts(index_t count, Fn& populate) {
        static const Components<Comps...> c;
        using seq = std::index_sequence_for<Comps...>;
        using creator = impl::multi_creator<Fn, seq, Comps...>;
        if (!bitecs_entt_create(reg, count, &c.list, creator::call, reinterpret_cast<void*>(&populate))) {
            throw std::runtime_error("Could not create entts");
        }
    }
    template<typename...Comps, typename Fn, typename = if_compatible_callback<Fn, Comps...>>
    void Entts(index_t count, Fn&& populate) {
        Entts<Comps...>(count, populate);
    }
    template<typename...Comps>
    void EnttsFromArrays(index_t count, Comps*...init) {
        return Entts<Comps...>(count, [&](Comps&...out){
            ((out = std::move(*init++)), ...);
        });
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
