#include "bitecs_private.h"

void bitecs_ranks_get(bitecs_Ranks* res, dict_t dict)
{
    assert(dict_popcnt(dict) <= BITECS_GROUPS_COUNT);
    *res = (bitecs_Ranks){0};
    int rank = 0;
    while(dict) {
        int trailing = ctz(dict);
        rank += trailing;
        int i = res->groups_count++;
        res->group_ranks[i] = rank;
        res->select_dict_masks[i] = fill_up_to(rank);
        dict >>= trailing + 1;
        rank++;
    }
}

static mask_t relocate_part(dict_t dictDiff, mask_t mask, int index, const dict_t* restrict rankMasks) {
    dict_t select_mask = rankMasks[index];
    int shift = dict_popcnt(dictDiff & select_mask) * BITECS_GROUP_SIZE;
    int value_mask_offset = index * BITECS_GROUP_SIZE;
    mask_t value_mask = (mask_t)fill_up_to(BITECS_GROUP_SIZE);
    mask_t value_mask_shifted = value_mask << value_mask_offset;
    mask_t value = mask & value_mask_shifted;
    return value << shift;
}

static mask_t adjust_for(dict_t diff, mask_t qmask, const dict_t* restrict rankMasks) {
    mask_t r0 = relocate_part(diff, qmask, 0, rankMasks);
    mask_t r1 = relocate_part(diff, qmask, 1, rankMasks);
    mask_t r2 = relocate_part(diff, qmask, 2, rankMasks);
    mask_t r3 = relocate_part(diff, qmask, 3, rankMasks);
    return r0 | r1 | r2 | r3;
}

static bool needs_adjust(dict_t diff, const bitecs_Ranks *ranks)
{
    return diff & ranks->select_dict_masks[ranks->groups_count - 1];
    // if any relocations (at least on biggest mask) -> dicts are incompatible
}

index_t bitecs_query_match(
    index_t cursor, const bitecs_SparseMask* query,
    const bitecs_Ranks *ranks, const bitecs_Entity* entts, index_t count)
{
    for (;cursor < count; ++cursor) {
        const Entity* entt = entts + cursor;
        dict_t edict = entt->dict;
        dict_t qdict = query->dict;
        if ((edict & qdict) != qdict) continue;
        dict_t diff = edict ^ qdict;
        bool adjust = needs_adjust(diff, ranks);
        mask_t mask = query->bits;
        if (adjust) {
            mask = adjust_for(diff, query->bits, ranks->select_dict_masks);
        }
        if ((entt->components & mask) == mask) {
            return cursor;
        }
    }
    return cursor;
}

bitecs_index_t bitecs_query_miss(
    bitecs_index_t cursor, const bitecs_SparseMask* query,
    const bitecs_Ranks *ranks, const bitecs_Entity* entts, bitecs_index_t count)
{
    bitecs_SparseMask adjusted;
    const bitecs_SparseMask* current = query;
    for (;cursor < count; ++cursor) {
        const Entity* entt = entts + cursor;
again:
        if ((entt->dict & current->dict) != current->dict) {
            // missmatch on adjusted query is not definitive!
            if (current != query) {
                current = query;
                goto again;
            }
            return cursor;
        }
        dict_t diff = entt->dict ^ current->dict;
        bool adjust = needs_adjust(diff, ranks);
        mask_t mask = current->bits;
        if (unlikely(adjust)) {
            mask = adjust_for(diff, current->bits, ranks->select_dict_masks);
            adjusted.bits = mask;
            adjusted.dict = entt->dict;
            current = &adjusted;
        }
        if ((entt->components & mask) != mask) {
            return cursor;
        }
    }
    return cursor;
}

