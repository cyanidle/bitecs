#pragma once

#if !defined(DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN) && !defined(DOCTEST_CONFIG_IMPLEMENT)
#define DOCTEST_CONFIG_DISABLE
#endif

#include <cstdint>
#include <cstddef>
#include <array>
#include <bit>
#include <cassert>
#include <climits>
#include <cstdint>
#include <initializer_list>
#include "bitecs_core.h"
#include "doctest/doctest.h"

namespace bitecs
{

using dict_t = bitecs_dict_t;
using mask_t = bitecs_mask_t;
using index_t = bitecs_index_t;
using generation_t = bitecs_generation_t;
using comp_id_t = bitecs_comp_id_t;

// TODO: cpp wrappers for bitecs_* types


TEST_CASE("Basic Mask Operations")
{
    bitecs_SparseMask mask{};
    _bitecs_sanity_test(&mask);
    CHECK(mask.bits != 0);
    mask.bits = 0;
    SUBCASE("Set/Get") {
        bitecs_mask_set(1, &mask, true);
        CHECK(bitecs_mask_get(1, &mask) == true);
        CHECK(bitecs_mask_get(512, &mask) == false);
        bitecs_mask_set(512, &mask, true);
        CHECK(bitecs_mask_get(1, &mask) == true);
        CHECK(bitecs_mask_get(512, &mask) == true);
        CHECK(bitecs_mask_get(513, &mask) == false);
        CHECK(bitecs_mask_get(1023, &mask) == false);
        bitecs_mask_set(513, &mask, true);
        CHECK(bitecs_mask_get(1, &mask) == true);
        CHECK(bitecs_mask_get(512, &mask) == true);
        CHECK(bitecs_mask_get(513, &mask) == true);
        CHECK(bitecs_mask_get(1023, &mask) == false);
        bitecs_mask_set(1023, &mask, true);
        CHECK(bitecs_mask_get(1, &mask) == true);
        CHECK(bitecs_mask_get(1023, &mask) == true);
        bitecs_mask_set(32, &mask, true);
        CHECK(bitecs_mask_get(1, &mask) == true);
        CHECK(bitecs_mask_get(1023, &mask) == true);
        CHECK(bitecs_mask_get(32, &mask) == true);
        bitecs_mask_set(1023, &mask, false);
        CHECK(bitecs_mask_get(1, &mask) == true);
        CHECK(bitecs_mask_get(1023, &mask) == false);
    }
    SUBCASE("Create From Array") {
        int init[] = {100, 101, 120, 200, 202, 204, 600};
        bitecs_SparseMask mask;
        bitecs_mask_from_array(&mask, init, std::size(init));
        CHECK(bitecs_mask_get(100, &mask) == true);
        CHECK(bitecs_mask_get(101, &mask) == true);
        CHECK(bitecs_mask_get(102, &mask) == false);
        CHECK(bitecs_mask_get(120, &mask) == true);
        CHECK(bitecs_mask_get(200, &mask) == true);
        CHECK(bitecs_mask_get(202, &mask) == true);
        CHECK(bitecs_mask_get(203, &mask) == false);
        CHECK(bitecs_mask_get(204, &mask) == true);
        CHECK(bitecs_mask_get(600, &mask) == true);

        SUBCASE("Convert back") {
            bitecs_BitsStorage back;
            bitecs_Ranks ranks;
            bitecs_get_ranks(mask.dict, &ranks);
            int set = bitecs_mask_into_array(&mask, &ranks, &back);
            for (int i = 0; i < set; ++i) {
                CHECK(init[i] == back[i]);
            }
        }
    }
}

}

