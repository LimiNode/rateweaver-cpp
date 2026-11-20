# Native asynchronous foundation validation

Validation date: 2026-11-20. This corrective pass starts at
`282a6b3b3d580e3e05de4318aafe043cb3c5df4b` in Draft PR #1. The resulting exact
revision and its CI run are recorded in that PR. No release tag or MGC change
is included. Results below are from the corrected source, not the starting HEAD.

## Release matrix and package checks

| Toolchain | Full CTest (including opt-in live) | Native owned limiter, 3 refreshes / 500 ms period |
| --- | --- | --- |
| MinGW GCC 15.2.0 | 9/9 passed, 47.32 s | 1063 ms |
| MSVC 19.44.35228 | 9/9 passed, 40.32 s | 1066 ms |
| Linux GCC 15.2.0 (Ubuntu/WSL) | 9/9 passed, 77.22 s | 1005 ms |

The full configuration builds JSON, ECB, native kurlyk and live examples in
Release. All 19 public headers compile individually as the first/only include
of their translation units. Multi-TU linkage, standalone and embedded builds,
relocated installed core and optional JSON/XML/kurlyk consumers pass. The core
consumer disables JSON, XML, kurlyk and CURL discovery and remains usable.
An additional MinGW ECB-only profile passes 6/6 with JSON/HTTP discovery disabled.

The optional installed consumer preserves external dependency prefixes, including
transitive CURL dependencies from vcpkg. A local reproducer with two prefixes
(one containing a space) failed dependency discovery before the correction and
passed afterward. The prefix list crosses the subprocess boundary in an initial
cache file; the independent core consumer does not inherit those dependencies.

Core has 168 Release-active checks; parser fixtures have 66. The native loopback
suite's check count varies with its control-poll scheduling; manual kurlyk
processing has 7. No test relies on NDEBUG-disabled
assertions. The core reader/writer regression checks atomic two-quote batches,
revision monotonicity, 400 successful publications and snapshot ownership.

The NoConsensus regression uses conflicting fresh quotes plus an additional
stale vote. It fails against the previous policy and passes with the correction.
LastKnown also retains disabled-source, quorum, outlier, future-time and maximum
age rejection, and contributor origin/usage. QuoteGraph has one canonical Core
implementation, unconditional reciprocals and explicit reverse preference;
the old routing graph, GraphBuildOptions and forwarding policy/graph headers
have been removed.

## Actual HTTP, cancellation and limiter checks

Tests use the exact MGC kurlyk pin
`fe2698dec259a7e417883e1f41985ed21ec28b55`, real libcurl and a Python standard
library HTTP server on an ephemeral loopback port. No fake transport or public
network is needed. Covered behavior:

- CDN/CBR JSON and ECB XML; successful chunked transfer parsed once;
- ETag/304 retention, unsolicited 304, final 404/500/503, malformed JSON/XML,
  empty body, empty feed, rejected partial feed and explicit partial acceptance;
- native retry with intermediate non-final responses, timeout and streamed
  overflow; old quotes/revision remain intact on failure;
- same-source Busy, multiple providers publishing into one book, cancellation
  followed by late successful response, terminal stop and reentrant parsing;
- parser/controller/book destruction, native client destruction with pending
  work, completion-clock failure, and host deinit with an active request;
- actual TCP peer EOF after native cancellation/timeout/client destruction;
- a single owned client limiter persists across three refreshes; a shared
  sequential handle prevents the second client's arrival until the first gate
  is released (the server rejects incorrect ordering);
- init(false), manual process/poll and callback drain before client destruction,
  with no background worker required by RateWeaver.

The completion timestamp test signals from the actual final callback's clock
sample, advances the injected clock, then polls: retrieval time stays at HTTP
completion. It deliberately does not treat the pinned native group wait as a
reliable drain assertion.

