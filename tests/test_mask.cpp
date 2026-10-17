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
    bitecs_ranks_get(&ranks, (bitecs_dict_t)1 << 63);
    CHECK(ranks.groups_count == 1);
    CHECK(ranks.group_ranks[0] == 63);
    CHECK(ranks.select_dict_masks[0] == ~((bitecs_dict_t)1 << 63));
    CHECK(ranks.highest_select_mask == ranks.select_dict_masks[0]);
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

TEST(Mask, FromArray){
    unsigned init[] = {1003};
    bitecs_SparseMask mask;
    CHECK(bitecs_mask_from_array(&mask, init, std::size(init)) == 1);
    CHECK(bitecs_mask_get(&mask, 1003) == true);

    CHECK(bitecs_mask_set(&mask, 1003, false) == true);
    CHECK(bitecs_mask_get(&mask, 1003) == false);

    CHECK(mask.bits == 0);
    CHECK(mask.dict == 0);

    CHECK(bitecs_mask_set(&mask, 1003, true) == true);
    CHECK(bitecs_mask_get(&mask, 1003) == true);

    bitecs_Ranks ranks;
    bitecs_ranks_get(&ranks, mask.dict);
    CHECK(ranks.groups_count == 1);
}


TEST(Mask, FromToArray){
    unsigned init[] = {120, 200, 202, 204, 600, 1003};
    bitecs_SparseMask mask;
    CHECK(bitecs_mask_from_array(&mask, init, std::size(init)) == true);
    CHECK(bitecs_mask_get(&mask, 102) == false);
    CHECK(bitecs_mask_get(&mask, 120) == true);
    CHECK(bitecs_mask_get(&mask, 200) == true);
    CHECK(bitecs_mask_get(&mask, 202) == true);
    CHECK(bitecs_mask_get(&mask, 203) == false);
    CHECK(bitecs_mask_get(&mask, 204) == true);
    CHECK(bitecs_mask_get(&mask, 600) == true);
    CHECK(bitecs_mask_get(&mask, 1003) == true);
    CHECK(bitecs_mask_get(&mask, 1004) == false);

    bitecs_BitsStorage back;
    bitecs_Ranks ranks;
    bitecs_ranks_get(&ranks, mask.dict);
    int count = bitecs_mask_into_array(&mask, &ranks, back);
    CHECK(count == std::size(init));
    for (int i = 0; i < count; ++i) {
        CHECK(init[i] == back[i]);
    }
}


struct Test500 {
    enum {bitecs_id = 500};
};

struct Test1003 {
    enum {bitecs_id = 1003};
};


TEST(Components, List)
{
    Components<Test500> c1;
    Components<Test1003> c2;

    bitecs_BitsStorage back;
    bitecs_Ranks ranks;
    bitecs_ranks_get(&ranks, c1.list.mask.dict);
    int count = bitecs_mask_into_array(&c1.list.mask, &ranks, back);
    CHECK(count == 1);
    CHECK(back[0] == 500);

    bitecs_ranks_get(&ranks, c2.list.mask.dict);
    count = bitecs_mask_into_array(&c2.list.mask, &ranks, back);
    CHECK(count == 1);
    CHECK(back[0] == 1003);
}

TEST(Mask, RemoveMiddleGroup)
{
    unsigned init[] = {1, 33, 50};
    bitecs_SparseMask mask;
    CHECK(bitecs_mask_from_array(&mask, init, std::size(init)) == true);
    CHECK(mask.dict == 0b1101);

    CHECK(bitecs_mask_set(&mask, 33, false) == true);
    CHECK(mask.dict == 0b1001);
    CHECK(bitecs_mask_get(&mask, 1) == true);
    CHECK(bitecs_mask_get(&mask, 33) == false);
    CHECK(bitecs_mask_get(&mask, 50) == true);

    bitecs_BitsStorage back;
    bitecs_Ranks ranks;
    bitecs_ranks_get(&ranks, mask.dict);
    int count = bitecs_mask_into_array(&mask, &ranks, back);
    CHECK(count == 2);
    CHECK(back[0] == 1);
    CHECK(back[1] == 50);
}


struct Test101 {
    enum {bitecs_id = 101};
};

struct Test103 {
    enum {bitecs_id = 303};
};


TEST(Mask, Remove)
{
    auto mask = Components<Test101, Test103>::list.mask;

    bitecs_BitsStorage back;
    bitecs_Ranks ranks;
    bitecs_ranks_get(&ranks, mask.dict);

    CHECK(bitecs_mask_get(&mask, 101) == true);
    CHECK(bitecs_mask_get(&mask, 303) == true);

    int count = bitecs_mask_into_array(&mask, &ranks, back);
    CHECK(count == 2);

    CHECK(bitecs_mask_set(&mask, 101, false) == true);

    CHECK(bitecs_mask_get(&mask, 101) == false);
    CHECK(bitecs_mask_get(&mask, 303) == true);

    bitecs_ranks_get(&ranks, mask.dict);
    count = bitecs_mask_into_array(&mask, &ranks, back);
    CHECK(count == 1);
}
