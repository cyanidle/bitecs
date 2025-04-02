#include "components.hpp"
#include "benchmark.hpp"
#include <entt/entity/registry.hpp>


namespace bench1 {


struct EnTT
{
    using Entity = entt::registry::entity_type;

    entt::registry registry;

    template<typename...Components>
    void RegisterComponents() {
        //pass
    }

    template<typename...Components>
    Entity CreateOneEntt(Components&&...c) {
        auto res = registry.create();
        (registry.emplace<Components>(res, std::move(c)), ...);
        return res;
    }
    template<typename...Components>
    void CreateManyEntts(size_t count, Components*...cs) {
        for (size_t i = 0; i < count; ++i) {
            auto res = registry.create();
            (registry.emplace<Components>(res, std::move(cs[i])), ...);
        }
    }
    template<typename...Components, typename System>
    void RunSystem(System&& system) {
        registry.view<Components...>().each(system);
    }
    template<typename Component>
    void AddComponentTo(Entity entt, Component c) {
        registry.emplace<Component>(entt, std::move(c));
    }
    template<typename Component>
    void RemoveComponentFrom(Entity entt) {
        registry.remove<Component>(entt);
    }
    template<typename Component>
    Component& GetComponent(Entity entt) {
        return registry.get<Component>(entt);
    }
};
    
ECS_BENCHMARKS(EnTT);

}
    