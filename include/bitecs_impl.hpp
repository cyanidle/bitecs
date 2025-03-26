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
using generation_t = bitecs_generation_t;
using comp_id_t = bitecs_comp_id_t;
using Entity = bitecs_Entity;
using EntityPtr = bitecs_EntityPtr;

template<typename T>
struct component_info {
    static constexpr int id = T::bitecs_id;
};

template<typename T>
constexpr int component_id = component_info<T>::id;

#define BITECS_COMPONENT(T, _id) \
template<> struct bitecs::component_info<T> { \
    static constexpr int id = _id;\
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

constexpr int count_groups(const int* comps, int ncomps) {
    bool groups[BITECS_MAX_COMPONENTS] = {};
    for (int i = 0; i < ncomps; ++i) {
        int comp = comps[i];
        int group = comp >> BITECS_GROUP_SHIFT;
        groups[group] = true;
    }
    int count = 0;
    for (auto hit: groups) {
        if (hit) count++;
    }
    return count;
}

template<typename...Comps>
constexpr std::array<int, sizeof...(Comps)> prepare_comps()
{
    std::array<int, sizeof...(Comps)> res = {component_id<Comps>...};
    for (int i = 0; i < res.size(); ++i) {
        for (int j = 0; j < res.size(); ++j) {
            if (i == j) continue;
            if (res[i] < res[j]) {
                int tmp = res[i];
                res[i] = res[j];
                res[j] = tmp;
            }
        }
    }
    return res;
}


template<typename, typename, typename...>
struct system_thunk;
template<typename Fn, typename...Comps, size_t...Is>
struct system_thunk<Fn, std::index_sequence<Is...>, Comps...>
{
    static void call(void* udata, void** begins, index_t count)
    {
        Fn& f = *static_cast<Fn*>(udata);
        for (size_t i = 0; i < count; ++i) {
            f(*(static_cast<Comps*>(begins[Is]) + i)...);
        }
    }
};

template<typename T>
void deleter_for(void* begin, index_t count) {
    for (index_t i = 0; i < count; ++i) {
        static_cast<T*>(begin)[i].~T();
    }
}

template<typename, typename...>
struct single_creator;
template<typename...Comps, size_t...Is>
struct single_creator<std::index_sequence<Is...>, Comps...> {
    static void call(void* udata, void** outs, index_t)
    {
        void** sources = static_cast<void**>(udata);
        ((new (outs[Is]) Comps(std::move(*static_cast<Comps*>(sources[Is])))), ...);
    }
};

template<typename, typename, typename...>
struct multi_creator;
template<typename Fn, typename...Comps, size_t...Is>
struct multi_creator<Fn, std::index_sequence<Is...>, Comps...> {
    static void call(void* udata, void** outs, index_t count)
    {
        Fn& f = *static_cast<Fn*>(udata);
        for (index_t i = 0; i < count; ++i) {
            f((*new(static_cast<Comps*>(outs[Is]) + i) Comps{})...);
        }
    }
};

}
