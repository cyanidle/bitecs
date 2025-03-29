#include <bitecs/bitecs.hpp>

struct DataComponent {
    inline static constexpr uint32_t DefaultSeed = 340383L;

    int thingy{0};
    double dingy{0.0};
    bool mingy{false};

    uint32_t seed{DefaultSeed};
    random_xoshiro128 rng;
    uint32_t numgy;

    DataComponent() : rng(seed), numgy(rng()) {}
};

struct EmptyComponent{};

};


enum class PlayerType { NPC, Monster, Hero };

struct PlayerComponent {
    ecs::benchmarks::base::random_xoshiro128 rng{};
    PlayerType type{PlayerType::NPC};
};

enum class StatusEffect { Spawn, Dead, Alive };
struct HealthComponent {
    int32_t hp{0};
    int32_t maxhp{0};
    StatusEffect status{StatusEffect::Spawn};
};

struct DamageComponent {
    int32_t atk{0};
    int32_t def{0};
};


struct PositionComponent {
    float x{0.0F};
    float y{0.0F};
};


struct SpriteComponent {
    char character{' '};
};

struct VelocityComponent {
    float x{1.0F};
    float y{1.0F};
};
