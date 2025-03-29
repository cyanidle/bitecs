#include <bitecs/bitecs.hpp>

using TimeDelta = double;

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

static void updateDamage(HealthComponent& health,
                         DamageComponent& damage) {
    // Calculate damage
    const int totalDamage = damage.atk - damage.def;

    if (health.hp > 0 && totalDamage > 0) {
        health.hp = std::max(health.hp - totalDamage, 0);
    }
}

static void updateData(DataComponent& data, TimeDelta dt) {
    data.thingy = (data.thingy + 1) % 1'000'000;
    data.dingy += 0.0001 * gsl::narrow_cast<double>(dt);
    data.mingy = !data.mingy;
    data.numgy = data.rng();
}

static void updateHealth(HealthComponent& health) {
    if (health.hp <= 0 && health.status != StatusEffect::Dead) {
        health.hp = 0;
        health.status = StatusEffect::Dead;
    } else if (health.status == StatusEffect::Dead && health.hp == 0) {
        health.hp = health.maxhp;
        health.status = StatusEffect::Spawn;
    } else if (health.hp >= health.maxhp && health.status != StatusEffect::Alive) {
        health.hp = health.maxhp;
        health.status = StatusEffect::Alive;
    } else {
        health.status = StatusEffect::Alive;
    }
}

static void updateComponents(const PositionComponent& position, DirectionComponent& direction, DataComponent& data) {
    if ((data.thingy % 10) == 0) {
        if (position.x > position.y) {
            direction.x = gsl::narrow_cast<float>(data.rng.range(3, 19)) - 10.0F;
            direction.y = gsl::narrow_cast<float>(data.rng.range(0, 5));
        } else {
            direction.x = gsl::narrow_cast<float>(data.rng.range(0, 5));
            direction.y = gsl::narrow_cast<float>(data.rng.range(3, 19)) - 10.0F;
        }
    }
}

static void updatePosition(PositionComponent& position,
                           VelocityComponent& direction, TimeDelta dt) {
    position.x += direction.x * static_cast<float>(dt);
    position.y += direction.y * static_cast<float>(dt);
}

class FrameBuffer {
public:
    FrameBuffer(uint32_t w, uint32_t h)
        : m_width{w}, m_height{h}, m_buffer(gsl::narrow_cast<size_t>(m_width) * gsl::narrow_cast<size_t>(m_height)) {}

    [[nodiscard]] auto width() const noexcept { return m_width; }
    [[nodiscard]] auto height() const noexcept { return m_height; }

    void draw(int x, int y, char c) {
        if (y >= 0 && std::cmp_less(y, m_height)) {
            if (x >= 0 && std::cmp_less(x, m_width)) {
                m_buffer[gsl::narrow_cast<size_t>(x) + gsl::narrow_cast<size_t>(y) * m_width] = c;
            }
        }
    }

private:
    uint32_t m_width;
    uint32_t m_height;
    std::vector<char> m_buffer;
};

void renderSprite(PositionComponent& position,
                  SpriteComponent& spr) {
    //todo
}

static void updateSprite(ecs::benchmarks::base::components::SpriteComponent& spr,
                         const ecs::benchmarks::base::components::PlayerComponent& player,
                         const ecs::benchmarks::base::components::HealthComponent& health) {
    spr.character = [&]() {
        switch (health.status) {
        case components::StatusEffect::Alive:
            switch (player.type) {
            case components::PlayerType::Hero:
                return PlayerSprite;
            case components::PlayerType::Monster:
                return MonsterSprite;
            case components::PlayerType::NPC:
                return NPCSprite;
            }
            break;
        case components::StatusEffect::Dead:
            return GraveSprite;
        case components::StatusEffect::Spawn:
            return SpawnSprite;
        }
        return NoneSprite;
    }();
}
