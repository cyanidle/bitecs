#pragma once

#if !defined(DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN) && !defined(DOCTEST_CONFIG_IMPLEMENT)
#define DOCTEST_CONFIG_DISABLE
#endif

#include <cstdint>
#include <cstddef>
#include <array>
#include <algorithm>
#include <stdexcept>
#include <utility>
#include <bit>
#include <cassert>
#include <climits>
#include <cstdint>
#include <initializer_list>
#include "bitecs_core.h"
#include "doctest/doctest.h"

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
int component_id = component_info<T>::id;

#define BITECS_COMPONENT(T, _id) \
template<> struct bitecs::component_info<T> { \
    static constexpr int id = _id;\
}

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

template<typename, typename, typename...>
struct _system_thunk;
template<typename Fn, typename...Comps, size_t...Is>
struct _system_thunk<Fn, std::index_sequence<Is...>, Comps...>
{
    static void* call(void* udata, void** begins, bitecs_index_t count) noexcept
    {
        try {
            Fn& f = *static_cast<Fn*>(udata);
            for (size_t i = 0; i < count; ++i) {
                f(*(static_cast<Comps*>(begins[Is]) + i)...);
            }
            return nullptr;
        } catch (...) {
            void* err;
            static_assert(sizeof(void*) == sizeof(std::exception_ptr));
            new (&err) std::exception_ptr(std::current_exception());
            return err;
        }
    }
};

template<typename T>
void _deleter_for(void* begin, index_t count) {
    for (index_t i = 0; i < count; ++i) {
        static_cast<T*>(begin)[i].~T();
    }
}

// todo: make whole class constexpr
template<typename...Cs>
struct Components
{
    std::array<int, sizeof...(Cs)> _data;
    Components() {
        _data = {component_id<Cs>...};
        std::sort(_data.begin(), _data.end());
        // todo: replace check with constexpr version.
        SparseMask(_data.data(), _data.size());
    }
    const int* data() const noexcept {
        return _data.data();
    }
    int size() const noexcept {
        return int(_data.size());
    }
};

template<typename...C>
struct SystemProxy
{
    bitecs_registry* reg;
    Components<C...> comps;

    SystemProxy(bitecs_registry* reg) :
        reg(reg)
    {}

    template<typename Fn>
    void Run(Fn&& f) {
        constexpr auto* system = _system_thunk<Fn, std::index_sequence_for<C...>, C...>::call;
        std::exception_ptr error;
        if (!bitecs_system_run(reg, comps.data(), comps.size(), system, &f, reinterpret_cast<void**>(&error)) && error) {
            std::rethrow_exception(error);
        }
    }
};

template<typename Comp>
bool _try_create_single(void* data, bitecs_comp_id_t id, void* out) {
    if (component_id<Comp> == id) {
        new (out) Comp(std::move(*static_cast<Comp*>(data)));
        return true;
    }
    return false;
}

