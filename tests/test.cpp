#include "bitecs/bitecs.hpp"
#include <gtest/gtest.h>

using namespace bitecs;

struct Component1 {
    enum {bitecs_id = 101};
    int a;
    int b;
};

struct Component2 {
    enum {bitecs_id = 303};
    double a;
    double b;
};

struct Component3 {
    enum {bitecs_id = 1303};
};

static const auto counts = {1, 2, 10, 100, 200, 1000, 30000};

#define CHECK ASSERT_TRUE

TEST(Systems, Basic)
{
    Registry reg;
    reg.DefineComponent<Component1>(bitecs_freq3);
    reg.DefineComponent<Component2>(bitecs_freq5);
    reg.DefineComponent<Component3>(bitecs_freq9);
    int prev_counts = 1;
    for (int i: counts) {
        (void)reg.Entt(Component1{}, Component2{});
        (void)reg.Entt(Component3{});
        (void)reg.Entt(Component1{}, Component3{});
        (void)reg.Entt(Component1{}, Component2{});
        (void)reg.Entt(Component2{});
        int iter = 0;
        reg.RunSystem<Component1>([&](EntityPtr ptr, Component1& c1){
           iter++;
        });
        CHECK(iter == 3 * prev_counts);
        iter = 0;
        // First two c2`s not selected???
        reg.RunSystem<Component2>([&](EntityPtr ptr, Component2& c2){
           iter++;
        });
        CHECK(iter == 3 * prev_counts);
        iter = 0;
        reg.RunSystem<Component3>([&](EntityPtr ptr, Component3& c3){
            iter++;
        });
        CHECK(iter == 2 * prev_counts);
        iter = 0;
        reg.RunSystem<Component1, Component2>([&](EntityPtr ptr, Component1& c1, Component2& c2){
            iter++;
        });
        CHECK(iter == 2 * prev_counts);
        prev_counts++;
    }
}

TEST(Entts, MultiCreate) {
    Registry reg;
    reg.DefineComponent<Component1>(bitecs_freq3);
    reg.DefineComponent<Component2>(bitecs_freq5);
    int prev_counts = 0;
    for (int count: counts) {
        int iter = 0;
        reg.Entts<Component2, Component1>(count, [&](EntityPtr ptr, Component2& c2, Component1& c1){
            iter++;
            c1.a = iter;
            c1.b = iter * 2;
            c2.a = iter * 3;
            c2.b = iter * 4;
        });
        CHECK(iter == count);
        iter = 0;
        reg.RunSystem<Component1, Component2>([&](Component1& c1, Component2& c2){
            iter++;
        });
        CHECK(iter == count + prev_counts);
        iter = 0;
        reg.RunSystem([&](Component1& c1, Component2& c2){
            iter++;
        });
        CHECK(iter == count + prev_counts);
        iter = 0;
        reg.RunSystem<Component1>([&](Component1& c1){
            iter++;
        });
        CHECK(iter == count + prev_counts);
        iter = 0;
        reg.RunSystem<Component2>([&](Component2& c1){
            iter++;
        });
        CHECK(iter == count + prev_counts);
        prev_counts += count;
    }
}

TEST(Entts, FromArray) {
    Registry reg;
    reg.DefineComponent<Component1>(bitecs_freq3);
    reg.DefineComponent<Component2>(bitecs_freq5);
    int prev_counts = 0;
    for (int count: counts) {
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
        reg.RunSystem<Component1, Component2>([&](Component1& c1, Component2& c2){
            iter++;
        });
        CHECK(iter == count + prev_counts);
        prev_counts += count;
    }
}

TEST(Destroy, Basic)
{
    Registry reg;
    reg.DefineComponent<Component1>(bitecs_freq3);
    reg.DefineComponent<Component2>(bitecs_freq5);
    reg.Entt(Component2{});
    auto e = reg.Entt(Component1{}, Component2{});
    reg.Entt(Component2{});
    CHECK(reg.Deref(e));
    reg.Destroy(e);
    CHECK(reg.Deref(e) == nullptr);
    auto e2 = reg.Entt(Component1{}, Component2{});
    CHECK(reg.Deref(e) == nullptr);
    CHECK(reg.Deref(e2));
    CHECK(e.index == e2.index);
    CHECK(e.generation != e2.generation);
}

TEST(Cleanup, Basic)
{
    Registry reg;
    reg.DefineComponent<Component1>(bitecs_freq3);
    reg.DefineComponent<Component2>(bitecs_freq5);
    auto e = reg.Entt(Component1{}, Component2{});
    auto data = reg.PrepareCleanup();
    reg.Cleanup(data);
    reg.RemoveComponent<Component1>(e);
    auto data2 = reg.PrepareCleanup();
    reg.Cleanup(data2);
}

TEST(Merge, Basic)
{
    Registry reg;
    reg.DefineComponent<Component1>(bitecs_freq3);
    reg.DefineComponent<Component2>(bitecs_freq5);
    reg.DefineComponent<Component3>(bitecs_freq2);
    Registry reg2;
    reg2.DefineComponent<Component1>(bitecs_freq3);
    reg2.DefineComponent<Component2>(bitecs_freq5);
    reg2.DefineComponent<Component3>(bitecs_freq2);

    int total = 0;
    for (int i: counts) {
        for (int k = 0; k < i; ++k) {
            reg2.Entt(Component1{}, Component2{});
            reg2.Entt(Component3{}, Component1{});
        }
        int appended = i * 2;

        int count = 0;
        reg2.RunSystem([&](Component1& c1){
            count++;
        });

        CHECK(count == appended);

        reg.MergeFrom(reg2);
        total += appended;

        count = 0;
        reg2.RunSystem([&](Component1& c1){
            count++;
        });
        CHECK(count == 0);

        count = 0;
        reg.RunSystem([&](Component1& c1){
            count++;
        });
        CHECK(count == total);
    }
}

TEST(Mask, Ranks)
{
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

TEST(Mask, GetSet) {
    bitecs_SparseMask mask{};
    mask.bits = 0;
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

TEST(Mask, FromToArray){
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

    bitecs_BitsStorage back;
    bitecs_Ranks ranks;
    bitecs_ranks_get(&ranks, mask.dict);
    int count = bitecs_mask_into_array(&mask, &ranks, &back);
    CHECK(count == std::size(init));
    for (int i = 0; i < count; ++i) {
        CHECK(init[i] == back[i]);
    }
}


// TODO: test removal + add + removal + add
