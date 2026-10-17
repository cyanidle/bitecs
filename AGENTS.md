# AGENTS.md

Guidance for AI coding agents working on `bitecs`.

## What this project is

`bitecs` is a cache-friendly ECS (entity component system): a C17 core with a header-only C++17
wrapper, plus a benchmark suite comparing it with flecs, EnTT, entityx, gaia, ginseng and mustache.

- `include/bitecs/bitecs_core.h` — public C API: entity/component types, masks, registry, systems.
- `src/bitecs_core.c` — C implementation (storage, queries, cleanup). Compiled as C, never as C++.
- `include/bitecs/bitecs_impl.hpp` — C++ internals: component ids, callback thunks, argument deduction.
- `include/bitecs/bitecs.hpp` — user-facing `bitecs::Registry`, `Components<>`, `BITFUNC`.
- `tests/` — GoogleTest suites for masks and the registry.
- `benchmarks/` — Google Benchmark workloads and adapters for competing ECS libraries.
- `CMakeLists.txt`, `conanfile.py`, `cmake/` — build and dependency glue.

There is no README; this file is the entry point. License: MIT (see `LICENSE`).

## Build and test

Requirements: CMake >= 3.16, a C17/C++17 compiler, Ninja (recommended), Conan 2 (>= 2.0.5) for
dependencies. Conan is bootstrapped by CMake and detects the default profile automatically.

### Tests (normal Conan flow)

```sh
cmake -S . -B build/local -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBITECS_TEST=ON
cmake --build build/local -j
ctest --test-dir build/local --output-on-failure
```

`BITECS_TEST=ON` enables the Conan integration (see `cmake/conan2.cmake`), which installs gtest.
The default `CONAN_INSTALL_ARGS` also sets `-o &:bench=True`, so this pulls the whole benchmark
dependency set. To install only what the tests need:

```sh
cmake -S . -B build/local -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBITECS_TEST=ON \
  -DCONAN_INSTALL_ARGS="--build=missing"
```

If Conan is unavailable, pass `-DCONAN_BUILDING=ON` to skip the Conan bootstrap and let
`find_package` resolve dependencies from the system/prefix path:

```sh
cmake -S . -B build/local -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBITECS_TEST=ON -DCONAN_BUILDING=ON
```

That is the same flag `conanfile.py` uses while building the package. Note the condition in
`CMakeLists.txt` is `BITECS_BENCH OR BITECS_TEST AND NOT CONAN_BUILDING`: with `BITECS_BENCH=ON`,
Conan is invoked even when `CONAN_BUILDING=ON`.

### Benchmarks

```sh
cmake -S . -B build/bench -G Ninja -DCMAKE_BUILD_TYPE=Release -DBITECS_BENCH=ON
cmake --build build/bench -j
./build/bench/bitecs_bench --benchmark_filter='Bitecs/BM_Systems'
```

The first configure downloads `ginseng` and `mustache` from GitHub through CPM, so it needs network
access (ask the user first in a restricted environment). Never run `ctest` on the benchmark target
blind: it executes the full matrix, including 500k-entity cases. Use `--benchmark_filter`.

### Conan package build

```sh
conan build . -o '&:bench=True'
```

This builds the library and, with the option, the benchmarks. The recipe declares gtest as a
`test_requires` but does not enable `BITECS_TEST`, so the test executable is built through the CMake
flow above.

Existing `build/GCC-*` and `build/Clang-*` directories are local IDE artifacts and are gitignored;
they may reference stale Conan paths. Prefer a fresh build directory.

### Tests: conventions

- One test binary, `bitecs_test`; its source list is explicit in `CMakeLists.txt` — add new test
  files there (there is no glob for tests).
- Put mask/`SparseMask` tests in `tests/test_mask.cpp`; registry, entity lifecycle, systems, cleanup
  and merge tests in `tests/test_registry.cpp`.
- Debug builds are the meaningful ones: the core asserts invariants, and tests exercise boundaries
  up to 30k entities. Keep the suite fast.
- Every behavior change deserves a test; bug fixes should add the smallest reproducing case.

