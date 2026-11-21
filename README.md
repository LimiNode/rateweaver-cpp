# RateWeaver

RateWeaver is a C++17 header-only library for currency conversion using
attributed exchange-rate observations. Core storage, immutable graphs,
aggregation, quote policy, routing and optional feed adapters have separate
responsibilities. These are valuation inputs, not broker tick streams or
execution prices.

This is a pre-release foundation. See the [implementation inventory](docs/implementation-inventory.md),
[integration guide](docs/integration.md), [provider lifecycle](docs/providers.md)
and [validation report](docs/validation.md) for contracts and observed checks.

## Conversion

```cpp
#include <rateweaver.hpp>

// Fictional offline observation, not a current market rate.
rateweaver::Quote quote;
quote.from = "EUR";
quote.to = "USD";
quote.rate = 1.25; // 1 EUR = 1.25 USD.
quote.source_id = "offline-demonstration";
quote.origin = rateweaver::QuoteOrigin::Observed;
quote.published_at = 1700000000000LL;
quote.effective_at = 1700000000000LL;
quote.retrieved_at = 1700000000100LL;
const rateweaver::QuoteGraph graph(std::vector<rateweaver::Quote>{quote});
rateweaver::RouterOptions options;
options.aggregation.reference_time = 1700000000200LL;
options.aggregation.max_age_ms = 1000; // Illustrative fixture window: one second.
const auto result = rateweaver::RateRouter(graph, options).convert(10.0, "EUR", "USD");
if (result) {
    // *result.amount == 12.5; result.route owns the contributing quote evidence.
} else {
    // Report result.status. No converted amount is available.
}
```

The [complete example](examples/convert.cpp) explains all fields and the
fixture's clock choices. A daily feed needs its own freshness policy; the
one-second window above is not a production recommendation.

Rates must be finite and positive. Finite amounts may be zero or negative;
unavailable conversion differs from a valid zero. Known identical currencies
need no quote. Normalization trims surrounding ASCII whitespace (space and
tab through carriage return) and uppercases ASCII letters. Other control bytes
are preserved and rejected by validation. Canonical identifiers match
`[A-Z0-9][A-Z0-9_.:-]{0,31}`; this is not an ISO whitelist or alias resolution.
`USD` and `USDT` are different identifiers.

`QuoteBook::replace_source()` atomically replaces a complete source batch.
Rejected batches preserve data and revision. An explicit empty batch removes a
source; provider errors must not become empty success. Owned snapshots and
graphs remain valid after updates. Core owns the canonical `QuoteGraph` and
derives reciprocals unless that source supplies an explicit reverse quote.

Routes are deterministic shortest acceptable simple paths with lexicographic
ties, hop and search-work bounds. They do not maximize the numerical rate.
Median, mean and priority aggregation retain source evidence. Distinct IDs
prevent duplicate voting but do not establish independent upstream sources.
`NoConsensus` is a final refusal; LastKnown cannot add stale votes to repair it.
Synthetic assumptions are opt-in and never enter an observed-rate median.

## Time, freshness and precision

Core requires Time Shield 2.0. `ts_ms_t` aliases `time_shield::ts_ms_t`;
`current_time_ms()` uses UTC `ts_ms()`, and refresh cadence uses
`monotonic_time_ms()`/`monotonic_ms()`. Wall-clock adjustments do not schedule
refreshes. Explicit injected clocks keep tests deterministic.

Publication time, effective time and successful retrieval time are separate.
The daily adapters put source calendar dates in `effective_at`; they do not
claim a precise publication instant. Freshness uses `published_at`, otherwise
`effective_at`, otherwise `retrieved_at`. Future effective rates are unavailable
for current valuation, including explicit synthetic assumptions, even with
allowed publication-clock skew or a disabled age limit. An age limit
rejects observed quotes with no timestamp. Leaving `max_age_ms` unset disables
that limit; financial consumers must choose it explicitly.

`Observed` means source provenance, not accuracy or freshness. A bounded opt-in
`LastKnownFallback` retains that origin. `Synthetic` marks an explicit assumption;
no parity is invented. Inspect route contributions before presenting cached or
synthetic valuations. `RateRouter::diagnose_pair()` exposes direct-pair policy
status and exclusions without expanding every conversion result.

The current book has no historical lookup. `reference_time` checks supplied
quotes; today's daily rate cannot establish a past trade's valuation. Missing
historical evidence means unavailable. Convert separately known currency
components before aggregating. Binary `double` is not decimal money: the host
owns accounting precision, minor-unit and rounding rules.

