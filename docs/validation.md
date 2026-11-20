# Standalone foundation validation

Validation date: 2026-11-20. This corrective pass follows
`28b2f8d7fd5fdf2b42677bd1e64f0476ec6f6162` in Draft PR #1. The PR records the
published corrective HEAD and CI runs at that exact revision. These results
supersede the earlier dependency audit.

## Toolchains and package checks

The full C++17 configuration enables JSON, XML, native HTTP, examples and the
opt-in live smoke. Debug and Release use active checks, independent of NDEBUG.

| Toolchain | Release full CTest | Debug full CTest |
| --- | --- | --- |
| MinGW GCC 15.2 | 10/10, 131.40 s | 10/10, 107.24 s |
| MSVC 19.44 | 10/10, 77.45 s | 10/10, 66.11 s |
| Linux GCC 15.2, Ubuntu/WSL | 10/10, 292.01 s | 10/10, 192.03 s |

Each full suite contains ten tests: live providers, Core, multi-TU linkage,
embedded host, parser fixtures, native HTTP, manual HTTP processing, persistent
refresh example, installed Core, and installed optional components. Live HTTPS
is opt-in; mandatory CI runs the other nine. All 19 public headers also compile
individually as the first and only include of their translation units.

Builds use RateWeaver's pinned submodules, including source-built curl, without
consumer-repository include/library paths. Separate executable checks cover:

- Embedded host: real TimeShield, JSON, pugixml and HTTP targets exist before
  RateWeaver is added. Discovery is then disabled; target reuse and absence of
  new duplicate targets are checked. JSON/XML parsing and native client lifecycle
  code are linked and run.
- Installed package: RateWeaver and dependencies occupy separate, relocated
  prefixes. Registries are disabled and source paths in exports are rejected.
  Core runs with JSON/XML/kurlyk/CURL discovery disabled. Missing mandatory
  TimeShield fails configuration. The optional consumer parses feeds and links
  native HTTP using installed packages.
- Fresh first configuration: a new core-only MinGW Debug tree passed 5/5,
  including separate-prefix Core, missing-TimeShield rejection and host reuse.
  Dependency build directories are captured without relying on install scripts
  that do not exist until generation.

Core has 217 active checks and the full parser fixture executable has 111.
They cover atomic snapshots, canonical reciprocals and explicit reverse quotes,
source/quorum/outlier policy, final NoConsensus refusal, bounded LastKnown,
arithmetic limits, identifier grammar, direct-pair diagnostics and time semantics.

An initial Linux Debug host fixture exceeded its former 120-second budget while
installing dependencies and compiling the independent consumer on a WSL-mounted
drive alongside sanitizer builds. The three package/build fixtures now allow
300 seconds; native HTTP/manual liveness timeouts are unchanged. Its interrupted
link left an invalid scratch executable, which was removed and linked again.
The matrix reports completed runs rather than counting the timeout as passing.

## Dependency graph and reproduction

CMake 3.21+ and C++17 are required. Exact tags and gitlinks are in the
[dependency registry](dependencies.md). Core requires Time Shield 2.0 and
platform Threads; JSON, XML and native HTTP remain optional. Time Shield 1.x
is unsupported; old header paths are not probed as a fallback.

Standalone defaults to `RATEWEAVER_DEPENDENCY_MODE=BUNDLED`; embedded builds
default to `EXISTING`. Compatible existing targets take precedence in both.
The separately named `RateWeaverKurlykHttp::client` owns HTTP-only kurlyk,
CURL and Threads. It does not replace upstream's full `kurlyk::kurlyk` target
or configure unused WebSocket dependencies.

```sh
git submodule update --init
cmake -S . -B tmp/agent-work/release -DCMAKE_BUILD_TYPE=Release \
  -DRATEWEAVER_DEPENDENCY_MODE=BUNDLED -DRATEWEAVER_CURL_MODE=BUNDLED \
  -DRATEWEAVER_BUILD_EXAMPLES=ON -DRATEWEAVER_BUILD_PROVIDERS=ON \
  -DRATEWEAVER_ENABLE_ECB=ON -DRATEWEAVER_ENABLE_KURLYK=ON
cmake --build tmp/agent-work/release
ctest --test-dir tmp/agent-work/release -C Release --output-on-failure
```

Windows routine validation adds `-G "MinGW Makefiles"`; the MSVC profile uses
`-G "Visual Studio 17 2022" -A x64` and `--config Debug` or `Release`.
Linux uses Unix Makefiles and system OpenSSL development packages. Windows
source-built curl uses Schannel. The pinned `curl-8_23_0` source header reports
`8.23.0-DEV`; this is the upstream string at the documented gitlink, not an
alternative dependency revision. Vendor files are unchanged.

## Time and API corrections

