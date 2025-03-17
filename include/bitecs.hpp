#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <bit>
#include <cassert>
#include <climits>
#include <cstdint>
#include <initializer_list>

namespace bitecs
{

using mask_t = __uint128_t;

struct alignas(mask_t) SparseBitMap
{
    uint32_t dict; // which groups of 16 are active out of 32 total
    uint16_t bits[6];

    mask_t as_mask() const noexcept {
        return *reinterpret_cast<const mask_t*>(this);
    }
    static SparseBitMap from_mask(mask_t mask) noexcept {
        return *reinterpret_cast<SparseBitMap*>(&mask);
    }
};

struct Ranks {
    uint8_t ranks[6];
    int popcount;
};

constexpr Ranks get_ranks(uint32_t dict) {
    Ranks res{};
    int out = 0;
    while(dict) {
        if (dict & 1) {
            res.ranks[res.popcount++] = out;
        }
        dict >>= 1;
        out++;
    }
    return res;
}

constexpr mask_t fill_up_to(int bit) {
    return (mask_t(1) << bit) - mask_t(1);
}

inline mask_t relocate_part(uint32_t dictDiff, mask_t mask, int index, const uint8_t* __restrict qranks) {
    int rank = qranks[index];
    int select_mask = fill_up_to(rank);
    int shift = std::popcount(dictDiff & select_mask) * 16;
    int value_mask_offset = (2/*adjust for dict*/ + index) * 16;
    mask_t value_mask = mask_t(0xff'ff) << value_mask_offset;
    mask_t value = mask & value_mask;
    return value << shift;
}

inline mask_t adjust_for(uint32_t dict, mask_t mask, const uint8_t* __restrict qranks) {
    mask_t rdict = mask & mask_t(0xff'ff'ff'ff);
    uint32_t dictDiff = dict ^ rdict;
    mask_t r0 = relocate_part(dictDiff, mask, 0, qranks);
    mask_t r1 = relocate_part(dictDiff, mask, 1, qranks);
    mask_t r2 = relocate_part(dictDiff, mask, 2, qranks);
    mask_t r3 = relocate_part(dictDiff, mask, 3, qranks);
    mask_t r4 = relocate_part(dictDiff, mask, 4, qranks);
    mask_t r5 = relocate_part(dictDiff, mask, 5, qranks);
    return rdict | r0 | r1 | r2 | r3 | r4 | r5;
}

void test() {
    SparseBitMap map{0b10100, {1, 3}};
    Ranks ranks = get_ranks(map.dict);
    auto result_mask = adjust_for(0b11111, map.as_mask(), ranks.ranks);
    SparseBitMap result = SparseBitMap::from_mask(result_mask);
}

}

