#pragma once
#include <fastPRNG.h>
#include <utility>
#include <vector>
#include "gsl/gsl-lite.hpp"

using TimeDelta = double;

struct DataComponent {
    inline static constexpr uint32_t DefaultSeed = 340383L;

    int thingy{0};
    double dingy{0.0};
    bool mingy{false};

    uint32_t seed{DefaultSeed};
    fastPRNG::fastXS32 rng;
    uint32_t numgy;

    DataComponent() : rng(seed), numgy(rng.xorShift()) {}
};

struct SmallComponent {int dummy;};

inline constexpr char PlayerSprite = '@';
inline constexpr char MonsterSprite = 'k';
inline constexpr char NPCSprite = 'h';
inline constexpr char GraveSprite = '|';
inline constexpr char SpawnSprite = '_';
inline constexpr char NoneSprite = ' ';

enum class PlayerType { NPC, Monster, Hero };

struct PlayerComponent {
    fastPRNG::fastXS32 rng{};
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
    data.numgy = data.rng.xorShift();
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

static void updateComplex(const PositionComponent& position, VelocityComponent& direction, DataComponent& data) {
    if ((data.thingy % 10) == 0) {
        if (position.x > position.y) {
            direction.x = gsl::narrow_cast<float>(data.rng.xoroshiro64x_Range(3, 19)) - 10.0F;
            direction.y = gsl::narrow_cast<float>(data.rng.xoroshiro64x_Range(0, 5));
        } else {
            direction.x = gsl::narrow_cast<float>(data.rng.xoroshiro64x_Range(0, 5));
            direction.y = gsl::narrow_cast<float>(data.rng.xoroshiro64x_Range(3, 19)) - 10.0F;
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
        if (y >= 0 && uint32_t(y) < m_height) {
            if (x >= 0 && uint32_t(x) < m_width) {
                m_buffer[gsl::narrow_cast<size_t>(x) + gsl::narrow_cast<size_t>(y) * m_width] = c;
            }
        }
    }

private:
    uint32_t m_width;
    uint32_t m_height;
    std::vector<char> m_buffer;
};

static void renderSprite(
    FrameBuffer& out,
    PositionComponent& position,
    SpriteComponent& spr)
{
    out.draw(gsl::narrow_cast<int>(position.x), gsl::narrow_cast<int>(position.y), spr.character);
}

static void updateSprite(
    SpriteComponent& spr,
    const PlayerComponent& player,
    const HealthComponent& health)
{
    spr.character = [&]() {
        switch (health.status) {
        case StatusEffect::Alive:
            switch (player.type) {
            case PlayerType::Hero:
                return PlayerSprite;
            case PlayerType::Monster:
                return MonsterSprite;
            case PlayerType::NPC:
                return NPCSprite;
            }
            break;
        case StatusEffect::Dead:
            return GraveSprite;
        case StatusEffect::Spawn:
            return SpawnSprite;
        }
        return NoneSprite;
    }();
}