`ts_ms_t` aliases Time Shield's type. UTC samples use `time_shield::ts_ms()`;
refresh intervals use `time_shield::monotonic_ms()`. Tests jump UTC forward and
backward while cadence remains controlled by monotonic time. Final callbacks
sample UTC, so delayed polling does not change retrieval time. Missing/throwing
clocks, cancellation and reentry from both injected clocks are covered.

`Quote::effective_at` is an optional eighth constructor argument; existing
positions retain their meaning. Daily CDN/ECB dates and CBR applicability
populate it instead of inventing publication instants. Future applicability is
rejected for observed, synthetic and LastKnown rates even without an age limit
or with publication skew allowed. Freshness selects publication, then effective,
then retrieval time. Calendar conversion delegates to Time Shield 2.0 while
preserving feed syntax. Fixtures include pre-epoch dates, leap years, offsets,
milliseconds and malformed/signed-year input.

`RateRouter::diagnose_pair()` returns the existing policy report on demand;
ordinary conversion does not build a route-wide failure tree. Currency identifiers
follow the documented ASCII grammar and length bound. Stricter identifiers and
corrected daily timestamp meaning can change caller behavior. Adding a Quote
member changes its layout: rebuild consumers rather than mixing header revisions.
The fifth refresh clock remains UTC; deterministic cadence tests also inject the
new sixth clock. Binary double and accounting rounding limits remain explicit in
the [integration contract](integration.md).

## Native HTTP and lifecycle

Tests use kurlyk v1.1.1, actual libcurl and an ephemeral Python loopback server:

- JSON/XML, chunked body parsed once, ETag/304 metadata retention;
- final HTTP errors, native retries, timeout, malformed/empty/partial feeds,
  streamed overflow and preservation of the previous book on failure;
- Busy, cancel followed by late completion, terminal stop, reentrant parsing and
  clocks, and publication fences;
- client/book/parser/controller lifetimes, completion timestamps, manual
  processing, host deinit and peer EOF after native abort;
- persistent owned/shared native limits, immediate admission counts, timed waits,
  gated outstanding counts and final callbacks before cancellation returns;
- two refreshes through the example's single persistent client, including 304.

Group regressions agree with v1.1.1's continuous outstanding counter; the old
pending-to-active diagnosis is superseded. Buffering before the void streaming
callback and waiting client cancellation remain source-confirmed constraints.
Accepted-body rejection is not a receive-memory ceiling. Worker-callback
cancellation recursion is unsupported here, not a newly reproduced deadlock.
See the [kurlyk audit and separate upstream proposal](kurlyk-limitations.md).

## Sanitizers and live HTTPS

ASan+UBSan passed 4/4 focused tests in 5.09 s; TSan passed 4/4 in 4.83 s,
with no findings. Both runs cover Core, full parser fixtures, native HTTP and
manual processing, with leak detection/halt-on-error where applicable.
Instrumentation uses `-O1 -g -DNDEBUG`, frame pointers and sanitizer flags for C
and C++, covering bundled curl/pugixml and selected test executables. System
OpenSSL and installed-consumer subprocess builds are outside instrumentation.
Clean executed paths do not prove general race freedom, arbitrary concurrent
teardown or peak-memory bounds.

The opt-in live executable reports HTTP/refresh status, attempts, effective and
retrieval times, revision, amount and provenance for CDN, CBR and ECB. Numeric
market rates are not asserted. Current valuation rejects future-effective CBR
data; separately labelled feed-time arithmetic is not historical lookup or
permission to use future data for current money.

The Windows live run returned HTTP 200 and Updated for all three feeds, each
at revision 1, with Observed/Normal direct and reciprocal contributors:

| Feed | Quotes | Effective time, Unix ms | Publication time |
| --- | --- | --- | --- |
| CDN USD | 339 | 1795132800000 | Not supplied |
| CBR redistribution | 54 | 1795163400000 | Not supplied |
| ECB | 29 | 1795046400000 | Not supplied |

Configure `RATEWEAVER_BUILD_LIVE_SMOKE=ON` and, where needed,
`RATEWEAVER_LIVE_CA_FILE=/path/to/cacert.pem`. Exit 77 reports endpoint/network
unavailability as SKIP; TLS trust/verification errors fail. Live endpoints are
excluded from mandatory CI.

## Documentation boundary and remaining limits

Public docs now describe generic dependency ownership, installation, host
lifecycle, API and provider limitations. The internal integration plan was
removed from the current tree and preserved in a private local handoff. Deletion
does not erase published Git history. No consumer repository or vendor source
was modified by this pass.

Core and parsers are independently consumable. Native HTTP remains a foundation
for integration evaluation; upstream body-cap and nonwaiting targeted cancellation
work stays separate. There is no production resource-bound guarantee, historical
store, decimal money type or accounting rounding policy. PR #1 stays Draft;
no merge or release is part of validation.
