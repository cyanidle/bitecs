# AGENTS.md

Guidance for AI agents working on `bitecs`: a cache-friendly ECS with a C17 core, a header-only C++17
wrapper, and a Google Benchmark suite against flecs, EnTT, entityx, ginseng and mustache.
[README.md](README.md) documents the sparse-mask design; this file covers development.

## Layout

- `include/bitecs/bitecs_core.h` — public C API (types, registry, masks, systems).
- `src/bitecs_core.c` — C implementation: storage, queries, cleanup. Compile as C only.
- `include/bitecs/bitecs_impl.hpp` — C++ internals: ids, callback thunks, argument deduction.
- `include/bitecs/bitecs.hpp` — public `bitecs::Registry`, `Components<>`, `BITFUNC`.
- `tests/` — GoogleTest for masks (`test_mask.cpp`) and registry/entities/systems/cleanup/merge
  (`test_registry.cpp`).
- `benchmarks/` — workloads and adapters for competing ECS libraries.
- `CMakeLists.txt`, `cmake/`, `conanfile.py` — build glue. License: MIT.

## Build and test

Requirements: CMake >= 3.20, C17/C++17 compiler, Ninja, Conan 2 (>= 2.0.5, auto-detected).

```sh
# tests (Conan; the default install args also pull benchmark deps)
cmake -S . -B build/local -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBITECS_TEST=ON
cmake --build build/local -j
ctest --test-dir build/local --output-on-failure

# tests with system gtest, no Conan bootstrap
cmake -S . -B build/local -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBITECS_TEST=ON -DCONAN_BUILDING=ON

# benchmarks (first configure downloads ginseng/mustache via CPM -> needs network)
cmake -S . -B build/bench -G Ninja -DCMAKE_BUILD_TYPE=Release -DBITECS_BENCH=ON
cmake --build build/bench -j
./build/bench/bitecs_bench --benchmark_filter='BM_Systems<Bitecs>'
```

- `BITECS_BENCH=ON` enters the Conan block even with `CONAN_BUILDING=ON`.
- Never run the whole benchmark matrix blind: it includes 500k-entity cases. Filter.
- `conan build . -o '&:bench=True'` builds the package and benchmarks but not `bitecs_test`.
- Do not commit `build/` or IDE/Conan artifacts.

## Test conventions

- One binary, `bitecs_test`; its sources are listed explicitly in `CMakeLists.txt` (no glob) — add
  new test files there.
- Debug is the meaningful build: the core asserts invariants.
- Every behavior change needs a test; bug fixes need the smallest reproducer. Keep the suite fast.

## How it works

- **Entities**: dense array of `{dict, components, generation, flags}`; `EntityPtr` = index +
  generation. Freed indices go through a coalescing free list.
- **Components**: per-type aligned chunks of `1 << (frequency + BITECS_FREQUENCY_ADJUST)` elements;
  each chunk tracks `nalives` for deferred cleanup.
- **Masks**: `dict` + `bits` encode an entity's components in two 64-bit words. Read README.md before
  changing mask code; the layout is the hot path's core.
- **Systems**: one linear scan; `query_match`/`query_miss` bound a run, which is split at chunk
  boundaries; the callback gets `EntityProxy*`, one `void*` per component, and a count.
- **Cleanup**: empty chunks are marked, `PrepareCleanup` collects, `Cleanup` frees. Call `Cleanup`
  exactly once; it frees the data it is given.
- **Merge**: `MergeFrom` appends entities and relocates component data, consuming the source.

## C++ API rules

- Component ids: `enum { bitecs_id = N };` or `BITECS_COMPONENT(Type, N)`. Unique per registry,
  `< 1024`. Two types sharing an id are the same C++ component.
- One `Components<A, B, ...>` list may span at most 4 groups of 16 ids (64 components).
- `RunSystem`/`Entts` deduce types from a non-generic lambda/functor only; use an explicit template
  list for generic lambdas/function names, and `BITFUNC(func)` for function pointers.
