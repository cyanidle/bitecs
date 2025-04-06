#include "components.hpp"
#include "benchmark.hpp"
#include <gaia.h>


namespace bench2 {

using namespace gaia::ecs;

struct Gaia
{
    using Entity = Entity;

    World w;

    template<typename...Components>
    void RegisterComponents() {
        //pass
    }

    template<typename...Components>
    Entity CreateOneEntt(Components&&...c) {
        Entity e = w.add();
        auto builder = w.build(e);
        (builder.add<Components>(), ...);
        builder.commit();
        auto changer = w.acc_mut(e);
        (changer.set<Components>(e, std::move(c)), ...);
        return e;
    }
    template<typename...Components>
    void CreateManyEntts(size_t count, Components*...cs) {
        size_t idx = 0;
        w.add_n(count, [&](Entity e){
            auto builder = w.build(e);
            (builder.add<Components>(), ...);
            builder.commit();
            auto changer = w.acc_mut(e);
            (changer.set<Components>(e, std::move(cs[idx])), ...);
            idx++;
        });
    }
    template<typename...Components, typename System>
    void RunSystem(System&& system) {
        Query q = w.query();
        q.all<Components...>();
        q.each([&](Components&...cs){
            system(cs...);
        });
    }
    template<typename Component>
    void AddComponentTo(Entity entt, Component c) {
        w.add<Component>(entt);
        w.set<Component>(entt) = std::move(c);
    }
    template<typename Component>
    void RemoveComponentFrom(Entity entt) {
        w.del<Component>(entt);
    }
    template<typename Component>
    Component& GetComponent(Entity entt) {
        return w.set<Component>(entt);
    }
};
    
ECS_BENCHMARKS(Gaia);

}
    
