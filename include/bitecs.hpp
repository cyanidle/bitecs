#pragma once

#include "bitecs_impl.hpp"
#include <memory>
#include <vector>
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
    void EnttsFromArrays(index_t count, const Comps*...init) {
        return Entts<Comps...>(count, [&](Comps&...out){
            ((out = Comps(*init++)), ...);
        });
    }
};

TEST_CASE("Basic entt operations")
{
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
    Registry reg;
    reg.DefineComponent<Component1>(bitecs_freq3);
    reg.DefineComponent<Component2>(bitecs_freq5);
    const auto counts = {1, 2, 10, 100, 200, 1000, 30000};
    // SUBCASE ("Simple") {
    //     const auto c1_0 = Component1{1, 2};
    //     const auto c1_1 = Component1{200, 300};
    //     const auto c2_0 = Component2{2.5, 7.5};
    //     const auto c2_1 = Component2{5.5, 10.5};
    //     reg.Entt(c1_0, c2_0);
    //     reg.Entt(c1_0, c2_0);
    //     reg.Entt(c1_1);
    //     reg.Entt(c2_1);
    //     int iter = 0;
    //     reg.System<Component1>().Run([&](Component1& c1){
    //         iter++;
    //     });
    //     CHECK(iter == 3);
    //     iter = 0;
    //     reg.System<Component2>().Run([&](Component2& c2){
    //         iter++;
    //     });
    //     CHECK(iter == 3);
    //     iter = 0;
    //     reg.System<Component1, Component2>().Run([&](Component1& c1, Component2& c2){
    //         iter++;
    //     });
    //     CHECK(iter == 2);
    // }
    SUBCASE ("Create multi") {
        Registry reg;
        reg.DefineComponent<Component1>(bitecs_freq3);
        reg.DefineComponent<Component2>(bitecs_freq5);
        int prev_counts = 0;
        for (int count: counts) {
            CAPTURE(count);
            int iter = 0;
            reg.Entts<Component1, Component2>(count, [&](EntityPtr ptr, Component1& c1, Component2& c2){
                iter++;
                c1.a = iter;
                c1.b = iter * 2;
                c2.a = iter * 3;
                c2.b = iter * 4;
            });
            CHECK(iter == count);
            iter = 0;
            reg.System<Component1, Component2>().Run([&](Component1& c1, Component2& c2){
                iter++;
            });
            CHECK(iter == count + prev_counts);
            iter = 0;
            reg.System<Component1>().Run([&](Component1& c1){
                iter++;
            });
            CHECK(iter == count + prev_counts);
            iter = 0;
            reg.System<Component2>().Run([&](Component2& c1){
                iter++;
            });
            CHECK(iter == count + prev_counts);
            prev_counts += count;
        }
    }
    SUBCASE ("Create from array") {
        int prev_counts = 0;
        for (int count: counts) {
            CAPTURE(count);
            std::vector<Component1> c1(count);
            std::vector<Component2> c2(count);
            for (int i = 0; i < count; ++i) {
                c1[i].a = i;
                c1[i].b = i * 2;
                c2[i].a = i * 3;
                c2[i].b = i * 4;
            }
            reg.EnttsFromArrays(count, c1.data(), c2.data());
            int iter = 0;
            reg.System<Component1, Component2>().Run([&](Component1& c1, Component2& c2){
                iter++;
            });
            CHECK(iter == count + prev_counts);
            prev_counts += count;
        }
    }
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