These results do not establish a hard receive-memory ceiling or upstream race
freedom. The pinned client's body buffering and group accounting defects are
recorded with a separate change proposal in [the kurlyk audit](kurlyk-limitations.md).
Cancel/stop fence publication; actual native I/O cancellation still belongs to
the host client and can wait for callbacks. No blocking HTTP adapter was added.

## Live HTTPS

With an explicit trusted CA bundle, all three endpoints returned HTTP 200 and
Updated on Windows and Linux. Each independent book reached revision 1:

| Source | Quotes | Source timestamp (Unix ms) | Direct/reciprocal check |
| --- | --- | --- | --- |
| CDN | 339 | 1795046400000 | USD/EUR, observed contributors, Normal usage |
| CBR redistribution | 54 | 1795163400000 | USD/RUB at the explicitly labelled feed timestamp |
| ECB | 29 | 1795046400000 | EUR/USD, observed contributors, Normal usage |

The executable prints HTTP/refresh status, attempts, source/retrieval timestamps,
revision, converted amount and each contributor's origin/usage/reversal. Numeric
rates are not asserted. CBR announced a future effective timestamp during this
run: current valuation was correctly unavailable, then routing arithmetic was
checked separately at the feed timestamp. That is not authorization to value
current amounts with future observations.

Live CTest is opt-in and excluded from mandatory CI. Exit 77 means endpoint or
network unavailability and is displayed as SKIP. TLS trust-file/verification
errors fail explicitly; a missing CA bundle is not reported as a successful
network check. Configure RATEWEAVER_LIVE_CA_FILE or pass a bundle path directly
to rateweaver_live_providers (Linux system bundle: /etc/ssl/certs/ca-certificates.crt).

## Reproduction and dependencies

Core: CMake 3.20+, C++17, platform Threads. Parser dependencies used here are
MGC's nlohmann/json `55f93686c01528224f448c19128836e7df245f72` (3.12.0) and
pugixml 1.15 `ee86beb30e4973f5feffe3ce63bfa4fbadf72f38`. Windows native HTTP
uses MGC's existing shared curl 8.11.0; Linux uses its system libcurl 8.18.
No dependency is copied into RateWeaver or required by core-only consumers.

```sh
cmake -S . -B tmp/agent-work/release -DCMAKE_BUILD_TYPE=Release \
  -DRATEWEAVER_BUILD_PROVIDERS=ON -DRATEWEAVER_ENABLE_ECB=ON \
  -DRATEWEAVER_ENABLE_KURLYK=ON \
  -DRATEWEAVER_KURLYK_INCLUDE_DIR=/path/to/pinned/kurlyk/include
cmake --build tmp/agent-work/release
ctest --test-dir tmp/agent-work/release -C Release --output-on-failure
```

Supply parser packages and CURL::libcurl (or the host's existing kurlyk target).
Windows routine builds use MinGW Makefiles; additional MSVC uses Visual Studio
17 2022. Linux uses Unix Makefiles. Builds use separate ignored task trees.

## Sanitizers and consumer readiness

Current native/manual tests pass under ASan+UBSan (2/2, 4.31 s) and TSan
(2/2, 4.11 s), with leak detection and halt-on-error enabled. The final empty
clock-exception regression is included. No sanitizer findings were reported.
The broader ASan+UBSan suite passed 8/8 and TSan core/native/manual passed 3/3;
the affected native cases were rebuilt and rerun after the final correction.
Compilation used -O1 -g -DNDEBUG, the corresponding -fsanitize flags and frame
pointers. Instrumentation covers test executables and the library/dependency
headers compiled into them; system libcurl and installed-consumer sub-builds
were not instrumented. These executed paths do not prove upstream race freedom
or repair the pinned dependency's documented synchronization defect.

Core and parsing are independently consumable. Native HTTP is suitable for
integration evaluation; production MGC acceptance remains gated on the kurlyk
fix/review and the [MGC consumer findings](mgc-integration.md). MGC PR #63 remains
unchanged: unknown aggregate currency must not become Native, and its smoke
checks must be active in Release. No historical quote store or accounting
rounding policy is implemented by this library.
