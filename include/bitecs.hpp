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
    SUBCASE("Get ranks") {
        bitecs_Ranks ranks;
        bitecs_get_ranks(0b1, &ranks);
        CHECK(ranks.groups_count == 1);
        CHECK(ranks.group_ranks[0] == 0);
        CHECK(ranks.select_dict_masks[0] == 0);
        bitecs_get_ranks(0b101, &ranks);
        CHECK(ranks.groups_count == 2);
        CHECK(ranks.group_ranks[0] == 0);
        CHECK(ranks.group_ranks[1] == 2);
        CHECK(ranks.select_dict_masks[0] == 0);
        CHECK(ranks.select_dict_masks[1] == 0b11);
        bitecs_get_ranks(0b110101, &ranks);
        CHECK(ranks.groups_count == 4);
        CHECK(ranks.group_ranks[0] == 0);
        CHECK(ranks.group_ranks[1] == 2);
        CHECK(ranks.group_ranks[2] == 4);
        CHECK(ranks.group_ranks[3] == 5);
        CHECK(ranks.select_dict_masks[0] == 0);
        CHECK(ranks.select_dict_masks[1] == 0b11);
        CHECK(ranks.select_dict_masks[2] == 0b1111);
        CHECK(ranks.select_dict_masks[3] == 0b11111);
    }
    bitecs_SparseMask mask{};
    _bitecs_sanity_test(&mask);
    CHECK(mask.bits != 0);
    mask.bits = 0;
    SUBCASE("Set/Get") {
        bitecs_mask_set(&mask, 1, true);
        CHECK(bitecs_mask_get(&mask, 1) == true);
        CHECK(bitecs_mask_get(&mask, 512) == false);
        bitecs_mask_set(&mask, 512, true);
        CHECK(bitecs_mask_get(&mask, 1) == true);
        CHECK(bitecs_mask_get(&mask, 512) == true);
        CHECK(bitecs_mask_get(&mask, 513) == false);
        CHECK(bitecs_mask_get(&mask, 1023) == false);
        bitecs_mask_set(&mask, 513, true);
        CHECK(bitecs_mask_get(&mask, 1) == true);
        CHECK(bitecs_mask_get(&mask, 512) == true);
        CHECK(bitecs_mask_get(&mask, 513) == true);
        CHECK(bitecs_mask_get(&mask, 1023) == false);
        bitecs_mask_set(&mask, 1023, true);
        CHECK(bitecs_mask_get(&mask, 1) == true);
        CHECK(bitecs_mask_get(&mask, 1023) == true);
        bitecs_mask_set(&mask, 32, true);
        CHECK(bitecs_mask_get(&mask, 1) == true);
        CHECK(bitecs_mask_get(&mask, 1023) == true);
        CHECK(bitecs_mask_get(&mask, 32) == true);
        bitecs_mask_set(&mask, 1023, false);
        CHECK(bitecs_mask_get(&mask, 1) == true);
        CHECK(bitecs_mask_get(&mask, 1023) == false);
    }
    SUBCASE("Create From Array") {
        int init[] = {100, 101, 120, 200, 202, 204, 600};
        bitecs_SparseMask mask;
        bitecs_mask_from_array(&mask, init, std::size(init));
        CHECK(bitecs_mask_get(&mask, 100) == true);
        CHECK(bitecs_mask_get(&mask, 101) == true);
        CHECK(bitecs_mask_get(&mask, 102) == false);
        CHECK(bitecs_mask_get(&mask, 120) == true);
        CHECK(bitecs_mask_get(&mask, 200) == true);
        CHECK(bitecs_mask_get(&mask, 202) == true);
        CHECK(bitecs_mask_get(&mask, 203) == false);
        CHECK(bitecs_mask_get(&mask, 204) == true);
        CHECK(bitecs_mask_get(&mask, 600) == true);

        SUBCASE("Convert back") {
            bitecs_BitsStorage back;
            bitecs_Ranks ranks;
            bitecs_get_ranks(mask.dict, &ranks);
            int count = bitecs_mask_into_array(&mask, &ranks, &back);
            CHECK(count == std::size(init));
            for (int i = 0; i < count; ++i) {
                CHECK(init[i] == back[i]);
            }
        }
    }
}

}

