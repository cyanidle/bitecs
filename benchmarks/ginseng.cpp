#include "components.hpp"
#include "benchmark.hpp"
#include <ginseng/ginseng.hpp>

namespace bench5 {

struct Ginseng
{
    ginseng::database db;

    using Entity = ginseng::database::ent_id;

    template<typename...Components>
    void RegisterComponents() {
        //pass
    }

    template<typename...Components>
    Entity CreateOneEntt(Components&&...c) {
        auto e = db.create_entity();
        (db.add_component(e, std::move(c)), ...);
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
        db.visit([&](Components&...cs) {
            system(cs...);
        });
    }
    template<typename Component>
    void AddComponentTo(Entity entt, Component c) {
        db.add_component(entt, std::move(c));
    }
    template<typename Component>
    void RemoveComponentFrom(Entity entt) {
        db.remove_component<Component>(entt);
    }
    template<typename Component>
    Component& GetComponent(Entity entt) {
        return db.get_component<Component>(entt);
    }
};

ECS_BENCHMARKS(Ginseng);

}
