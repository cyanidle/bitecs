#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <bit>
#include <cassert>
#include <climits>
#include <cstdint>
#include <initializer_list>
#include "bitecs_core.h"

namespace bitecs
{

using dict_t = bitecs_dict_t;
using mask_t = bitecs_mask_t;
using Entity = bitecs_Entity;
using Ranks = bitecs_Ranks;

inline void test_masks() {
    dict_t dict{};
    mask_t mask{};
    bitecs_flag_set(1, true, &dict, &mask);
    bitecs_flag_set(512, true, &dict, &mask);
    auto a1 = bitecs_flag_get(1023, dict, mask);
    bitecs_flag_set(1023, true, &dict, &mask);
    auto a2 = bitecs_flag_get(1023, dict, mask);
    bitecs_flag_set(32, true, &dict, &mask);
    auto a3 = bitecs_flag_get(1023, dict, mask);
    bitecs_flag_set(1023, false, &dict, &mask);
    auto a4 = bitecs_flag_get(1023, dict, mask);
}

inline void test() {
    test_masks();
}

}

