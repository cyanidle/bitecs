#include "bitecs/bitecs.hpp"
#include "components.hpp"
#include <algorithm>
#include <type_traits>
#include <utility>
#include <benchmark/benchmark.h>

struct ECS_Interface
{
    using Entity = int;
    template<typename...Components>
    Entity CreateOneEntt(Components&&...);
    template<typename...Components>
    void CreateManyEntts(size_t count, Components*...);
    template<typename...Components, typename System>
    void RunSystem(System& system);
    template<typename Component>
    void AddComponentTo(Entity entt, Component c);
    template<typename Component>
    void RemoveComponentFrom(Entity entt);
    template<typename Component>
    Component& GetComponent(Entity entt);
};


template<typename ECS>
static typename ECS::Entity CreateProtag(ECS& ecs)
{
    ecs.template RegisterComponents<
        HealthComponent, PlayerComponent, DataComponent, SmallComponent,
        DamageComponent, PositionComponent, SpriteComponent, VelocityComponent
    >();
    return ecs.CreateOneEntt(
        HealthComponent{1000, 1000, StatusEffect::Spawn},
        PositionComponent{10, 10},
        VelocityComponent{0, 0},
        DamageComponent{0, 5},
        SpriteComponent{SpawnSprite},
        PlayerComponent{{999}, PlayerType::Hero}
    );
}

template<typename ECS>
static void CreateEntities(benchmark::State& state, ECS& ecs)
{
    size_t ndata = state.range(0);
    size_t nheroes = state.range(1);
    size_t nmonsters = state.range(2);
    size_t nactors = nheroes + nmonsters;

    ecs.template RegisterComponents<
        HealthComponent, PlayerComponent, DataComponent, SmallComponent,
        DamageComponent, PositionComponent, SpriteComponent, VelocityComponent
    >();

    std::vector<DataComponent> datas(ndata);
    std::vector<SmallComponent> empt(ndata);

    std::vector<DataComponent> alive_datas(nactors);
    std::vector<HealthComponent> healths(nactors);
    std::vector<PositionComponent> poses(nactors);
    std::vector<VelocityComponent> vels(nactors);
    std::vector<DamageComponent> dmgs(nactors);
    std::vector<SpriteComponent> sprites(nactors);
    std::vector<PlayerComponent> players(nactors);

    for (size_t i{0}; i < nactors; ++i) {
        alive_datas[i].rng.seed(i);
        healths[i].maxhp = i & 1 ? 100 : 200;
        healths[i].status = StatusEffect::Spawn;
        healths[i].hp = healths[i].maxhp;
        dmgs[i].def = i & 1 ? 1 : 2;
        sprites[i].character = SpawnSprite;
        if (i < nheroes) {
            players[i].type = PlayerType::Hero;
        } else {
            // half of monsters are NPC
            players[i].type = i & 1 ? PlayerType::Monster : PlayerType::NPC;
        }
    }

    const size_t total = ndata + nactors;
    if (!total) return;
    constexpr size_t interleaveGroupSize = 1024;
    size_t dataDone = 0;
    size_t actorDone = 0;
    for (size_t groupBegin = 0; groupBegin < total; groupBegin += interleaveGroupSize) {
        const size_t groupEnd = std::min(groupBegin + interleaveGroupSize, total);
        const size_t dataEnd = groupEnd * ndata / total;
        const size_t actorEnd = groupEnd - dataEnd;

        if (size_t count = dataEnd - dataDone) {
            ecs.CreateManyEntts(count, datas.data() + dataDone, empt.data() + dataDone);
            dataDone = dataEnd;
        }
        if (size_t count = actorEnd - actorDone) {
            ecs.CreateManyEntts(count,
                                alive_datas.data() + actorDone, healths.data() + actorDone,
                                poses.data() + actorDone, vels.data() + actorDone,
                                dmgs.data() + actorDone, sprites.data() + actorDone,
                                players.data() + actorDone);
            actorDone = actorEnd;
        }
    }
}

