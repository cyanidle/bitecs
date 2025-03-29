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
public:
    static constexpr const int* data() noexcept {
        return _data.data();
    }
    static constexpr int size() noexcept {
        return int(_data.size());
    }
};

template<typename...C>
struct SystemProxy
{
    bitecs_registry* reg;

    SystemProxy(bitecs_registry* reg) :
        reg(reg)
    {}

    template<typename Fn>
    void Run(Fn&& f) {
        constexpr Components<C...> comps;
        constexpr auto* system = impl::system_thunk<Fn, std::index_sequence_for<C...>, C...>::call;
        bitecs_system_run(reg, comps.data(), comps.size(), system, &f);
    }
};

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
    void DefineComponent(bitecs_Frequency freq) {
        bitecs_ComponentMeta meta {sizeof(T), freq, nullptr};
        if (!std::is_trivially_destructible_v<T>) {
            meta.deleter = impl::deleter_for<T>;
        }
        if (!bitecs_component_define(reg, component_id<T>, meta)) {
            throw std::runtime_error("Could not define");
        }
    }
    template<typename...Comps>
    SystemProxy<Comps...> System() {
        return SystemProxy<Comps...>{reg};
    }
    template<typename...Comps>
    EntityPtr Entt(Comps...comps) {
        constexpr Components<Comps...> c;
        EntityPtr res;
        void* temp[] = {&res, &comps...};
        using seq = std::index_sequence_for<Comps...>;
        using creator = impl::single_creator<seq, Comps...>;
        if (!bitecs_entt_create(reg, 1, c.data(), c.size(), creator::call, &temp)) {
            throw std::runtime_error("Could not create entts");
        }
        return res;
    }
    template<typename FirstComp, typename...OtherComps, typename Fn>
    void Entts(index_t count, Fn&& populate) {
        constexpr Components<FirstComp, OtherComps...> c;
        using seq = std::index_sequence_for<FirstComp, OtherComps...>;
        using creator = impl::multi_creator<Fn, seq, FirstComp, OtherComps...>;
        if (!bitecs_entt_create(reg, count, c.data(), c.size(), creator::call, &populate)) {
            throw std::runtime_error("Could not create entts");
        }
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

    void Destroy(const EntityPtr* entt, size_t count) {
        bitecs_entt_destroy_batch(reg, entt, count);
    }
};


}