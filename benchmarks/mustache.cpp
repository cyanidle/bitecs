#include "components.hpp"
#include "benchmark.hpp"
#include <mustache/ecs/world.hpp>
#include <mustache/ecs/job.hpp>


namespace bench6 {


struct Mustache
{
    mustache::World world;

    static constexpr mustache::FunctionSafety unsafe = mustache::FunctionSafety::kUnsafe;

    using Entity = mustache::Entity;

    template<typename...Components>
    void RegisterComponents() {
        //pass
    }

    template<typename...Components>
    Entity CreateOneEntt(Components&&...c) {
        auto e = world.entities().create<Components...>();
        ((*world.entities().getComponent<Components>(e) = std::move(c)), ...);
        return e;
    }
    template<typename...Components>
    void CreateManyEntts(size_t count, Components*...cs) {
        for (size_t i = 0; i < count; ++i) {
            CreateOneEntt(std::move(cs[i])...);
        }
    }
    template<typename...Components, typename System>
    void RunSystem(System&& system) {
        world.entities().forEach([&](Components&...cs) {
            system(cs...);
        }, mustache::JobRunMode::kCurrentThread);
    }
    template<typename Component>
    void AddComponentTo(Entity entt, Component c) {
        world.entities().assign<Component>(entt, std::move(c));
    }
    template<typename Component>
    void RemoveComponentFrom(Entity entt) {
        world.entities().removeComponent<Component>(entt);
    }
    template<typename Component>
    Component& GetComponent(Entity entt) {
        return *world.entities().getComponent<Component>(entt);
    }
};

// Segfaults on lots of allocations
ECS_BENCHMARKS_NO_CREATE(Mustache);

}
