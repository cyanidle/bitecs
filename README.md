# bitecs

A cache-friendly ECS: C17 core, header-only C++17 wrapper, Google Benchmark suite comparing it with
flecs, EnTT, entityx, ginseng and mustache.

```cpp
#include <bitecs/bitecs.hpp>

struct Position { enum { bitecs_id = 0 }; float x = 0, y = 0; };
struct Velocity { enum { bitecs_id = 1 }; float x = 0, y = 0; };

bitecs::Registry reg;
reg.DefineComponent<Position>(bitecs_freq7);
reg.DefineComponent<Velocity>(bitecs_freq7);

auto e = reg.Entt(Position{1, 2}, Velocity{3, 4});
reg.RunSystem<Position, Velocity>([](Position& p, Velocity& v) {
    p.x += v.x;
    p.y += v.y;
});
```

Build, test and contribution rules live in [AGENTS.md](AGENTS.md). This document describes the
sparse component mask — the central data structure of the ECS.

# Sparse component masks

## The problem

There are up to **1024** component ids (`BITECS_MAX_COMPONENTS`), but a query must decide whether an
entity has a set of components in **one or two machine words**. Storing 1024 bits per entity is too
much, so bitecs splits an entity's component set into two 64-bit words:

| word | meaning |
|---|---|
| `dict` | which 16-id **groups** are present (one bit per group) |
| `bits` | four 16-bit **slots**, one per present group, packed by rank |

An entity may use at most **4 groups**, i.e. at most **64 component ids at once**
(`BITECS_GROUPS_COUNT * BITECS_GROUP_SIZE`). The whole mask is two `uint64_t`s:

```c
typedef struct {
    uint64_t dict;
    uint64_t bits;
} bitecs_SparseMask;
```

Every entity stores this pair inline (`Entity::dict`, `Entity::components`), so scanning entities is
just a few 64-bit operations with no memory indirection.

## Id → group → bit

```
component id (0..1023):  [ group: bits 9..4 ][ bit: bits 3..0 ]
                              id >> 4           id & 15

group = id >> 4          (0..63)
bit   = id & 15          (0..15)
```

`dict` has one bit per group:

```
dict bit:  63 .................................................... 2  1  0
           +------------------------------------------------------+-+-+
           |                 one bit per group id                 | | |
           +------------------------------------------------------+-+-+
```

`bits` is four 16-bit slots. Slot 0 is the least significant:

```
bits:  [ slot 3: bits 63..48 ][ slot 2: bits 47..32 ][ slot 1: bits 31..16 ][ slot 0: bits 15..0 ]
```

A present group is placed into slot

```
slot(group) = popcount(dict & ((1 << group) - 1))
            = number of present groups with a smaller group id
```

so slots are filled in ascending slot order, starting at slot 0 (the rightmost slot in the diagram
below), with no holes. The component's own bit stays at `id & 15` inside its slot:

```
bit position = slot(group) * 16 + (id & 15)
```

## Example: an entity with components `101` and `303`

```
101 = group 6,  bit 5
303 = group 18, bit 15
```

Both groups are present, `bits` holds two occupied slots:

```
dict = 0x0000000000040040        (group 6 and group 18)

        bit 63 ........................ bit 18 ........ bit 6 ........ bit 0
        +--------------------------------+---------------+---------------+
dict =  | 0000 0000 0000 0000 0000 0000 0 | 1             | 1             |
        +--------------------------------+---------------+---------------+

bits = 0x0000000080000020

        slot 3          slot 2          slot 1              slot 0
        +---------------+---------------+-------------------+-------------------+
bits =  | 0000 0000 0000 0000 | 0000 0000 0000 0000 | 1000 0000 0000 0000 | 0000 0000 0010 0000 |
        +---------------+-------------------+-------------------+-------------------+
                                                  ^ bit 15            ^ bit 5
                                                  component 303       component 101
                                                  (group 18, rank 1)  (group 6, rank 0)
```

`ranks` derived from `dict`:

```
slot 0 <- group 6   (rank 0)
slot 1 <- group 18  (rank 1)
```

Note that component ids themselves are *not* stored in `bits` — only their in-group bit. The group
comes back from `dict` (through the rank), which is why the same 64 bits cover all 1024 ids.

