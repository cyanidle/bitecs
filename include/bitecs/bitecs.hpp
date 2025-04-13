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
    explicit SparseMask(const int* bits, unsigned nbits) {
        if (!bitecs_mask_from_array(&mask, bits, nbits)) {
            throw std::runtime_error("Cannot construct sparse bitmask");
        }
    }
};

template<typename...Comps>
class Components
{
    static_assert(impl::no_duplicates<Comps...>());
    static constexpr int data[] = {component_id<Comps>...};
    static constexpr auto sorted = impl::sorted_ids<Comps...>();
    static constexpr int size = sizeof...(Comps);
    static_assert(impl::count_groups(data, size) <= BITECS_GROUPS_COUNT);
public:
    static inline bitecs_ComponentsList list = {
        SparseMask(sorted.data(), size).mask, data, size
    };
};

template<auto func, typename Sig>
struct FuncBase;

template<auto func, typename Ret, typename...Args>
struct FuncBase<func, Ret(*)(Args...)>
{
    _BITECS_INLINE Ret operator()(Args...args) {
        return func(std::forward<Args>(args)...);
    }
};

template<auto func>
struct Func : FuncBase<func, decltype(func)>
{};

#define BITFUNC(func) bitecs::Func<func>{}

template<typename Fn>
using if_not_function_ptr = std::enable_if_t<!std::is_function_v<Fn> && "Use BITFUNC(f)">;

class Registry
{
    bitecs_registry* reg;

    template<typename...Comps, typename Fn>
    void DoEntts(index_t count, Fn& populate, TypeList<Comps...> = {})
    {
        using seq = std::index_sequence_for<Comps...>;
        using creator = impl::multi_creator<Fn, seq, Comps...>;
        if (!bitecs_entt_create(reg, count, &Components<Comps...>::list, creator::call, &populate)) {
            throw std::runtime_error("Could not create entts");
        }
    }

    template<typename...Comps, typename Fn>
    void DoRunSystem(bitecs_flags_t flags, Fn& f, TypeList<Comps...> = {}) {
        using seq = std::index_sequence_for<Comps...>;
        constexpr auto* system = impl::system_thunk<Fn, seq, Comps...>::call;
        bitecs_SystemParams params = {};
        params.comps = &Components<Comps...>::list;
        params.system = system;
        params.flags = flags;
        params.udata = &f;
        bitecs_system_run(reg, &params);
    }

public:
    Registry(Registry const&) = delete;
    Registry(Registry&& o) : reg(std::exchange(o.reg, nullptr)) {}

    Registry() {
        reg = bitecs_registry_new();
    }
    ~Registry() {
        bitecs_registry_delete(reg);
    }

    EntityProxy* Deref(EntityPtr ptr) {
        return bitecs_entt_deref(reg, ptr);
    }

    template<typename T>
    bool DefineComponent(bitecs_Frequency freq = bitecs_Frequency::bitecs_freq5) {
        bitecs_ComponentMeta meta {std::is_empty_v<T> ? 0 : sizeof(T), freq, nullptr};
        if constexpr (!std::is_trivially_destructible_v<T>) {
            meta.deleter = impl::deleter_for<T>;
        }
        if constexpr (!std::is_trivially_move_constructible_v<T>) {
            meta.relocater = impl::relocater_for<T>;
        }
        return bitecs_component_define(reg, component_id<T>, meta);
    }

    template<typename...Comps, typename Fn, typename = if_not_function_ptr<Fn>>
    void RunSystem(bitecs_flags_t flags, Fn& f) {
        if constexpr (sizeof...(Comps) == 0) {
            using args = impl::deduce_args_t<Fn>;
            if constexpr (!std::is_void_v<args>) {
                DoRunSystem(flags, f, args{});
            } else {
                DoRunSystem<Comps...>(flags, f);
            }
        } else {
            DoRunSystem<Comps...>(flags, f);
        }
    }

    template<typename...Comps, typename Fn, typename = if_not_function_ptr<Fn>>
    void RunSystem(bitecs_flags_t flags, Fn&& f) {
        RunSystem<Comps...>(flags, f);
    }

    template<typename...Comps, typename Fn, typename = if_not_function_ptr<Fn>>
    void RunSystem(Fn&& f) {
        RunSystem<Comps...>(0, f);
    }

    template<typename...Comps, typename Fn, typename = if_not_function_ptr<Fn>>
    void Entts(index_t count, Fn& populate)
    {
        if constexpr (sizeof...(Comps) == 0) {
            using args = impl::deduce_args_t<Fn>;
            if constexpr (!std::is_void_v<args>) {
                DoEntts(count, populate, args{});
            } else {
                DoEntts<Comps...>(count, populate);
            }
        } else {
            DoEntts<Comps...>(count, populate);
        }
    }
    template<typename...Comps, typename Fn, typename = if_not_function_ptr<Fn>>
    void Entts(index_t count, Fn&& populate) {
        Entts<Comps...>(count, populate);
    }
    template<typename...Comps>
    void EnttsFromArrays(index_t count, Comps*...init) {
        Entts<Comps...>(count, [&](Comps&...out){
            ((out = std::move(*init++)), ...);
        });
    }

    template<typename...Comps>
    EntityPtr Entt(Comps...comps) {
        EntityPtr res;
        Entts<Comps...>(1, [&](EntityPtr e, Comps&...cs){
            res = e;
            ((cs = std::move(comps)), ...);
        });
        return res;
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

    bitecs_cleanup_data* PrepareCleanup() {
        return bitecs_cleanup_prepare(reg);
    }

    void Cleanup(bitecs_cleanup_data* data) {
        bitecs_cleanup(reg, data);
    }

    void MergeFrom(Registry& reg) {
        if (!bitecs_registry_merge_other(this->reg, reg.reg)) {
            throw std::runtime_error("Could not merge other registry");
        }
    }
};


}
