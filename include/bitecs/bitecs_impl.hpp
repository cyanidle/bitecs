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

template<typename T>
struct component_info {
    static constexpr int id = T::bitecs_id;
};

template<typename T>
constexpr int component_id = component_info<T>::id;

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

template<typename T>
_BITECS_FLATTEN
void deleter_for(void* begin, index_t count) {
    for (index_t i = 0; i < count; ++i) {
        static_cast<T*>(begin)[i].~T();
    }
}

}