- Callable forms: `(EntityPtr, Comps&...)`, `(EntityProxy*, Comps&...)`, `(Comps&...)`. Never store
  `EntityProxy*` — the entity array may reallocate.
- Components must be default-constructible; `Entt`/`EnttsFromArrays` also move-assign them.
  Non-trivial dtors/moves are wired by `DefineComponent`; do not bypass it.
- The C layer returns `bool`/`NULL`; the wrapper throws `std::runtime_error`. Keep that split.
- System `flags` match with `(entity.flags & flags) == flags`.

## Conventions

- 4 spaces, LF, braces on their own line; no formatter config.
- C: `bitecs_` prefix, snake_case, plain C types, `extern "C"`, C standard headers only in
  `bitecs_core.h`, no exceptions.
- C++: PascalCase types, CamelCase `Registry` methods, internals in `bitecs::impl`, hot helpers
  `_BITECS_INLINE`/`_BITECS_FLATTEN`.
- Public C/C++ headers carry the MIT header; several files are UTF-8 with a BOM — preserve BOM and
  line endings.
- Comments only for non-obvious invariants/TODOs; no narration.
- GCC/Clang builtins, C11 atomics and VLAs are used; MSVC is not supported in practice. Keep
  `-fexceptions` on the core. This is benchmark-driven: no extra allocations/branches in hot paths;
  measure with `bitecs_bench` before/after.

## Adding code

- Core behavior: `bitecs_core.c` (+ `bitecs_core.h` if public), update the wrapper, add tests. Keep
  the C ABI stable unless a break is intended (`BITECS_INDEX_T` is the override).
- High-level helpers go in `bitecs.hpp`/`bitecs_impl.hpp` unless they must be in C.
- New benchmark adapter: `benchmarks/<name>.cpp` implementing `ECS_Interface` from
  `benchmarks/benchmark.hpp` (see `entt.cpp`), `ECS_BENCHMARKS(Name)` in a unique namespace, then add
  the dependency to `conanfile.py` and the `find_package`/link line in `CMakeLists.txt`
  (`benchmarks/*.cpp` is configured-time globbed).

## Known rough edges

- `destroy_batch_noadd` starts at `ptr + 1` and never kills the first entity of a run, so
  `Destroy.Basic` fails and a destroyed index can still deref. Fix before trusting destruction/reuse.
- `bitecs_entt_destroy_batch` never starts its run counter, so it destroys nothing (the wrapper's
  `DestroyBatch` exposes it). Use per-entity destroy until fixed.
- `bitecs_component_define` asserts `typesize > 0`, but the wrapper maps empty trivially-destructible
  types to `typesize == 0` (tags); defining a tag aborts in Debug. Tag support is intended.
- `bitecs_registry_clone_settings` only aliases component lists and returns `false`; background
  loading is unfinished and the aliasing is unsafe.
- `bitecs_system_run_many` and `bitecs_threadpool_*` are empty stubs.
- `bitecs_entt_create` can leak partially created entities if a chunk allocation fails.

Baseline: Release/RelWithDebInfo passes everything except `Destroy.Basic`; Debug aborts earlier in
`Entt.Dtors` on the tag assert. ASan is clean; UBSan reports pre-existing null references for the
empty tag `Component3` (`bitecs_impl.hpp:129, :153, :157`) in `Entt.Dtors`, `Systems.Basic`,
`Merge.Basic`. Treat these as the baseline, not regressions.

## Verification

1. Build Debug with `BITECS_TEST=ON`, run `ctest --output-on-failure`, compare against the baseline.
2. Fix new warnings (`-Wall -Wextra -pedantic` on the core).
3. For hot-path changes, run the relevant Release `bitecs_bench` filter and report the delta.
4. Keep public C headers C-compatible and the wrapper C++17-compatible.
