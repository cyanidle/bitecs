#include "components.hpp"
#include "benchmark.hpp"
#include <entityx/entityx.h>


namespace bench3 {


struct EntityX
{
    entityx::EntityX ex;

    using Entity = entityx::Entity;

    template<typename...Components>
    void RegisterComponents() {
        //pass
    }

    template<typename...Components>
    Entity CreateOneEntt(Components&&...c) {
        Entity e = ex.entities.create();
        (e.assign<Components>(std::move(c)), ...);
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
        ex.entities.each<Components...>([&](Entity, Components&...cs){
            system(cs...);
        });
    }
    template<typename Component>
    void AddComponentTo(Entity entt, Component c) {
        entt.assign<Component>(std::move(c));
    }
    template<typename Component>
    void RemoveComponentFrom(Entity entt) {
        entt.remove<Component>();
    }
    template<typename Component>
    Component& GetComponent(Entity entt) {
        return *entt.component<Component>();
    }
};

ECS_BENCHMARKS(EntityX);

}
