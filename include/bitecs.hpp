#pragma once

#include "bitecs_core.h"
#include <bit>
#include <cassert>
#include <climits>

namespace bitecs
{

constexpr int groupSize = 4;
constexpr int groupMask = 0xf;

struct SparseBitMap
{
    // max total components: 128
    // components in single query: [8 - 32]
    // 4x compression ratio
    uint32_t dict; //32 groups by 4 bits
    uint32_t bits; //8 groups of 4 (out of 32)
};


inline bool matches(uint32_t queryDict, uint32_t query, uint32_t compDict, uint32_t comps)
{
    assert((compDict & queryDict) == queryDict && "query must be compatible");
    if (queryDict == compDict) {
        return (query & comps) == query;
    } else {
        while(queryDict) {
            if (compDict & 1) {
                if (queryDict & 1) {
                    auto qmask = query & groupMask;
                    auto cmask = comps & groupMask;
                    if ((qmask & cmask) != qmask) {
                        return false;
                    }
                    query >>= groupSize;
                }
                comps >>= groupSize;
            }
            queryDict >>= 1;
            compDict >>= 1;
        }
        return true;
    }
}

inline size_t first_match(SparseBitMap query, const SparseBitMap* components, size_t count)
{
    for (size_t idx = 0; idx < count; ++idx) {
        const auto& comp = components[idx];
        if ((comp.dict & query.dict) == query.dict && matches(query.dict, query.bits, comp.dict, comp.bits)) {
            return idx;
        }
    }
    return count;
}

inline size_t first_miss(SparseBitMap query, const SparseBitMap* components, size_t count)
{
    for (size_t idx = 0; idx < count; ++idx) {
        const auto& comp = components[idx];
        if ((comp.dict & query.dict) != query.dict || !matches(query.dict, query.bits, comp.dict, comp.bits)) {
            return idx;
        } else if (comp.dict != query.dict) {
            // todo: optimize query for current component here
        }
    }
    return count;
}

}