## How the ECS works

- **Entities** are a dense array of `{dict, components, generation, flags}`. Index + generation form
  `EntityPtr`; destroying bumps the generation, so stale pointers are rejected by `deref`. Freed
  indices go to a coalescing free list and are reused.
- **Components** are stored per component type in aligned chunks. Chunk capacity is
  `1 << (frequency + BITECS_FREQUENCY_ADJUST)` (`frequency` is `freq1..freq9`, `ADJUST` is 6), and
  each chunk tracks `nalives` for deferred cleanup. Empty trivially-destructible types are tags:
  `typesize == 0`, no storage.
- **Sparse masks** encode an entity's component set in two words: `dict` marks which 16-id groups
  are present; `bits` holds four 16-bit fields compacted by group *rank*. Queries rank-adjust the
  query mask per entity when the entity has groups the query does not. This is the central trick;
  mask layout changes are high risk.
- **Systems** scan the entity array once: `bitecs_query_match` finds the start of a run,
  `bitecs_query_miss` finds its end, and the run is split at component-chunk boundaries before the
  callback receives `EntityProxy*`, one `void*` per component, and a count. Component data is
  AoS-per-type, so iteration is linear and allocation-free.
- **Deferred cleanup**: destroying/removing marks empty chunks and sets `chunks_cleanup_pending`.
  `PrepareCleanup` collects them, `Cleanup` frees them. The cleanup data is owned by the caller and
  is freed by `Cleanup`; call it exactly once.
- **Merging** (`MergeFrom`) appends the source entity array and relocates component data, consuming
  the source's entities. Registries must have the same component definitions.

## Using the C++ API

```cpp
#include <bitecs/bitecs.hpp>

struct Position { enum { bitecs_id = 0 }; float x = 0, y = 0; };
struct Velocity { enum { bitecs_id = 1 }; float x = 0, y = 0; };

bitecs::Registry reg;
reg.DefineComponent<Position>(bitecs_freq7);
reg.DefineComponent<Velocity>(bitecs_freq7);

auto e = reg.Entt(Position{1, 2}, Velocity{3, 4});
reg.Entts<Position, Velocity>(100, [](Position& p, Velocity& v) { /* populate */ });

reg.RunSystem<Position, Velocity>([](Position& p, Velocity& v) {
    p.x += v.x;
    p.y += v.y;
});
reg.RunSystem<Velocity>([](bitecs::EntityPtr e, Velocity& v) { /* per-entity */ });
```

Rules that are easy to violate:

- Every component needs a compile-time id, either `enum { bitecs_id = N };` or the
  `BITECS_COMPONENT(Type, N)` macro. Ids must be `< BITECS_MAX_COMPONENTS` (1024) and unique within
  a registry; two C++ types with the same id are the same component to bitecs.
- A single `Components<A, B, ...>` list (and therefore one entity/query) may span at most
  `BITECS_GROUPS_COUNT` (4) id groups of 16, i.e. at most 64 simultaneous component ids. Ids are
  sorted and converted to a sparse mask by `Components<>`; `bitecs_mask_from_array` requires sorted
  input (asserted in debug).
- `RunSystem`/`Entts` deduce component types from the callable's argument list only when the
  callable is a non-generic lambda/functor. Generic lambdas and plain function names need an
  explicit template list; function pointers must be wrapped with `BITFUNC(func)`.
- Callables may take `(EntityPtr, Comps&...)`, `(EntityProxy*, Comps&...)`, or `(Comps&...)`; the
  thunk picks the first matching form. `EntityProxy*` is a temporary view into the entity array and
  must never be stored (the array can reallocate on growth).
- `Entt`/`Entts` default-construct components and then assign/move into them, so wrapped components
  must be default-constructible (and move-assignable for `Entt`/`EnttsFromArrays`). Non-trivial
  destructors and move constructors are detected by `DefineComponent`, which installs the deleter
  and relocator hooks; do not bypass `DefineComponent`.
- `Registry` is non-copyable and movable. The C layer reports failure through `bool`/`NULL`; the
  wrapper converts failures into `std::runtime_error`. Keep that split.
