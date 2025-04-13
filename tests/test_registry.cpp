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
    enum {bitecs_id = 1003};
};

static const auto counts = {1, 2, 10, 100, 200, 1000, 30000};

#define CHECK ASSERT_TRUE

static void System(Component1&) {

}

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
        reg.RunSystem(bitecs::Func<System>{});
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
            reg2.Entt(Component1{k}, Component2{});
            reg2.Entt(Component3{}, Component1{k});
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


// TODO: test removal + add + removal + add