template<typename, typename...>
struct _single_creator;
template<typename...Comps, size_t...Is>
struct _single_creator<std::index_sequence<Is...>, Comps...> {
    static bool call(void* udata, bitecs_comp_id_t id, void* begin, bitecs_index_t) noexcept
    {
        try {
            (void)(_try_create_single<Comps>(static_cast<void**>(udata)[Is], id, begin) || ...);
            return true;
        } catch (...) {
            return false; //return err?
        }
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
            meta.deleter = _deleter_for<T>;
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
    EntityPtr CreateEntt(Comps...comps) {
        Components<Comps...> c;
        EntityPtr res;
        void* temp[] = {&comps...};
        constexpr auto* creator = _single_creator<std::index_sequence_for<Comps...>, Comps...>::call;
        const int* cdata = c.data();
        int csize = c.size();
        if (!bitecs_entt_create(reg, 1, &res, cdata, csize, creator, &temp)) {
            throw std::runtime_error("Could not create component");
        }
        return res;
    }
};

TEST_CASE("Basic entt operations")
{
    Registry reg;
    struct Component1 {
        enum {
            bitecs_id = 1,
        };
        int a;
        int b;
    };
    struct Component2 {
        enum {
            bitecs_id = 3,
        };
        float a;
        float b;
    };
    reg.DefineComponent<Component1>(bitecs_freq3);
    reg.DefineComponent<Component2>(bitecs_freq5);
    reg.CreateEntt(Component1{1, 2}, Component2{2.5, 7.5});
    reg.System<Component1>().Run([&](Component1& c1){
        CHECK(c1.a == 1);
        CHECK(c1.b == 2);
    });
    reg.System<Component2>().Run([&](Component2& c2){
        CHECK(c2.a == 2.5);
        CHECK(c2.b == 7.5);
    });
    reg.System<Component1, Component2>().Run([&](Component1& c1, Component2& c2){
        CHECK(c1.a == 1);
        CHECK(c1.b == 2);
        CHECK(c2.a == 2.5);
        CHECK(c2.b == 7.5);
    });
}

TEST_CASE("Basic Mask Operations")
{
    SUBCASE("Get ranks") {
        bitecs_Ranks ranks;
        bitecs_ranks_get(&ranks, 0b1);
        CHECK(ranks.groups_count == 1);
        CHECK(ranks.group_ranks[0] == 0);
        CHECK(ranks.select_dict_masks[0] == 0);
        bitecs_ranks_get(&ranks, 0b101);
        CHECK(ranks.groups_count == 2);
        CHECK(ranks.group_ranks[0] == 0);
        CHECK(ranks.group_ranks[1] == 2);
        CHECK(ranks.select_dict_masks[0] == 0);
        CHECK(ranks.select_dict_masks[1] == 0b11);
        bitecs_ranks_get(&ranks, 0b110101);
        CHECK(ranks.groups_count == 4);
        CHECK(ranks.group_ranks[0] == 0);
        CHECK(ranks.group_ranks[1] == 2);
        CHECK(ranks.group_ranks[2] == 4);
        CHECK(ranks.group_ranks[3] == 5);
        CHECK(ranks.select_dict_masks[0] == 0);
        CHECK(ranks.select_dict_masks[1] == 0b11);
        CHECK(ranks.select_dict_masks[2] == 0b1111);
        CHECK(ranks.select_dict_masks[3] == 0b11111);
    }
    bitecs_SparseMask mask{};
    _bitecs_sanity_test(&mask);
    CHECK(mask.bits != 0);
    mask.bits = 0;
    SUBCASE("Set/Get") {
        CHECK(bitecs_mask_set(&mask, 1, true) == true);
        CHECK(bitecs_mask_get(&mask, 1) == true);
        CHECK(bitecs_mask_get(&mask, 512) == false);
        CHECK(bitecs_mask_set(&mask, 512, true) == true);
        CHECK(bitecs_mask_get(&mask, 1) == true);
        CHECK(bitecs_mask_get(&mask, 512) == true);
        CHECK(bitecs_mask_get(&mask, 513) == false);
        CHECK(bitecs_mask_get(&mask, 1023) == false);
        CHECK(bitecs_mask_set(&mask, 513, true) == true);
        CHECK(bitecs_mask_get(&mask, 1) == true);
        CHECK(bitecs_mask_get(&mask, 512) == true);
        CHECK(bitecs_mask_get(&mask, 513) == true);
        CHECK(bitecs_mask_get(&mask, 1023) == false);
        CHECK(bitecs_mask_set(&mask, 1023, true) == true);
        CHECK(bitecs_mask_get(&mask, 1) == true);
        CHECK(bitecs_mask_get(&mask, 1023) == true);
        CHECK(bitecs_mask_set(&mask, 32, true) == true);
        CHECK(bitecs_mask_get(&mask, 1) == true);
        CHECK(bitecs_mask_get(&mask, 1023) == true);
        CHECK(bitecs_mask_get(&mask, 32) == true);
        CHECK(bitecs_mask_set(&mask, 1023, false) == true);
        CHECK(bitecs_mask_get(&mask, 1) == true);
        CHECK(bitecs_mask_get(&mask, 1023) == false);
    }
    SUBCASE("Create From Array") {
        int init[] = {100, 101, 120, 200, 202, 204, 600};
        bitecs_SparseMask mask;
        CHECK(bitecs_mask_from_array(&mask, init, std::size(init)) == true);
        CHECK(bitecs_mask_get(&mask, 100) == true);
        CHECK(bitecs_mask_get(&mask, 101) == true);
        CHECK(bitecs_mask_get(&mask, 102) == false);
        CHECK(bitecs_mask_get(&mask, 120) == true);
        CHECK(bitecs_mask_get(&mask, 200) == true);
        CHECK(bitecs_mask_get(&mask, 202) == true);
        CHECK(bitecs_mask_get(&mask, 203) == false);
        CHECK(bitecs_mask_get(&mask, 204) == true);
        CHECK(bitecs_mask_get(&mask, 600) == true);

        SUBCASE("Convert back") {
            bitecs_BitsStorage back;
            bitecs_Ranks ranks;
            bitecs_ranks_get(&ranks, mask.dict);
            int count = bitecs_mask_into_array(&mask, &ranks, &back);
            CHECK(count == std::size(init));
            for (int i = 0; i < count; ++i) {
                CHECK(init[i] == back[i]);
            }
        }
    }
}

}

