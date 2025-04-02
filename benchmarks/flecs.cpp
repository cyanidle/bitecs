#include "components.hpp"
#include "benchmark.hpp"
#include <flecs.h>


namespace bench1 {


struct Flecs
{
    using Entity = flecs::entity_t;

    flecs::world registry;

    template<typename...Components>
    void RegisterComponents() {
        //pass
    }

    template<typename...Components>
    Entity CreateOneEntt(Components&&...c) {

    }
    template<typename...Components>
    void CreateManyEntts(size_t count, Components*...cs) {

    }
    template<typename...Components, typename System>
    void RunSystem(System&& system) {

    }
    template<typename Component>
    void AddComponentTo(Entity entt, Component c) {

    }
    template<typename Component>
    void RemoveComponentFrom(Entity entt) {

    }
    template<typename Component>
    Component& GetComponent(Entity entt) {

    }
};
    
//ECS_BENCHMARKS(Flecs);

}
    