## Why the compaction

Two masks with identical `dict` can be compared with a single AND:

```c
(entity.bits & query.bits) == query.bits
```

This is the whole match test only when the entity's groups are exactly the query's groups, or its
extra groups are all *above* every query group (their ranks do not move the query slots). If the
entity has extra groups below or between query groups, the query slots must be rank-adjusted first —
see "Query matching" below. The price of the compaction is that adding or removing a group changes
the rank of every group above it, so their slots must shift. All shifts are done by 16-bit steps with
`adjust_for` (below).

# Basic operations

## `bitecs_mask_from_array`: build from sorted ids

Input must be sorted ascending (asserted in Debug). Groups therefore appear in ascending order, and
the new group's slot is its rank among the groups seen so far:

```
dict = bits = 0
for each id (ascending):
    group = id >> 4
    newDict = dict | (1 << group)
    slot = popcount(newDict) - 1          // this group's rank
    bits |= 1 << (slot * 16 + (id & 15))
    dict = newDict
```

```
ids:  1, 33, 50

1  -> group 0, bit 1  -> dict {0}        bits [ slot0: 0x0002 ]
33 -> group 2, bit 1  -> dict {0,2}      bits [ slot1: 0x0002 | slot0: 0x0002 ]
50 -> group 3, bit 2  -> dict {0,2,3}    bits [ slot2: 0x0004 | slot1: 0x0002 | slot0: 0x0002 ]

dict = 0b1101 = 0x0D        bits = 0x0000000400020002
```

More than four groups makes the function return `false`.

## `bitecs_mask_get`: test one id

```
group = id >> 4
if (!(dict & (1 << group)))          return false   // group absent
slot  = popcount(dict & ((1 << group) - 1))
return (bits >> (slot * 16 + (id & 15))) & 1
```

No shifts of `bits` are needed: the `dict` check already rejects absent groups.

## `bitecs_mask_set(id, true)`: add a component

**Group already present** — just set the bit:

```
slot = popcount(dict & ((1 << group) - 1))
bits |= 1 << (slot * 16 + (id & 15))
```

**New group** — first open a hole at the new group's rank, then set the bit:

```
1. ranks = bitecs_ranks_get(dict)          // ranks of the OLD dict
2. if ranks.groups_count == 4: return false
3. newDict = dict | (1 << group)
   diff    = newDict ^ dict                // only the new group
   bits    = adjust_for(diff, bits, ranks.select_dict_masks)
4. dict = newDict
5. set the bit in its new slot
```

`adjust_for` shifts every slot at or above the insertion point one slot to the left: it counts how
many groups in `diff` lie below each occupied group and shifts that group's 16-bit value by
`16 * count`. Adding a group therefore moves existing higher groups up, keeping the rank order:

```
add group 18 to {group 6}:

before:  dict {6}          bits [ slot0: group 6 ]
after:   dict {6,18}       bits [ slot1: group 18 | slot0: group 6 ]   (group 6 unchanged,
                                                                        new slot 1 is empty)
```

(The newly added group is filled by step 5.)

## `bitecs_mask_set(id, false)`: remove a component

```
slot = popcount(dict & ((1 << group) - 1))
bits &= ~(1 << (slot * 16 + (id & 15)))
```

If the slot is still non-empty, that is all. If it was the **last bit of its group**, the group
disappears from `dict` and every higher slot is compacted one position to the right:

```
tail = low 16 * slot bits          // slots below the removed group: unchanged
head = bits & ~tail                // slots above the removed group
bits = tail | (head >> 16)
dict &= ~(1 << group)
```

Example — remove `33` (group 2, slot 1) from `{1, 33, 50}`:

```
before: dict {0,2,3}    bits [ slot2: group 3 | slot1: group 2 | slot0: group 0 ]
        slot2 = 0x0004 (50)   slot1 = 0x0002 (33)   slot0 = 0x0002 (1)

remove 33, slot 1 becomes empty, drop group 2 from dict:

        dict {0,3}      bits [ slot2: group 3 | slot1: 0x0000 | slot0: group 0 ]

compact higher slots right by 16:

after:  dict {0,3}      bits [ slot1: group 3 | slot0: group 0 ]
        slot1 = 0x0004 (50)                          slot0 = 0x0002 (1)
```

