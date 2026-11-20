# Providers and native asynchronous refresh

A `Provider` holds a source ID, URL and parsing function. Parsing owns normalized
quotes and distinguishes complete, partial, empty and malformed data. It has no
HTTP lifecycle dependency. JSON adapters require `rateweaver::providers`; ECB
independently requires `rateweaver::ecb`. Native refresh requires
`rateweaver::kurlyk`. Core requires Time Shield 2.0 but no HTTP/JSON/XML runtime.

| Factory | Endpoint/data | Effective timestamp |
| --- | --- | --- |
| `cdn_provider("USD")` | Fawaz Ahmed Currency API, `https://cdn.jsdelivr.net/npm/@fawazahmed0/currency-api@latest/v1/currencies/usd.json` | Feed `date`, day precision |
| `cbr_provider()` | `https://www.cbr-xml-daily.ru/daily_json.js`, third-party redistribution of Bank of Russia rates | Feed `Date`, normalized from its explicit UTC offset |
| `ecb_provider()` | Official ECB `https://www.ecb.europa.eu/stats/eurofxref/eurofxref-daily.xml` | Daily `Cube time`, day precision |

CDN quotes units per base. CBR quotes foreign currency to RUB using
`Value / Nominal`; ECB quotes EUR to foreign currency. Core derives reciprocals
unless the same source supplies an explicit reverse observation.

Daily adapters set `effective_at`, leaving `published_at` absent: a source rate
date does not establish an exact publication instant. CDN/ECB dates map to UTC
midnight as day-resolution effective markers. CBR's timestamp is normalized to
UTC. `retrieved_at` records successful final-response completion. Retrieval never
replaces known source time merely to make old data fresh.

## Persistent client and host polling

Use the [dependency modes](dependencies.md) to configure bundled or shared host
targets. The bundled `RateWeaverKurlykHttp::client` dependency package exposes
native HTTP headers/CURL with HTTP-only definitions and manual runtime startup.
Hosts with WebSocket/authentication features reuse their existing kurlyk target,
macros and dependencies; RateWeaver does not override them globally.

```cmake
find_package(rateweaver CONFIG REQUIRED COMPONENTS providers kurlyk)
target_link_libraries(my_app PRIVATE rateweaver::providers rateweaver::kurlyk)
```

```cpp
#include <rateweaver/providers.hpp>
#include <rateweaver/runtime.hpp>

// The host has already started its one kurlyk runtime.
rateweaver::QuoteBook book;
kurlyk::HttpClient client;
client.set_timeout(5);                 // Native seconds.
client.set_connect_timeout(3);
client.set_retry_attempts(2, 250);      // Native semantics.
client.set_headers({{"Accept", "application/json"}});
client.set_rate_limit(2, 1000);         // Configure once, persists across refreshes.

rateweaver::RefreshOptions options;
options.min_interval_ms = 60000;       // Monotonic cadence, not HTTP rate limiting.
rateweaver::KurlykRefresh refresh(book, client,
    rateweaver::cdn_provider("USD"), options);

// Repeat from the host event loop/timer.
const auto started = refresh.start(); // Started, NotDue, Busy, etc.
if (const auto result = refresh.poll()) {
    if (result->status == rateweaver::RefreshStatus::ResponseTooLarge) {
        client.cancel_requests();     // Host-thread native cancellation barrier.
    }
    // Updated means one complete validated source snapshot was published.
}

// Host shutdown, outside callbacks and RateWeaver locks.
refresh.stop();                       // Immediate publication fence.
client.cancel_requests();            // Native callback barrier.
// Destroy refresh/client, then deinit the host runtime.
```

Construct one refresh object and a dedicated persistent client per source
identity. Do not mutate the client concurrently or independently publish that
source. Different clients may publish into one QuoteBook. Shared upstream HTTP
limits use the same native `HttpRateLimitHandlePtr` on those clients. RateWeaver
adds no request queue, retry engine, HTTP rate counter or background thread.

The host repeats `poll()` while pending. It may use manual kurlyk `init(false)`
and `process()` instead of a native worker. In manual mode keep pumping until
requests drain before invoking waiting barriers or destroying a busy client.
All translation units must agree on kurlyk feature/startup macros. Construction
sets provider URL, streaming and valid 304 status once; configure other defaults
beforehand.

## Two clock domains and lifetime

`start()` and `poll()` are host-serialized; `cancel()`/`stop()` may run concurrently.
The fifth constructor argument is the UTC clock sampled only at final native
completion. The optional sixth is a monotonic clock sampled only by host/start.
Defaults use Time Shield `ts_ms()` and `monotonic_ms()`. UTC jumps do not affect
cadence. Failures consume the interval; a backward injected monotonic clock
returns NotDue until the interval can again be measured.

Clocks/parser run outside the state mutex and may re-enter cancel/stop. Injected
clock captures must be thread-safe and survive callback drain, including after
refresh destruction. They must not destroy/wait on the native client from its
callback. The default clocks own no host state. Parsing/publication occur on
the host thread; the fence is checked again before publication.

Native callbacks own state, not the refresh object/client/book. Destroying the
refresh fences publication. The client/book must outlive host calls that use
them; destroy the native client on its owner thread. Never call its waiting
barrier or stop the runtime from a network callback. `cancel()` fences the
attempt and permits another after final-callback drain through `poll()`;
`stop()` is terminal. Neither claims to abort I/O.

## Accepted data, publication and freshness

Only a complete, nonempty validated batch publishes by default. HTTP/network
failure, timeout, cancellation, oversized/malformed/empty data preserve the
previous quotes and revision. Partial results do too unless `accept_partial`
explicitly permits replacing the source snapshot with its valid subset.
QuoteBook independently validates source ownership and the complete batch.

Successful publication saves the ETag. An expected 304 returns NotModified
without changing revision or publication/effective/retrieval timestamps.
Unsolicited 304 is an error. UTC retrieval time is captured in the final native
callback; delayed host polling does not change it. Retry notifications never
publish. Final content already contains all streamed chunks and is not appended
again; native retries stop once a streaming chunk has been emitted.

`max_body_bytes` bounds accepted content and safe streamed byte accounting.
Overflow reports ResponseTooLarge early; the host cancels I/O and continues
polling to drain the final callback. **kurlyk v1.1.1 buffers before notification,
so this is not a hard receive-memory ceiling.** See the
[version-specific audit](kurlyk-limitations.md) for fixed group accounting and
remaining pre-append cap/nonwaiting-cancellation work.

Freshness uses `published_at`, then `effective_at`, then `retrieved_at`. Future
effective rates are rejected, including explicit synthetic assumptions, even
with publication-clock skew allowance or a disabled age limit. LastKnown only
relaxes age.
Observed quotes without timestamps are rejected when an age limit is set;
leaving `max_age_ms` unset disables that limit. Distinct source IDs prevent
duplicate votes but do not prove independent upstream observations. Parser
success does not override quorum, NoConsensus, age/skew or LastKnown policy.

These are current daily snapshots, not historical storage. `reference_time`
does not select historical quotes, and the host owns decimal/accounting rounding.
The compact [provider_refresh example](../examples/provider_refresh.cpp) shows
two updates through one client. The opt-in [live smoke](../examples/live_providers.cpp)
diagnoses real feeds, including future-effective data; its separate feed-date
routing check does not authorize current valuation. External unavailability
returns 77 (SKIP). Mandatory tests use loopback HTTP and deterministic fixtures.
