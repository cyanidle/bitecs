#include "components.hpp"
#include "benchmark.hpp"
#include <flecs.h>


namespace bench4 {


struct Flecs
{
    flecs::world world;

    using Entity = flecs::entity;

    template<typename...Components>
    void RegisterComponents() {
        //pass
    }

    template<typename...Components>
    Entity CreateOneEntt(Components&&...c) {
        auto e = world.entity();
        (e.emplace<Components>(std::move(c)), ...);
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
        world.query<Components...>().each([&](Components&...cs){
            system(cs...);
        });
    }
    template<typename Component>
    void AddComponentTo(Entity entt, Component c) {
        entt.emplace<Component>(std::move(c));
    }
    template<typename Component>
    void RemoveComponentFrom(Entity entt) {
        entt.remove<Component>();
    }
    template<typename Component>
    Component& GetComponent(Entity entt) {
        return *entt.get_ref<Component>().get();
    }
    void Clear() {
        world.delete_with<DataComponent>();
    }
};

ECS_BENCHMARKS(Flecs);

}
