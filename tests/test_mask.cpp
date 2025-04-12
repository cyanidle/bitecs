#include "bitecs/bitecs.hpp"
#include <gtest/gtest.h>

using namespace bitecs;

#define CHECK ASSERT_TRUE

TEST(Mask, Ranks)
{
    bitecs_Ranks ranks;
    bitecs_ranks_get(&ranks, 0b1);
    CHECK(ranks.groups_count == 1);
    CHECK(ranks.group_ranks[0] == 0);
    CHECK(ranks.select_dict_masks[0] == 0);
    bitecs_ranks_get(&ranks, 0b101);
    CHECK(ranks.groups_count == 2);
    CHECK(ranks.group_ranks[0] == 0);
    CHECK(ranks.group_ranks[1] == 2);
    CHECK(ranks.select_dict_masks[0] == 0);
    CHECK(ranks.select_dict_masks[1] == 0b11);
    bitecs_ranks_get(&ranks, 0b110101);
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

TEST(Mask, GetSet) {
    bitecs_SparseMask mask{};
    CHECK(bitecs_mask_set(&mask, 1, true) == true);
    CHECK(bitecs_mask_get(&mask, 1) == true);
    CHECK(bitecs_mask_get(&mask, 512) == false);
    CHECK(bitecs_mask_set(&mask, 512, true) == true);
    CHECK(bitecs_mask_get(&mask, 1) == true);
    CHECK(bitecs_mask_get(&mask, 512) == true);
    CHECK(bitecs_mask_get(&mask, 513) == false);
    CHECK(bitecs_mask_get(&mask, 1023) == false);
    CHECK(bitecs_mask_set(&mask, 513, true) == true);
    CHECK(bitecs_mask_get(&mask, 1) == true);
    CHECK(bitecs_mask_get(&mask, 512) == true);
    CHECK(bitecs_mask_get(&mask, 513) == true);
    CHECK(bitecs_mask_get(&mask, 1023) == false);
    CHECK(bitecs_mask_set(&mask, 1023, true) == true);
    CHECK(bitecs_mask_get(&mask, 1) == true);
    CHECK(bitecs_mask_get(&mask, 1023) == true);
    CHECK(bitecs_mask_set(&mask, 32, true) == true);
    CHECK(bitecs_mask_get(&mask, 1) == true);
    CHECK(bitecs_mask_get(&mask, 1023) == true);
    CHECK(bitecs_mask_get(&mask, 32) == true);
    CHECK(bitecs_mask_set(&mask, 1023, false) == true);
    CHECK(bitecs_mask_get(&mask, 1) == true);
    CHECK(bitecs_mask_get(&mask, 1023) == false);
}

TEST(Mask, Unset) {
    bitecs_SparseMask mask{};
    CHECK(bitecs_mask_set(&mask, 1, true) == true);
    CHECK(mask.dict == 1);
    CHECK(bitecs_mask_set(&mask, 1, false) == true);
    CHECK(mask.dict == 0);
}

TEST(Mask, UnsetNotFull) {
    bitecs_SparseMask mask{};
    CHECK(bitecs_mask_set(&mask, 1, true) == true);
    CHECK(bitecs_mask_set(&mask, 2, true) == true);
    CHECK(mask.dict == 1);
    CHECK(bitecs_mask_set(&mask, 1, false) == true);
    CHECK(mask.dict == 1);
    CHECK(bitecs_mask_set(&mask, 2, false) == true);
    CHECK(mask.dict == 0);
}

TEST(Mask, UnsetOneOfAll) {
    bitecs_SparseMask mask{};
    CHECK(bitecs_mask_set(&mask, 1, true) == true);
    CHECK(bitecs_mask_set(&mask, 33, true) == true);
    CHECK(mask.dict == 0b101);
    CHECK(bitecs_mask_set(&mask, 1, false) == true);
    CHECK(mask.dict == 0b100);
    CHECK(bitecs_mask_set(&mask, 33, false) == true);
    CHECK(mask.dict == 0);
}

TEST(Mask, FromToArray){
    int init[] = {100, 101, 120, 200, 202, 204, 600};
    bitecs_SparseMask mask;
    CHECK(bitecs_mask_from_array(&mask, init, std::size(init)) == true);
    CHECK(bitecs_mask_get(&mask, 100) == true);
    CHECK(bitecs_mask_get(&mask, 101) == true);
    CHECK(bitecs_mask_get(&mask, 102) == false);
    CHECK(bitecs_mask_get(&mask, 120) == true);
    CHECK(bitecs_mask_get(&mask, 200) == true);
    CHECK(bitecs_mask_get(&mask, 202) == true);
    CHECK(bitecs_mask_get(&mask, 203) == false);
    CHECK(bitecs_mask_get(&mask, 204) == true);
    CHECK(bitecs_mask_get(&mask, 600) == true);

    bitecs_BitsStorage back;
    bitecs_Ranks ranks;
    bitecs_ranks_get(&ranks, mask.dict);
    int count = bitecs_mask_into_array(&mask, &ranks, back);
    CHECK(count == std::size(init));
    for (int i = 0; i < count; ++i) {
        CHECK(init[i] == back[i]);
    }
}