bool bitecs_mask_set(bitecs_SparseMask* mask, int index, bool state)
{
    int group = index >> BITECS_GROUP_SHIFT;
    int bit = index & fill_up_to(BITECS_GROUP_SHIFT);
    if (unlikely(!(mask->dict & ((dict_t)1 << group)))) {
        Ranks ranks;
        bitecs_ranks_get(&ranks, mask->dict);
        if (unlikely(ranks.groups_count == BITECS_GROUPS_COUNT)) {
            return false;
        }
        dict_t newDict = mask->dict | ((dict_t)1 << group);
        dict_t diff = newDict ^ mask->dict;
        mask->bits = adjust_for(diff, mask->bits, ranks.select_dict_masks);
        mask->dict = newDict;
    }
    int groupIndex = dict_popcnt(mask->dict & fill_up_to(group));
    int shift = groupIndex * BITECS_GROUP_SIZE + bit;
    bitecs_mask_t selector = (bitecs_mask_t)1 << shift;
    bitecs_mask_t res;
    if (state) {
        res = mask->bits | selector;
    } else {
        res = mask->bits & ~selector;
        // todo: adjust dict if was last bit
    }
    mask->bits = res;
    return true;
}

bool bitecs_mask_get(const bitecs_SparseMask* mask, int index)
{
    int group = index >> BITECS_GROUP_SHIFT;
    int bit = index & fill_up_to(BITECS_GROUP_SHIFT);
    int groupIndex = dict_popcnt(mask->dict & fill_up_to(group));
    int shift = groupIndex * BITECS_GROUP_SIZE + bit;
    dict_t temp_dict = (dict_t)1 << group;
    bool dict_match = mask->dict & temp_dict;
    bool bits_match = mask->bits & (mask_t)1 << shift;
    return dict_match && bits_match;
}

bool bitecs_mask_from_array(bitecs_SparseMask *maskOut, const int *idxs, int idxs_count)
{
#ifndef NDEBUG
    {
        int _last = 0;
        for (int i = 0; i < idxs_count; ++i) {
            assert(idxs[i] > 0 && "bitecs_mask_from_array(): Invalid input");
            assert(_last <= idxs[i] && "bitecs_mask_from_array(): Unsorted input");
            _last = idxs[i];
        }
    }
#endif
    maskOut->dict = 0;
    maskOut->bits = 0;
    for (int i = 0; i < idxs_count; ++i) {
        int value = idxs[i];
        int group = value >> BITECS_GROUP_SHIFT;
        if (unlikely(group > BITECS_BITS_IN_DICT)) {
            return false;
        }
        dict_t newDict = maskOut->dict | (dict_t)1 << group;
        int groupIndex = dict_popcnt(newDict) - 1;
        if (unlikely(groupIndex == BITECS_GROUPS_COUNT)) {
            return false;
        }
        maskOut->dict = newDict;
        int bit = value & fill_up_to(BITECS_GROUP_SHIFT);
        int shift = groupIndex * BITECS_GROUP_SIZE + bit;
        maskOut->bits |= (mask_t)1 << shift;
    }
    return true;
}

void _bitecs_sanity_test(bitecs_SparseMask *out)
{
    out->bits = (mask_t)1 << 95;
}

static void expand_one(int bitOffset, uint32_t part, int offset, bitecs_BitsStorage *storage) {
    int bit = 0;
    int out = 0;
    while(part) {
        int trailing = ctz(part);
        bit += trailing;
        (*storage)[offset + out++] = bitOffset + bit;
        part >>= trailing + 1;
        bit++;
    }
}

int bitecs_mask_into_array(const bitecs_SparseMask *mask, const bitecs_Ranks *ranks, bitecs_BitsStorage *storage)
{
    const uint32_t* groups = (const uint32_t*)&mask->bits;
    int pcnt0 = popcnt32(groups[0]);
    int pcnt1 = popcnt32(groups[1]);
    int pcnt2 = popcnt32(groups[2]);
    int pcnt3 = popcnt32(groups[3]);
    expand_one(ranks->group_ranks[0] << BITECS_GROUP_SHIFT, groups[0], 0, storage);
    expand_one(ranks->group_ranks[1] << BITECS_GROUP_SHIFT, groups[1], pcnt0, storage);
    expand_one(ranks->group_ranks[2] << BITECS_GROUP_SHIFT, groups[2], pcnt0 + pcnt1, storage);
    expand_one(ranks->group_ranks[3] << BITECS_GROUP_SHIFT, groups[3], pcnt0 + pcnt1 + pcnt2, storage);
    return pcnt0 + pcnt1 + pcnt2 + pcnt3;
}
