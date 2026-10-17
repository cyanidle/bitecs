// MIT License. See LICENSE file for details
// Copyright (c) 2025 Доронин Алексей

#pragma once

#include "bitecs_core.h"
#include <array>
#include <exception>
#include <utility>

namespace bitecs
{

using dict_t = bitecs_dict_t;
using mask_t = bitecs_mask_t;
using index_t = bitecs_index_t;
using flags_t = bitecs_flags_t;
using generation_t = bitecs_generation_t;
using comp_id_t = bitecs_comp_id_t;
using Entity = bitecs_Entity;
using EntityProxy = bitecs_EntityProxy;
using EntityPtr = bitecs_EntityPtr;
using CallbackContext = bitecs_CallbackContext;

template<typename...T>
struct TypeList {};

template<typename T>
struct Tag {};

template<typename T>
struct component_info {
    static constexpr unsigned id = T::bitecs_id;
};

template<typename T>
constexpr unsigned component_id = component_info<T>::id;

#define BITECS_COMPONENT(T, _id) \
template<> struct bitecs::component_info<T> { \
    static constexpr comp_id_t id = _id;\
}

}

namespace bitecs::impl
{

template<typename...Comps>
constexpr bool no_duplicates() {
    int res[] = {component_id<Comps>...};
    for (int i: res) {
        int hits = 0;
        for (int j: res) {
            if (i == j && ++hits > 1) return false;
        }
    }
    return true;
}

constexpr unsigned count_groups(const unsigned* comps, unsigned ncomps) {
    bool groups[BITECS_MAX_COMPONENTS] = {};
    for (unsigned i = 0; i < ncomps; ++i) {
        unsigned comp = comps[i];
        unsigned group = comp >> BITECS_GROUP_SHIFT;
        groups[group] = true;
    }
    unsigned count = 0;
    for (auto hit: groups) {
        if (hit) count++;
    }
    return count;
}

template<typename...Comps>
constexpr std::array<unsigned, sizeof...(Comps)> sorted_ids()
{
    std::array<unsigned, sizeof...(Comps)> res = {component_id<Comps>...};
    for (unsigned i = 0; i < res.size(); ++i) {
        for (unsigned j = 0; j < res.size(); ++j) {
            if (i == j) continue;
            if (res[i] < res[j]) {
                unsigned tmp = res[i];
                res[i] = res[j];
                res[j] = tmp;
            }
        }
    }
    return res;
}

template<typename T>
_BITECS_FLATTEN void deleter_for(void* begin, index_t count) {
    for (index_t i = 0; i < count; ++i) {
        static_cast<T*>(begin)[i].~T();
    }
}

template<typename T>
_BITECS_FLATTEN void relocater_for(void* begin, index_t count, void* out) {
    for (index_t i = 0; i < count; ++i) {
        T* dest = static_cast<T*>(out) + i;
        T* source = static_cast<T*>(begin) + i;
        new (dest) T{std::move(*source)};
        source->~T();
    }
}


template<typename T>
_BITECS_INLINE inline static T* select(void* batch, index_t i) {
    return static_cast<T*>(batch) + (std::is_empty_v<T> ? 0 : i);
}

template<typename, typename, typename...>
struct system_thunk;
template<typename Fn, typename...Comps, size_t...Is>
struct system_thunk<Fn, std::index_sequence<Is...>, Comps...>
{
    _BITECS_FLATTEN
    static void call(bitecs_udata udata, CallbackContext* ctx, bitecs_ptrs outs, index_t count)
    {
        Fn& f = *static_cast<Fn*>(udata);
        for (size_t i = 0; i < count; ++i) {
            if constexpr (std::is_invocable_v<Fn, EntityPtr, Comps&...>) {
                EntityPtr ptr;
                ptr.generation = ctx->entts[i].generation;
                ptr.index = ctx->index + i;
                f(ptr, *select<Comps>(outs[Is], i)...);
            } else if constexpr (std::is_invocable_v<Fn, EntityProxy*, Comps&...>) {
                f(ctx->entts + i, *select<Comps>(outs[Is], i)...);
            } else {
                f(*select<Comps>(outs[Is], i)...);
            }
        }
    }
};

template<typename, typename, typename...>
struct multi_creator;
template<typename Fn, typename...Comps, size_t...Is>
struct multi_creator<Fn, std::index_sequence<Is...>, Comps...> {

    _BITECS_FLATTEN
    static void call(bitecs_udata udata, CallbackContext* ctx, bitecs_ptrs outs, index_t count)
    {
        Fn& f = *static_cast<Fn*>(udata);
        for (index_t i = 0; i < count; ++i) {
            if constexpr (std::is_invocable_v<Fn, EntityPtr, Comps&...>) {
                EntityPtr ptr;
                ptr.generation = ctx->entts[i].generation;
                ptr.index = ctx->index + i;
                f(ptr, *(new(select<Comps>(outs[Is], i)) Comps{})...);
            } else if constexpr (std::is_invocable_v<Fn, EntityProxy*, Comps&...>) {
                f(ctx->entts + i, *(new(select<Comps>(outs[Is], i)) Comps{})...);
            } else {
                f(*(new(select<Comps>(outs[Is], i)) Comps{})...);
            }
        }
    }
};

template<typename T>
auto remove_cvref(Tag<T>) -> T;

template<typename T>
auto remove_cvref(Tag<const T>) -> T;

template<typename T>
auto remove_cvref(Tag<T&>) -> decltype(impl::remove_cvref(Tag<T>{}));

template<typename...Args>
auto clean_args(TypeList<Args...>) -> TypeList<decltype(impl::remove_cvref(Tag<Args>{}))...>;

template<typename...Args>
auto clean_args(TypeList<EntityPtr, Args...>) -> TypeList<decltype(impl::remove_cvref(Tag<Args>{}))...>;

template<typename...Args>
auto clean_args(TypeList<EntityProxy*, Args...>) -> TypeList<decltype(impl::remove_cvref(Tag<Args>{}))...>;

auto deduce_args(...) -> void;

template<typename Ret, typename...Args>
auto deduce_args(Ret(*)(Args...)) -> decltype(impl::clean_args(TypeList<Args...>{}));

template<typename Ret, typename C, typename...Args>
auto deduce_args(Ret(C::*)(Args...)) -> decltype(impl::clean_args(TypeList<Args...>{}));

template<typename Ret, typename C, typename...Args>
auto deduce_args(Ret(C::*)(Args...) const) -> decltype(impl::clean_args(TypeList<Args...>{}));

template<typename Fn, typename = decltype(&Fn::operator())>
auto deduce_args(Fn) -> decltype(impl::deduce_args(&Fn::operator()));

template<typename Fn>
using deduce_args_t = decltype(impl::deduce_args(std::declval<Fn>()));


} //bitecs::impl