- System `flags` filter entities with `(entity.flags & flags) == flags`; flags are zeroed on
  creation and can be set through the `EntityProxy::flags` field.

## Code conventions

- Match the surrounding style: 4-space indentation, LF endings, braces on their own line for
  functions/classes, no formatter config exists.
- C layer: `bitecs_` prefix on public symbols, snake_case, plain C types, `extern "C"` in the header,
  only C standard headers in `bitecs_core.h`. No exceptions, no allocation-free promises that the
  code does not keep.
- C++ layer: PascalCase types, CamelCase `Registry` methods, internals under `bitecs::impl`,
  `_BITECS_INLINE`/`_BITECS_FLATTEN` on hot helpers. Keep the wrapper header-only.
- Files start with the MIT header comment. Several headers and benchmarks are UTF-8 with a BOM;
  preserve the existing BOM and line endings when editing.
- Comments are sparse and usually mark non-obvious invariants or TODOs. Do not add narration;
  preserve existing TODO/design notes unless the issue is actually resolved.
- The core uses GCC/Clang builtins (`__builtin_expect`, popcount, ctz), C11 atomics and VLAs. The
  MSVC branches in CMake exist but the core is not MSVC-clean; do not assume MSVC support.
- `-fexceptions` is added to the core for GCC so C++ exceptions can unwind through C frames; keep it.
- This is a benchmark-driven project: avoid extra allocations, indirection or branches in query and
  chunk hot paths, and measure with `bitecs_bench` before/after.

## Adding code

- New core behavior: change `bitecs_core.c` + `bitecs_core.h` if it is public, update the C++ wrapper,
  and add tests. Keep the C header ABI-stable unless a break is intended (`BITECS_INDEX_T` is a
  compile-time override).
- New high-level helper: put it in `bitecs.hpp`/`bitecs_impl.hpp` unless it must live in C.
- New test file: add it to the `add_executable(bitecs_test ...)` list in `CMakeLists.txt`.
- New benchmark adapter: create `benchmarks/<name>.cpp`, implement the `ECS_Interface` shape from
  `benchmarks/benchmark.hpp` (see `benchmarks/entt.cpp`), call `ECS_BENCHMARKS(Name)` inside a unique
  namespace, then add the dependency to `conanfile.py` and the `find_package`/link line in
  `CMakeLists.txt`. `benchmarks/*.cpp` is globbed at configure time.
- Do not commit `build/`, IDE files (`CMakeLists.txt.user` is ignored), or generated Conan files.

## Known rough edges

These are in the code today; verify before relying on them or treat them as work to finish:

- `bitecs_mask_set` uses the raw group number instead of the group rank when recompacting after the
  last bit of a group is removed (`fill_up_to(BITECS_GROUP_SIZE * group)`); there is a TODO and a
  reproducing test (`Mask.Remove`: 101 | 303, remove 101, 303 is lost). Mask removal is not reliable
  until this is fixed.
- `bitecs_entt_destroy_batch` never starts its run counter, so it destroys nothing; use
  `bitecs_entt_destroy` per entity until fixed. The wrapper's `DestroyBatch` exposes it.
- `bitecs_registry_clone_settings` only aliases component lists into the output registry and returns
  `false`; the documented background-loading workflow is unfinished and the aliasing is unsafe.
- `bitecs_system_run_many` and `bitecs_threadpool_*` are empty stubs; there is no threaded system
  execution yet.
- `bitecs_entt_create` can leak partially created entities on an allocation failure inside the
  chunk loop (marked with a TODO).

## Verification checklist

Before reporting work complete:

1. Build in Debug with tests enabled and run `ctest --output-on-failure`.
2. Build the changed target with the strict warnings from `CMakeLists.txt` (`-Wall -Wextra -pedantic`
   on the core, `/W4` for MSVC) and fix new warnings.
3. If performance-sensitive code changed, run the relevant `bitecs_bench` filter in a Release build
   and report the delta.
4. Keep public headers C-compatible and the wrapper C++17-compatible.
