#include "components.hpp"
#include <benchmark/benchmark.h>

constexpr long MIN_ENTITIES_RANGE = 0L;
constexpr long MAX_ENTITIES_RANGE = 2'097'152L;
constexpr long SMALL_MAX_ENTITIES_RANGE = 32'768L;

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
static typename ECS::Entity CreateEntities(benchmark::State& state, ECS& ecs)
{
    size_t ndata = state.range(0);
    size_t nheroes = state.range(1);
    size_t nmonsters = state.range(2);

    ecs.template RegisterComponents<
        HealthComponent, PlayerComponent, DataComponent, EmptyComponent,
        DamageComponent, PositionComponent, SpriteComponent, VelocityComponent
    >();

    std::vector<DataComponent> datas(ndata);
    ecs.CreateManyEntts(ndata, datas.data());

    std::vector<HealthComponent> healths(nheroes + nmonsters);
    std::vector<VelocityComponent> vels(nheroes + nmonsters);
    std::vector<DamageComponent> dmgs(nheroes + nmonsters);
    std::vector<SpriteComponent> sprites(nheroes + nmonsters);
    std::vector<PlayerComponent> players(nheroes + nmonsters);

    for (size_t i{0}; i < nheroes + nmonsters; ++i) {
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

    ecs.CreateManyEntts(nheroes + nmonsters, healths.data(), vels.data(), dmgs.data(), sprites.data(), players.data());

    auto protagonist = ecs.CreateOneEntt(
        HealthComponent{1000, 1000, StatusEffect::Spawn},
        VelocityComponent{0, 0},
        DamageComponent{0, 5},
        SpriteComponent{SpawnSprite},
        PlayerComponent{{999}, PlayerType::Hero}
    );
    return protagonist;
}

template<typename ECS>
static void RunSystems(ECS& ecs)
{
    ecs.template RunSystem<DataComponent>([&](auto& data){
        updateData(data, 1.f/60.f);
    });
    ecs.template RunSystem<HealthComponent>(updateHealth);
    ecs.template RunSystem<HealthComponent, DamageComponent>(updateDamage);
    ecs.template RunSystem<PositionComponent, VelocityComponent, DataComponent>(updateGeneric);
    ecs.template RunSystem<PositionComponent, VelocityComponent>([&](auto& pos, auto& dir){
        updatePosition(pos, dir, 1.f/60.f);
    });
    ecs.template RunSystem<SpriteComponent, PlayerComponent, HealthComponent>(updateSprite);
}

template<typename ECS>
static void PlotArmor(ECS& ecs, typename ECS::Entity protagonist) {
    HealthComponent& health = ecs.template GetComponent<HealthComponent>(protagonist);
    health.hp = health.maxhp;
}

template<typename ECS>
static void BM_ECS(benchmark::State& state)
{
    ECS ecs;
    auto protagonist = CreateEntities(state, ecs);
    for ([[maybe_unused]] auto _: state) {
        RunSystems(ecs);
        PlotArmor(ecs, protagonist);
    }
}

template<typename ECS>
static void BM_ECS_Create_Destroy_Entities(benchmark::State& state)
{
    for ([[maybe_unused]] auto _: state) {
        ECS ecs;
        benchmark::DoNotOptimize(CreateEntities(state, ecs));
    }
}

template<typename ECS>
static void BM_ECS_Modify_One(benchmark::State& state)
{
    ECS ecs;
    auto protagonist = CreateEntities(state, ecs);
    for ([[maybe_unused]] auto _: state) {
        PlotArmor(ecs, protagonist);
    }
}

static void Configurations(benchmark::internal::Benchmark* bench) {
    const auto matrix = {10, 30, 2000, 12000, 200'000, 2'000'000};
    for (long datas: matrix) {
        for (long heroes: matrix) {
            for (long monsters: matrix) {
                bench->Args({datas, heroes, monsters});
            }
        }
    }
}

#define ECS_BENCHMARKS(ECS) \
BENCHMARK(BM_ECS<ECS>)->Apply(Configurations); \
BENCHMARK(BM_ECS_Create_Destroy_Entities<ECS>)->Apply(Configurations); \
BENCHMARK(BM_ECS_Modify_One<ECS>)->Apply(Configurations) \
