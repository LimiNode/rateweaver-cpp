# RateWeaver

RateWeaver is a C++17 header-only library for currency conversion using attributed
exchange-rate observations. Quote storage, immutable graphs, aggregation, quote
policies, routing and optional feed adapters have separate responsibilities.
These rates are valuation inputs, not broker tick streams or execution prices.

This is a pre-release foundation. See the [implementation inventory](docs/implementation-inventory.md)
for the recovered baseline and limitations, the [provider guide](docs/providers.md)
for refresh ownership, and the [MGC integration proposal](docs/mgc-integration.md)
for consumer constraints.

## Core use

```cpp
#include <rateweaver.hpp>

rateweaver::QuoteBook book;
const auto publication = book.replace_source("example", {
    {"EUR", "USD", 1.25, "example", rateweaver::QuoteOrigin::Observed,
     1700000000000LL, 1700000000100LL}
});
if (publication != rateweaver::QuotePublishStatus::Success) {
    // Report the rejected update; the previous source snapshot is unchanged.
}
const rateweaver::QuoteGraph graph(book.snapshot());
rateweaver::RouterOptions options;
options.aggregation.reference_time = 1700000000200LL;
options.aggregation.max_age_ms = 1000;
const auto result = rateweaver::RateRouter(graph, options).convert(-8, "EUR", "USD");
if (result) {
    // *result.amount == -10; result.route owns the contributing quote evidence.
} else {
    // result.status describes failure; result.amount is empty.
}
```

A directed quote means `1 from = rate to`. Rates must be finite and positive;
finite amounts may be zero or negative. A valid zero result is distinct from
unavailable conversion. Same-currency conversion needs a known, valid currency
identifier and no quote. Currency normalization trims surrounding ASCII
whitespace/control characters and uppercases ASCII letters. Validation is not
an ISO currency whitelist and does not equate `USD` with `USDT`.

`QuoteBook` publishes complete source batches atomically. Rejected batches leave
data and revision unchanged. An explicit empty batch removes a source; errors
must never become empty updates. Snapshots own their data and graphs remain
independent of later publications. Graphs retain independent source candidates
and derive reciprocals only when no explicit reverse quote exists for a source.

Routing chooses shortest acceptable simple paths with lexicographic ties and
hop/search limits. It does not optimize the numerical rate. Median, mean and
priority aggregation preserve source evidence. Synthetic assumptions are opt-in
and never enter an observed-rate median. No parity is invented by the library.

## Time and freshness

`ts_ms_t` is a signed 64-bit Unix timestamp in milliseconds. Core uses
`std::chrono`; `time-shield-cpp` is not required. Supply `reference_time` for
reproducible valuations. `published_at` belongs to the source data and
`retrieved_at` to retrieval. Aggregation uses publication time when available,
otherwise retrieval time; downloading old data does not make it fresh.

Configure `max_age_ms` for financial use. Leaving it unset explicitly disables
the age limit. Future timestamps are subject to the clock-skew bound. LastKnown
is a separate, bounded opt-in fallback; origin remains Observed when reused.
Consumers must inspect contributing quote origins and usage before presenting
cached or synthetic valuations.

The current quote book is not historical storage. Today's daily rate cannot
establish a past trade's valuation. If appropriate historical quotes are absent,
the host must report the conversion unavailable.

## CMake package

Core needs CMake 3.20+, a C++17 toolchain and platform thread support. Configuration does not
download dependencies. For a standalone Release build:

```sh
cmake -S . -B tmp/agent-work/release -DCMAKE_BUILD_TYPE=Release
cmake --build tmp/agent-work/release
ctest --test-dir tmp/agent-work/release -C Release --output-on-failure
cmake --install tmp/agent-work/release --prefix /path/to/prefix
```

On Windows use `-G "MinGW Makefiles"` with `gcc`/`g++`. Additional MSVC validation
uses `-G "Visual Studio 17 2022" -A x64` and `--config Release` when building.

```cmake
find_package(rateweaver 0.1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE rateweaver::rateweaver)
```

`add_subdirectory(path/to/rateweaver-cpp)` exposes the same target. Existing
namespaced dependency targets are reused. Tests default to off when embedded.
The independent [installed consumer](tests/install_consumer) uses only
`find_package` and installed headers. CTest installs and relocates the prefix,
then builds that consumer with optional dependency discovery disabled.

| Option | Default | Purpose |
| --- | --- | --- |
| `RATEWEAVER_BUILD_TESTS` | On standalone; off embedded | Offline tests, public-header compilation, package/consumer checks |
| `RATEWEAVER_BUILD_EXAMPLES` | Off | Offline conversion example |
| `RATEWEAVER_BUILD_PROVIDERS` | Off | CDN/CBR JSON adapters; `rateweaver::providers` |
| `RATEWEAVER_ENABLE_ECB` | Off | ECB XML adapter; `rateweaver::ecb` |
| `RATEWEAVER_ENABLE_KURLYK` | Off | Native asynchronous refresh; `rateweaver::kurlyk` |
| `RATEWEAVER_BUILD_LIVE_SMOKE` | Off | Opt-in HTTPS smoke against the three public feeds |
| `RATEWEAVER_INSTALL` | On | Headers and exported CMake package |

Optional installed targets require components:

```cmake
find_package(rateweaver CONFIG REQUIRED COMPONENTS providers ecb)
target_link_libraries(my_app PRIVATE rateweaver::providers rateweaver::ecb)
```

## Providers and refresh

CDN fiat and CBR JSON parsing use `nlohmann/json >= 3.11`; ECB uses the official
daily XML feed and `pugixml >= 1.13`. Provide CMake packages or existing
namespaced targets. Core-only consumers need neither dependency. The core
umbrella excludes optional provider dependencies.

The optional `KurlykRefresh` uses a dedicated, long-lived `kurlyk::HttpClient`.
The host configures native timeouts, retries and owned/shared rate limits once.
`start()` submits asynchronously; `poll()` parses a ready result and publishes
on the host thread. RateWeaver creates no worker, scheduler or global runtime.
MGC reuses its existing kurlyk runtime.

The audited MGC kurlyk pin buffers bodies before streaming callbacks, exposes
client cancellation as a waiting barrier, and has group-accounting concurrency
defects. RateWeaver fences obsolete publication immediately, but a hard receive
memory cap, nonwaiting I/O cancellation and safe native group accounting require
a separate kurlyk change.
See the [exact-pin audit](docs/kurlyk-limitations.md) before deployment.

Parsing and publication are separate. Empty, malformed, cancelled or failed
responses retain the last valid source snapshot. Partial data are diagnosed
explicitly. Offline fixtures cover all three formats and refresh error paths;
network connectivity is not a test prerequisite. Details and usage are in the
[provider guide](docs/providers.md).

## Development and MGC integration

Use a dedicated branch and worktree under `.worktree/<task-name>/`; temporary
artifacts belong in `tmp/agent-work/`. Follow [AGENTS.md](AGENTS.md).

MGC integration is a separate review step after package/provider acceptance. It
must pin a reviewed commit, reuse shared dependencies and introduce an explicit
adapter. Unknown source currency, mixed-currency aggregates and absent historical
rates remain unavailable; they cannot be displayed as valid native amounts.
