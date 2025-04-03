#include "components.hpp"
#include <bitecs/bitecs.hpp>
#include "benchmark.hpp"

BITECS_COMPONENT(HealthComponent, 0);
BITECS_COMPONENT(PlayerComponent, 1);
BITECS_COMPONENT(DataComponent, 2);
BITECS_COMPONENT(EmptyComponent, 3);
BITECS_COMPONENT(DamageComponent, 4);
BITECS_COMPONENT(PositionComponent, 5);
BITECS_COMPONENT(SpriteComponent, 6);
BITECS_COMPONENT(VelocityComponent, 7);

namespace bench0 {

using namespace bitecs;

struct Bitecs
{
    using Entity = EntityPtr;

    Registry reg;

    template<typename...Components>
    void RegisterComponents() {
        (reg.DefineComponent<Components>() && ...);
    }

    template<typename...Components>
    Entity CreateOneEntt(Components&&...c) {
        return reg.Entt(std::move(c)...);
    }
    template<typename...Components>
    void CreateManyEntts(size_t count, Components*...cs) {
        reg.EnttsFromArrays(count, cs...);
    }
    template<typename...Components, typename System>
    void RunSystem(System&& system) {
        reg.RunSystem<Components...>(system);
    }
    template<typename Component>
    void AddComponentTo(Entity entt, Component c) {
        reg.AddComponent<Component>(entt, std::move(c));
    }
    template<typename Component>
    void RemoveComponentFrom(Entity entt) {
        reg.RemoveComponent<Component>(entt);
    }
    template<typename Component>
    Component& GetComponent(Entity entt) {
        return reg.GetComponent<Component>(entt);
    }
};

ECS_BENCHMARKS(Bitecs);

}
