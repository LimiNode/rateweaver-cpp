# Providers and host-owned refresh

The provider layer normalizes feed responses into `Quote` values. A `Provider`
holds a source ID, URL and parsing function. `ParseResult` distinguishes complete,
partial, empty and malformed data and owns its quotes. Parsing does not publish,
start a network runtime or expose JSON/XML through the core conversion API.

| Factory | Data and endpoint | Source timestamp |
| --- | --- | --- |
| `cdn_provider("USD")` | Fawaz Ahmed Currency API, `https://cdn.jsdelivr.net/npm/@fawazahmed0/currency-api@latest/v1/currencies/usd.json` | Feed `date`, day resolution |
| `cbr_provider()` | `https://www.cbr-xml-daily.ru/daily_json.js`, a third-party redistribution of Bank of Russia rates, not an official CBR JSON endpoint | Feed `Date`, with its explicit UTC offset |
| `ecb_provider()` | Official ECB `https://www.ecb.europa.eu/stats/eurofxref/eurofxref-daily.xml` | Daily `Cube time`, day resolution |

CDN rates are units per selected base. CBR quotes are foreign currency to RUB,
using `Value / Nominal`; a 100-unit nominal is not a one-unit rate. ECB quotes
are EUR to foreign currency. The graph can derive reciprocals without inventing
an additional independent observation.

CDN and ECB provide a calendar date rather than an exact release instant. It is
encoded as UTC midnight in `published_at` for conservative age calculation.
This is a source timestamp with day precision, not a claim that publication
occurred at midnight. CBR's timestamp is converted from its stated offset;
naive local timestamps are rejected. `retrieved_at` is supplied separately and
must never replace the source timestamp merely to pass freshness filtering.

## Using the adapters

JSON factories are available through `<rateweaver/providers.hpp>` and require
`rateweaver::providers`. ECB is included separately through
`<rateweaver/providers/ecb.hpp>` and requires `rateweaver::ecb`. It can be used
without enabling JSON providers. The common provider and runtime interfaces
need only core and the standard library.

```cpp
#include <rateweaver/providers.hpp>
#include <rateweaver/runtime.hpp>

// book and transport are owned by the embedding host and outlive controller.
rateweaver::RefreshOptions options;
options.min_interval_ms = 60000;
options.timeout_ms = 5000;
options.max_body_bytes = 1024 * 1024;
rateweaver::RefreshController controller(book, transport,
    rateweaver::cdn_provider("USD"), options);

const auto refresh = controller.refresh();
if (refresh.status == rateweaver::RefreshStatus::Updated) {
    const rateweaver::QuoteGraph graph(book.snapshot());
    // Convert using an explicit host freshness policy and this immutable graph.
}
```

Keep one controller per source so its interval, ETag and overlap gate remain
effective. Different sources can publish concurrently through `QuoteBook`.
Do not publish the same source independently behind its controller. There is
no process-global source registry or mandatory thread. A host timer can call the
same `refresh()` method; it returns `NotDue` before starting an early retry.
Failures also consume the interval, and a backward clock does not trigger a
tight request loop. Tests inject a deterministic clock; production defaults to
`current_time_ms()`. A fresh clock sample after HTTP success supplies retrieval
completion time. Clock exceptions propagate after releasing the overlap gate.

## Transport and cancellation

Implement `HttpTransport::get(HttpRequest, CancellationToken)` using the host's
HTTP facility. The request carries URL, total timeout, maximum response bytes
and optional `If-None-Match`. Return explicit network, timeout or cancellation
errors. Enforce body limits while receiving and cancellation/deadline checks
throughout the request. Returned bodies are checked again by the controller.
The controller cannot forcibly interrupt an uncooperative blocking transport.

A host with an existing `kurlyk` runtime can implement this interface using its
current request/cancellation ownership. This foundation does not ship or test a
concrete `kurlyk` bridge, initialize a second runtime, or vendor another HTTP
dependency. TLS verification, redirects and transport resource cleanup remain
the host transport's responsibility. No live HTTP interoperability is claimed
by the offline fixture tests.

Only one call per controller may be active; overlapping calls return `Busy`.
Transport, parser and clock callbacks run outside its mutex. `cancel()` signals
the current operation and permits a later due retry. `stop()` is terminal and
serializes with publication so a late response cannot publish after stop.
It signals cancellation and returns without waiting. Keep controller, book and
transport alive until every synchronous call returns; stop and drain the
host's executor before destroying them. No controller-owned worker needs joining.

## Publication and failures

Only a complete, nonempty, structurally valid batch publishes by default.
Network/HTTP errors, timeouts, cancellation, oversized responses, malformed
data and no-data responses preserve the previous source snapshot and revision.
Partial responses report retained valid quotes and rejected-entry counts but
do not publish unless `accept_partial` is explicitly enabled. Enabling it
authorizes replacement by a subset; it does not merge entries into an older
snapshot.

A successful publication records its ETag for later conditional requests.
An expected `304` returns `NotModified` and leaves all quote times and the book
revision unchanged. An unsolicited `304` is an HTTP error. Feed validation is
independent of routing freshness: a stored observation can subsequently be
rejected as old or future-dated by the conversion policy.

The fixtures under `tests/fixtures/` are deterministic representative responses;
tests also construct malformed/empty/partial variants. They establish parser,
normalization, provenance and failure-publication behavior. Historical lookup,
background scheduling and production HTTP runtime ownership are intentionally
outside this library layer.
