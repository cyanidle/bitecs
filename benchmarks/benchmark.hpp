#include "bitecs/bitecs.hpp"
#include "components.hpp"
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

    ecs.template RegisterComponents<
        HealthComponent, PlayerComponent, DataComponent, SmallComponent,
        DamageComponent, PositionComponent, SpriteComponent, VelocityComponent
    >();

    std::vector<DataComponent> datas(ndata);
    std::vector<SmallComponent> empt(ndata);
    ecs.CreateManyEntts(ndata, datas.data(), empt.data());

    std::vector<DataComponent> alive_datas(nheroes + nmonsters);
    std::vector<HealthComponent> healths(nheroes + nmonsters);
    std::vector<PositionComponent> poses(nheroes + nmonsters);
    std::vector<VelocityComponent> vels(nheroes + nmonsters);
    std::vector<DamageComponent> dmgs(nheroes + nmonsters);
    std::vector<SpriteComponent> sprites(nheroes + nmonsters);
    std::vector<PlayerComponent> players(nheroes + nmonsters);

    for (size_t i{0}; i < nheroes + nmonsters; ++i) {
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

    ecs.CreateManyEntts(nheroes + nmonsters,
                        alive_datas.data(), healths.data(), poses.data(),
                        vels.data(), dmgs.data(), sprites.data(), players.data());
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

template<typename ECS>
static void BM_Create_Destroy(benchmark::State& state)
{
    for ([[maybe_unused]] auto _: state) {
        ECS ecs;
        CreateEntities(state, ecs);
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