template<typename ECS>
static void RunSystems(ECS& ecs)
{
    ecs.template RunSystem<DataComponent>([&](auto& data){
        updateData(data, 1.f/60.f);
    });
    ecs.template RunSystem<HealthComponent>(BITFUNC(updateHealth));
    ecs.template RunSystem<HealthComponent, DamageComponent>(BITFUNC(updateDamage));
    ecs.template RunSystem<PositionComponent, VelocityComponent, DataComponent>(BITFUNC(updateComplex));
    ecs.template RunSystem<PositionComponent, VelocityComponent>([&](auto& pos, auto& dir){
        updatePosition(pos, dir, 1.f/60.f);
    });
    ecs.template RunSystem<SpriteComponent, PlayerComponent, HealthComponent>(BITFUNC(updateSprite));
    FrameBuffer buffer(rand() % 100, rand() & 200);
    ecs.template RunSystem<SpriteComponent, PositionComponent>([&](auto& sprite, auto& pos){
        renderSprite(buffer, pos, sprite);
    });
    // todo: maybe use data in buffer to prevent it being removed? is it removed?
}

template<typename ECS>
static void PlotArmor(ECS& ecs, typename ECS::Entity protagonist) {
    HealthComponent& health = ecs.template GetComponent<HealthComponent>(protagonist);
    benchmark::DoNotOptimize(health.hp = health.maxhp);
}

template<typename ECS>
static void BM_Systems(benchmark::State& state)
{
    ECS ecs;
    CreateEntities(state, ecs);
    auto protagonist = CreateProtag(ecs);
    for ([[maybe_unused]] auto _: state) {
        RunSystems(ecs);
        PlotArmor(ecs, protagonist);
    }
}

namespace benchmark_detail
{

template<typename ECS, typename = void>
struct has_clear : std::false_type {};

template<typename ECS>
struct has_clear<ECS, std::void_t<decltype(std::declval<ECS&>().Clear())>> : std::true_type {};

}

template<typename ECS>
static void BM_Create_Destroy(benchmark::State& state)
{
    if constexpr (benchmark_detail::has_clear<ECS>::value) {
        ECS ecs;
        for ([[maybe_unused]] auto _: state) {
            CreateEntities(state, ecs);
            ecs.Clear();
        }
    } else {
        for ([[maybe_unused]] auto _: state) {
            ECS ecs;
            CreateEntities(state, ecs);
        }
    }
}

template<typename ECS>
static void BM_Add_Get_Remove(benchmark::State& state)
{
    ECS ecs;
    auto protagonist = CreateProtag(ecs);
    ecs.template RemoveComponentFrom<PositionComponent>(protagonist);
    for ([[maybe_unused]] auto _: state) {
        ecs.template AddComponentTo<PositionComponent>(protagonist, PositionComponent{});
        benchmark::DoNotOptimize(ecs.template GetComponent<PositionComponent>(protagonist));
        ecs.template RemoveComponentFrom<PositionComponent>(protagonist);
    }
}

template<typename ECS>
static void BM_Modify_One(benchmark::State& state)
{
    ECS ecs;
    auto protagonist = CreateProtag(ecs);
    for ([[maybe_unused]] auto _: state) {
        PlotArmor(ecs, protagonist);
    }
}

static void Configurations(benchmark::internal::Benchmark* bench) {
    bench->ArgNames({"datas", "heroes", "monsters"});
    const auto matrix = {10, 2000, 30000, 500'000};
    for (long count: matrix) {
        bench->Args({count, count, count});
    }
}

#define ECS_BENCHMARKS_NO_CREATE(ECS) \
BENCHMARK(BM_Add_Get_Remove<ECS>); \
BENCHMARK(BM_Modify_One<ECS>); \
BENCHMARK(BM_Systems<ECS>)->Apply(Configurations)

#define ECS_BENCHMARKS(ECS) \
BENCHMARK(BM_Add_Get_Remove<ECS>); \
BENCHMARK(BM_Modify_One<ECS>); \
BENCHMARK(BM_Systems<ECS>)->Apply(Configurations); \
BENCHMARK(BM_Create_Destroy<ECS>)->Apply(Configurations)
