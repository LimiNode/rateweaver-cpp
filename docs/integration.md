# Embedding and conversion contract

RateWeaver supplies attributed currency valuation rates. Daily reference feeds
are not broker tick streams, execution prices or a historical rate database.

## Build and dependency ownership

Link `rateweaver::rateweaver` for Core, Aggregation and Routing. Core requires
Time Shield 2.0 and platform thread support, but no HTTP, JSON or XML library.
Optional components are `rateweaver::providers` (CDN/CBR JSON),
`rateweaver::ecb` (ECB XML) and `rateweaver::kurlyk` (native async refresh).
See [dependencies](dependencies.md) for pins and dependency selection.

An embedding project supplies shared dependency targets before
`add_subdirectory(path/to/rateweaver-cpp)` and selects existing dependencies.
RateWeaver reuses those targets and does not initialize a second HTTP runtime
or change the host's global feature settings. Tests default off when embedded.
Installed consumers discover dependencies independently:

```cmake
find_package(TimeShield 2.0 CONFIG REQUIRED)
find_package(rateweaver 0.1 CONFIG REQUIRED COMPONENTS providers ecb kurlyk)
target_link_libraries(my_application PRIVATE
    rateweaver::providers rateweaver::ecb rateweaver::kurlyk)
```

Install RateWeaver and dependencies into separate prefixes; supply their
prefixes through `CMAKE_PREFIX_PATH`. Installed consumers need no RateWeaver
source checkout. Omit optional components for offline Core use.

For MinGW x64, `RATEWEAVER_CURL_MODE=AUTO` tries an installed curl before
the pinned prebuilt fallback. `PREBUILT` selects that binary directly; `SOURCE`
selects the source submodule. An existing `CURL::libcurl` wins in every mode.
Hosts supplying `kurlyk::kurlyk` also retain its feature definitions and
dependency graph. RateWeaver does not fetch a second curl, change client
timeouts/retries/limits, or copy/install host-owned curl artifacts.

### Shared curl deployment and TLS trust

The MinGW binary fallback uses `libcurl-x64.dll` and its matching import
library and headers. Standalone RateWeaver HTTP tests and examples copy the
imported runtime DLL beside their executables automatically. Consumers own
their executable directory and opt in to the exported CMake helper:

```cmake
find_package(rateweaver CONFIG REQUIRED COMPONENTS kurlyk)
add_executable(my_application main.cpp)
target_link_libraries(my_application PRIVATE rateweaver::kurlyk)
rateweaver_copy_runtime_dlls(my_application)
```

The helper uses CMake's `TARGET_RUNTIME_DLLS`; on non-Windows platforms it does
nothing. It copies DLLs described by imported shared targets, not arbitrary
toolchain runtime dependencies. The host still owns its compiler runtime
deployment and final application installation. Embedded builds do not
automatically deploy host dependencies.

Installing the owned prebuilt dependency includes its DLL, import library,
headers, licenses and a relocatable `CURLConfig.cmake`. The `RateWeaver`
component contains only RateWeaver; `RateWeaverDependencies` contains the owned
HTTP shim and prebuilt curl. Separate relocated prefixes work through
`CMAKE_PREFIX_PATH`, without build-machine DLL paths in the installed package.

The prebuilt backend is LibreSSL. Direct curl probes used native Windows
ROOT/CA trust stores successfully, but kurlyk v1.1.1 always sets `CURLOPT_CAINFO`
to the client's configured CA file or to adjacent `curl-ca-bundle.crt` when the
setting is empty. The pinned package omits that bundle. For kurlyk HTTPS,
configure a trusted bundle with `HttpClient::set_ca_file()` or deploy
`curl-ca-bundle.crt` beside the executable; direct curl's native-store success
does not establish kurlyk's default HTTPS trust. RateWeaver does not disable
certificate verification or change the host's client settings. For the opt-in
live example, set `RATEWEAVER_LIVE_CA_FILE=/path/to/cacert.pem`.

## Host lifecycle

The host owns one process-wide kurlyk runtime and one persistent, dedicated
`HttpClient` per provider identity. Configure headers, TLS, native
timeouts/retries and rate-limit handles once. Construct `KurlykRefresh`, submit
with `start()` and call `poll()` from the host event loop. Parsing/publication
happen on that host thread. RateWeaver creates no worker or scheduler.