Removing a component whose group is absent is a no-op for the result (the group is temporarily added
and then removed again); removing the fifth group is rejected with `false`.

## `bitecs_ranks_get`: `dict` → group ranks

```
for each set bit in dict (ascending):
    i = next free index
    group_ranks[i]       = group id
    select_dict_masks[i] = (1 << group id) - 1   // mask of groups below it
    groups_count++
highest_select_mask = select_dict_masks[groups_count - 1]
```

Example `dict = {6, 18}`:

```
group_ranks       = [6, 18]
select_dict_masks = [ (1<<6)-1 = 0x3F, (1<<18)-1 = 0x3FFFF ]
highest_select_mask = 0x3FFFF
```

`select_dict_masks[i]` answers "how many groups of `diff` are below group `i`" in one popcount.

## Query matching: adjusting the query mask per entity

A query is a mask built from the queried components. The entity matches only if:

```c
(entity.dict & query.dict) == query.dict        // all queried groups exist
```

If the entity has **extra groups**, their ranks shift the query's slots. The query mask is corrected
on the fly:

```
diff = entity.dict ^ query.dict                  // groups only the entity has
if (diff & ranks.highest_select_mask)            // an extra group is below a query group
    adjusted = adjust_for(diff, query.bits, ranks.select_dict_masks)
else
    adjusted = query.bits

match = (entity.bits & adjusted) == adjusted
```

Example — query `{33}` against entity `{1, 33}`:

```
query  {33}: dict {2}     bits [ slot0: 0x0002 ]   (33 = group 2, bit 1)
entity {1,33}: dict {0,2} bits [ slot1: 0x0002 | slot0: 0x0002 ]
diff = {0}   (entity has group 0, query does not)

ranks(query): group_ranks[0] = 2, select_dict_masks[0] = (1<<2)-1 = 0b11
popcount(diff & 0b11) = 1  ->  query slot 0 shifts left by 16

adjusted query bits: [ slot1: 0x0002 ]
entity bits & adjusted == adjusted  ->  match
```

If extra entity groups lie only *above* all query groups, no adjustment is needed
(`needs_adjust` checks exactly that with `highest_select_mask`).

## `bitecs_mask_into_array`: mask → component ids

Reverse mapping, using ranks computed from the same `dict`:

```
for slot in 0..3:
    group = ranks.group_ranks[slot]
    for each bit set in bits[slot]:
        out[n++] = group * 16 + bit
```

Example `{1, 33, 50}` with `group_ranks = [0, 2, 3]`:

```
slot0 = 0x0002 -> 0*16 + 1 = 1
slot1 = 0x0002 -> 2*16 + 1 = 33
slot2 = 0x0004 -> 3*16 + 2 = 50
```

## Invariants

- `popcount(dict) <= 4`; a fifth group is rejected by `mask_set` / `from_array`.
- `bits` slots at index `>= popcount(dict)` are zero.
- Slot `i` always holds the group with rank `i`; no holes.
- `bitecs_mask_get`/`set` require `id < 1024`.
- `bitecs_mask_from_array` requires ascending input; `bitecs_mask_into_array` requires `ranks`
  computed from the same `dict`.
- `dict == dead_entt` (`~0`) marks a dead entity, not a valid mask.

# Where masks are used

| place | operation |
|---|---|
| `Entity` | stores `dict` + `components` inline |
| `Components<A,B,...>` | builds and caches the query/creation mask from sorted compile-time ids |
| `bitecs_entt_create` | copies the mask into every created entity |
| `bitecs_entt_add_component` / `remove_component` | `mask_set(..., true/false)` |
| `bitecs_entt_get_component` | `mask_get` |
| `bitecs_system_run` | `query_match` / `query_miss` with per-entity `adjust_for` |
| `destroy_arch` | `mask_into_array` to find the components to destroy |

# Tests

`tests/test_mask.cpp` covers every operation above, including slot compaction of a middle group and
group 63 (`id` 1008..1023). Run the suite as described in [AGENTS.md](AGENTS.md).
