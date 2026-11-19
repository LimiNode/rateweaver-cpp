# Foundation validation

Validation date: 2026-11-20. Baseline:
`848bad90818a1d0f8625ab76d1db1feb08876b9c`. The exact resulting revision and
review link are recorded in the Draft PR; no release tag is created.

## Core and packaging

Release builds passed with MinGW GCC 15.2.0, MSVC 19.44.35228 and Linux GCC
15.2.0 in Ubuntu/WSL. The complete JSON/ECB configuration passes seven CTest
entries: core regressions, multiple-translation-unit linkage, runtime edges,
embedded superproject, provider/runtime regressions, relocated installed core
consumer and relocated installed component consumer. All 23 public headers
compile as the only include in their own C++17 translation units.

Core-only validation also passed without enabling parser dependencies. A
separate MinGW configuration enabled ECB while disabling JSON package discovery,
including an installed ECB component consumer, to check optional isolation.

`rateweaver_core_tests` contains 146 Release-active checks across currency,
publication, graphs, aggregation, timestamp eligibility, LastKnown, routing and
concurrent access. The concurrency case uses two writers and three readers,
checks complete two-quote source batches, reader revision monotonicity, exactly
400 successful writer revisions and snapshot ownership. It is correctness
evidence for those exercised paths, not a throughput or lock-free claim.

The installed consumer is copied to a separate scratch source directory. The
install prefix is relocated before configure; package registries and optional
dependency discovery are disabled. It checks conversion/provenance and
unavailable-versus-zero semantics through `find_package`. The additional
OPTIONAL_COMPONENTS regression is enabled only by the test runner, keeping the
same example usable as a normal independent consumer.

Representative commands (use a task-specific build directory):

```sh
cmake -S . -B tmp/agent-work/release -DCMAKE_BUILD_TYPE=Release \
  -DRATEWEAVER_BUILD_TESTS=ON -DRATEWEAVER_BUILD_EXAMPLES=ON
cmake --build tmp/agent-work/release
ctest --test-dir tmp/agent-work/release -C Release --output-on-failure
```

Windows routine validation selects `MinGW Makefiles`; the additional MSVC
configuration selects `Visual Studio 17 2022` with `-A x64` and builds with
`--config Release`. Linux selects `Unix Makefiles`. Separate trees are used for
each compiler and optional dependency binaries.

## Providers and integration

The full provider/runtime executable has 132 Release-active checks and the
runtime edge executable has 23. They exercise all three feed fixtures,
malformed/empty/partial responses, duplicate JSON members and normalized pairs,
CBR nominal scaling/UTC offset, calendar validation, source/retrieval times,
transport errors/timeouts, response caps, ETag, cancellation, concurrent overlap,
reentrant stop, rejected publication and completion-clock exceptions. Quotes
and revisions are retained on failure. Retrieval time is measured after the
transport returns, while `304` changes neither timestamps nor revision.

Parser dependencies used for validation were MGC's existing nlohmann/json
`55f93686c01528224f448c19128836e7df245f72` (3.12.0) and optional pugixml 1.15
`ee86beb30e4973f5feffe3ce63bfa4fbadf72f38`. Dependency binaries were built
separately for MinGW, MSVC and Linux under ignored task scratch directories.

Live network interoperability and a concrete host `kurlyk` transport bridge are
not established by fixture tests. There is no historical quote store,
accounting rounding policy or MGC integration in this change.

The [MGC audit](mgc-integration.md) identifies the exact consumer revision and
two current acceptance blockers: unknown currency is classified as Native, and
the consumer smoke test uses Release-disabled `assert`. MGC source and
submodule pins remain unchanged by the RateWeaver work.
