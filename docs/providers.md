# Providers and native asynchronous refresh

A `Provider` holds a source ID, URL and parsing function. Parsing owns its
normalized quotes and distinguishes complete, partial, empty and malformed
responses. It has no dependency on kurlyk or HTTP lifecycle.

| Factory | Data and endpoint | Source timestamp |
| --- | --- | --- |
| `cdn_provider("USD")` | Fawaz Ahmed Currency API, `https://cdn.jsdelivr.net/npm/@fawazahmed0/currency-api@latest/v1/currencies/usd.json` | Feed `date`, day resolution |
| `cbr_provider()` | `https://www.cbr-xml-daily.ru/daily_json.js`, third-party redistribution of Bank of Russia rates | Feed `Date`, explicit UTC offset |
| `ecb_provider()` | Official ECB `https://www.ecb.europa.eu/stats/eurofxref/eurofxref-daily.xml` | Daily `Cube time`, day resolution |

CDN quotes units per base. CBR quotes foreign currency to RUB using
`Value / Nominal`; ECB quotes EUR to foreign currency. The Core graph derives
reciprocals unless a source supplies an explicit reverse observation. CDN/ECB
dates map conservatively to UTC midnight; they do not assert an exact release
instant. CBR's timestamp preserves its stated offset. Retrieval time never
substitutes for the source timestamp merely to make old data fresh.

## Persistent client and host polling

JSON adapters require `rateweaver::providers`; ECB independently requires
`rateweaver::ecb`. Native refresh requires the optional `rateweaver::kurlyk`
component and a host-supplied kurlyk dependency. Core needs none of them.

For a standalone HTTP-only application using the external-header/CURL fallback:

```cmake
find_package(rateweaver CONFIG REQUIRED COMPONENTS providers kurlyk)
target_link_libraries(my_app PRIVATE rateweaver::providers rateweaver::kurlyk)
target_compile_definitions(my_app PRIVATE
    KURLYK_AUTO_INIT=0 KURLYK_WEBSOCKET_SUPPORT=0
    KURLYK_AUTH_SUPPORT=0 KURLYK_OAUTH_SUPPORT=0)
```

The fallback resolves HTTP dependencies only. An embedded host with WebSocket
or authentication features reuses its existing kurlyk target, feature macros
and dependencies instead. RateWeaver does not override them globally. Start
the host runtime before constructing native clients.

```cpp
#include <rateweaver/providers.hpp>
#include <rateweaver/runtime.hpp>

// The host has already started its one kurlyk runtime.
rateweaver::QuoteBook book;
kurlyk::HttpClient client;
client.set_timeout(5);                 // native seconds
client.set_connect_timeout(3);
client.set_retry_attempts(2, 250);      // native semantics
client.set_headers({{"Accept", "application/json"}});
client.set_rate_limit(2, 1000);         // configure once, persists across refresh

rateweaver::RefreshOptions options;
options.min_interval_ms = 60000;       // feed cadence, not an HTTP rate limiter
rateweaver::KurlykRefresh refresh(book, client,
    rateweaver::cdn_provider("USD"), options);

// From the host event loop/timer:
const auto started = refresh.start(); // Started, NotDue, Busy, etc.
if (const auto result = refresh.poll()) {
    if (result->status == rateweaver::RefreshStatus::ResponseTooLarge) {
        // Native cancellation barrier on the host thread; see limitations below.
        client.cancel_requests();
    }
    // Updated means the complete source snapshot was atomically replaced.
}

// Host shutdown order, outside any callback or RateWeaver lock:
refresh.stop();                        // immediate publication fence
client.cancel_requests();             // native cancellation callback barrier
// Destroy refresh/client before the host stops its global kurlyk runtime.
```

The host repeats `poll()` while work is pending. It can use kurlyk's manual
`init(false)` plus `process()` event loop instead of a native worker; RateWeaver
does not call either. All translation units must share the host's kurlyk feature
and startup macros. Construction assigns the provider URL, enables streaming
and adds valid HTTP 304 once; configure other native defaults beforehand.
Use a dedicated client per source identity and do not mutate it concurrently.

With manual kurlyk processing, keep pumping until requests drain before invoking
a native waiting barrier or destroying a busy client. A barrier cannot progress
on the same thread while that thread has stopped calling `process()`.

One refresh object controls one source's interval, ETag and publication gate.
Do not independently publish the same source or share that client with another
provider. Different clients can publish concurrently into one QuoteBook. To
share an upstream's physical request limit, assign the same native
`HttpRateLimitHandlePtr` to each client. RateWeaver has no rate counter or HTTP
queue. Failures consume the refresh interval; a backward clock returns NotDue.

`start()` and `poll()` are host-serialized. `cancel()` and `stop()` can run
concurrently with them. Parsing and clock calls run outside the state mutex and
may re-enter cancel/stop. The injected clock must be thread-safe: start samples
it on the host; final completion samples it on the native callback thread.
Its captured resources must survive native callback drain, including callbacks
after cancellation/destruction. It must not destroy the client or call its
cancellation barrier from that callback. The default clock owns no host state.
Publication checks the operation fence after parsing.
There is no user completion callback on the network worker; the host handles
the returned result without a RateWeaver lock.

## Lifetime, cancellation and response size

Native callbacks capture owned state, never the refresh object, client or book.
Destroying the refresh fences publication and leaves pending callbacks safe.
The book/client must outlive host calls that use them. Destroy the native client
on its host thread; its destructor can wait for cancellation callbacks. Do not
destroy it or invoke its cancellation barrier from its network callback.

`cancel()` fences this attempt and allows a new attempt after its final callback
has drained through `poll()`. `stop()` is terminal. Neither waits for HTTP nor
claims to abort native I/O. Use the host's native client cancellation barrier
when required. The gate remains Busy until final completion, preventing an
obsolete response from racing a replacement attempt. ETag stays with the fixed
source identity.

`max_body_bytes` bounds accepted data and streamed byte accounting. A detected
overflow returns ResponseTooLarge early; after the host cancels native I/O,
continue polling to drain the final callback before restarting. Final content
already includes all chunks and is never duplicated. Retry notifications never
publish. The pinned client retries only before the first streaming chunk.

**The pinned kurlyk buffers internally before the streaming callback. This is
not a hard receive-memory ceiling.** The [pinned API audit and upstream
proposal](kurlyk-limitations.md) explains the missing cancellation identity and
pre-append abort API. Full network acceptance remains gated on that review.

## Publication

Only a complete, nonempty, validated batch publishes by default. HTTP/network
failure, timeout, cancellation, oversized response, malformed data and no data
preserve the old quotes and revision. Partial results preserve them too unless
`accept_partial` explicitly permits replacing a snapshot with the valid subset.
QuoteBook still checks source ownership and batch validity before committing.

A successful publication saves its ETag. An expected 304 returns NotModified
without changing revision or quote timestamps; an unsolicited 304 is an error.
The completion clock supplies retrieval time in the final native callback;
delayed host polling cannot change it. Feed parsing does not override routing age/skew,
source/quorum or LastKnown policy. Daily current snapshots are not historical
storage.

The opt-in [live example](../examples/live_providers.cpp) prints HTTP/refresh
status, quote count, source/retrieval time, revision, both conversion directions
and contributors. Its external-network unavailability exit code is 77 (SKIP).
If a feed already announces a future effective date (possible for CBR), it
requires rejection of current valuation, then labels a separate routing check
at the feed timestamp. That check does not authorize current use of future data.
Mandatory tests use a local HTTP server and deterministic fixtures.