## Build and install

Use CMake 3.21 or newer and a C++17 compiler.

Dependencies are pinned; see [the dependency registry](docs/dependencies.md).
Core needs only the Time Shield submodule. Use its recorded commit, never
`git submodule update --remote`:

```sh
git submodule update --init external/time-shield-cpp
cmake -S . -B build-mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DRATEWEAVER_DEPENDENCY_MODE=BUNDLED -DRATEWEAVER_BUILD_EXAMPLES=ON
cmake --build build-mingw
ctest --test-dir build-mingw -C Release --output-on-failure
cmake --install build-mingw --prefix /path/to/rateweaver-prefix
```

Linux can omit the generator or use `Unix Makefiles`. MSVC uses
`-G "Visual Studio 17 2022" -A x64` and `--config Release` for build/install.
Core needs Time Shield and platform threads; it needs no HTTP/JSON/XML runtime.
Bundled mode is the top-level default; existing-dependency mode is the embedded
default. Existing compatible targets always take precedence. Neither mode
starts an HTTP runtime. The [integration guide](docs/integration.md) documents
embedded and installed use.

```cmake
find_package(rateweaver 0.1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE rateweaver::rateweaver)
```

Optional features remain explicit:

| Option | Default | Purpose |
| --- | --- | --- |
| `RATEWEAVER_DEPENDENCY_MODE` | `BUNDLED` standalone; `EXISTING` embedded | Pinned submodules or host targets/packages |
| `RATEWEAVER_CURL_MODE` | `AUTO` | Host/system CURL, then pinned MinGW binary or source fallback; explicit `SYSTEM`, `PREBUILT`, `SOURCE` modes |
| `RATEWEAVER_BUILD_TESTS` | On standalone; off embedded | Release-active offline and package/consumer regressions |
| `RATEWEAVER_BUILD_EXAMPLES` | Off | Conversion and, when enabled, provider-refresh example |
| `RATEWEAVER_BUILD_PROVIDERS` | Off | CDN/CBR JSON adapters; `rateweaver::providers` |
| `RATEWEAVER_ENABLE_ECB` | Off | ECB XML adapter; `rateweaver::ecb` |
| `RATEWEAVER_ENABLE_KURLYK` | Off | Native async refresh; `rateweaver::kurlyk` |
| `RATEWEAVER_BUILD_LIVE_SMOKE` | Off | Diagnostic HTTPS smoke against public feeds |
| `RATEWEAVER_INSTALL` | On | Headers and exported CMake package |

Enable JSON, ECB and kurlyk options for the complete provider configuration.
For MinGW x64, initialize `external/kurlyk`, `external/json` and
`external/pugixml`, and select `RATEWEAVER_CURL_MODE=PREBUILT` to use the pinned
curl 8.22.0 DLL without initializing or compiling `external/curl`. `AUTO`
prefers a compatible host target or installed curl before that fallback.
`SOURCE` explicitly uses the curl source submodule; the old curl mode
`BUNDLED` is a deprecated alias for `SOURCE`. This is separate from
`RATEWEAVER_DEPENDENCY_MODE=BUNDLED`.

The prebuilt package uses LibreSSL and has known upstream advisories; see
[version and security limits](docs/dependencies.md#curl-version-and-security-limit).
With kurlyk v1.1.1, configure a trusted CA file through `HttpClient::set_ca_file()`
or deploy `curl-ca-bundle.crt` beside the executable. The pinned package does
not include that file. Installed consumers can call
`rateweaver_copy_runtime_dlls(my_app)` to copy imported runtime DLLs beside
their executable; hosts retain deployment ownership.

The [persistent-client example](examples/provider_refresh.cpp) submits repeated
async updates through one client. The separate [live smoke](examples/live_providers.cpp)
reports provider availability and provenance; mandatory tests use loopback HTTP
and offline fixtures.

kurlyk v1.1.1 fixes the old group-count handoff defect. Its body is still buffered
before streaming notification, and native cancellation still waits. Consequently
`max_body_bytes` is an acceptance limit, not a hard receive-memory ceiling. See
the [version-specific audit](docs/kurlyk-limitations.md) before relying on stronger
resource/cancellation guarantees.

Develop in a dedicated branch/worktree under `.worktree/<task-name>/`; temporary
artifacts belong in `tmp/agent-work/`. Follow [AGENTS.md](AGENTS.md).