`min_interval_ms` uses monotonic time; quote timestamps use UTC milliseconds.
Those clocks are separate from native HTTP limits. Busy starts cannot overlap
one provider's request. A successful poll publishes one complete validated
source snapshot. Failure preserves the previous snapshot.

`cancel()` fences the current publication; `stop()` is terminal. Native client
cancellation owns actual I/O and exposes a waiting barrier. Invoke it from the
owning host thread, outside callbacks and RateWeaver locks. Keep clock captures
alive through native callbacks. In manual mode, pump `process()` until requests
drain before using waiting barriers or destroying clients. Stop the host runtime
with `deinit()` after normal client teardown, or deliberately stop/join it while
operations are fenced. The [provider guide](providers.md) and
[kurlyk audit](kurlyk-limitations.md) document the remaining constraints.

## Conversion input and output

Capture one `QuoteBook::Snapshot` and construct an immutable `QuoteGraph` for a
consistent valuation. Supply each finite amount, known source currency, target
currency and valuation timestamp. Choose explicit maximum age, future-skew,
source/quorum and fallback policies before converting financial values.

`ConversionResult::status` describes the outcome. `amount` is present only on
success; valid zero and negative amounts are supported. Never replace an
unavailable result with the original number under a target-currency label.
Identity conversion requires a known, valid source identifier equal to the
target; an unknown currency is not native currency.

The ordered route owns each contributing quote, source ID, direction,
timestamps, `QuoteOrigin` and `QuoteUsage`. `Observed` identifies a source
observation, not accuracy or freshness. `Synthetic` identifies an explicit
assumption. `LastKnownFallback` records bounded reuse of an older observation
without changing its origin. The library never invents a parity. Distinct source
IDs prevent duplicate votes but do not prove independent upstream data.

`NoRoute` means the bounded search found no route. `PolicyRejected` covers
rejected quote candidates, invalid policy and exhausted search-work budget.
Any rejected candidate can set that coarse failure flag, including a dead end
unrelated to the destination. It is not a complete route-failure explanation.
Other statuses distinguish invalid input and arithmetic failure. For a direct pair,
`RateRouter::diagnose_pair()` returns the existing `QuotePolicyResult` with
candidate exclusions and aggregation status. It does not search routes or relax
policy when a conversion is unavailable.

The opt-in report reuses `QuotePolicyResult`, keeping `ConversionResult` focused
on the amount and selected route evidence. A full candidate-search tree in every
result would require a separate bounded, route-wide diagnostic contract; this
direct-pair interface does not imply one.

## Time, unavailable data and precision

`published_at` is a known publication instant, `effective_at` is when the rate
starts applying, and `retrieved_at` is successful response completion.
`reference_time` is the valuation instant. Daily adapter calendar dates are
effective-date markers, normalized to UTC midnight when only a day is supplied;
they are not invented publication instants. Future effective rates cannot value
current money, including explicit synthetic assumptions, even with clock-skew
allowance or a disabled age limit. LastKnown only relaxes age. Freshness uses publication,
otherwise effective, otherwise retrieval time. A configured age limit rejects
an observed quote without those timestamps. Leaving `max_age_ms` unset disables
the age limit; it is not a financial preset.

`effective_at` is an appended optional `Quote` constructor argument, preserving
the meaning of existing calls while separating applicability from publication.
Keeping it on the attributed observation avoids a parallel time hierarchy and
translation between parser, graph and route metadata. It does not invent finer
precision than the provider supplies.

Convert known `(amount, currency, valuation time)` components before adding them.
A mixed-currency total cannot be converted unless its components remain known.
Choose valuation time from the monetary event, for example settlement/booking
time or the balance snapshot's `as_of`. `reference_time` checks supplied quotes;
it does not look up history. Without appropriate historical observations, return
unavailable.

Rates/amounts use binary `double`. RateWeaver checks nonfinite results and
overflow/underflow but does not implement decimal money, minor units, rounding
modes or accounting policy. The host owns precision and rounding at its
financial boundary. See [conversion](../examples/convert.cpp) for the domain
model and [persistent refresh](../examples/provider_refresh.cpp) for lifecycle;
the live provider smoke remains a separate opt-in diagnostic.
