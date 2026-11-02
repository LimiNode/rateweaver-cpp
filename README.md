# RateWeaver

**RateWeaver** is a C++17 library for collecting, aggregating, and converting fiat and cryptocurrency exchange rates. Its design supports multi-source quotations, graph-based conversion routes, and configurable data-quality and fallback policies.

> **Status:** Early development. This README describes the intended public contract; the API and providers are not implemented yet.

## Design goals

- **Currency identifiers are strings.** Use normalized codes such as `USD`, `EUR`, `RUB`, `BTC`, and `USDT`. Adding a currency must not require recompiling an enum definition.
- **No privileged base currency.** Quotes represent directed currency pairs. A conversion can use a direct quote or a route through intermediate currencies.
- **Multiple data sources.** Keep transport, response parsing, normalization, and quote storage separate. Initial adapters are planned for the ECB, CBR, and CDN exchange-rate feeds.
- **Explicit data quality.** Preserve the source, publication time (when available), retrieval time, and freshness/status of a quote. A successful HTTP response does not automatically mean a successful data update.
- **Configurable selection.** Support currency filters, source priorities, aggregation policies, and limits on route length and quote age.
- **Controlled fallbacks.** Support last-known quotes and configurable synthetic rates (for example, `USD -> USDT` at `1.0`) without representing them as live market observations.
- **Portable and independently usable.** Do not depend on MegaConnector classes, logging macros, Windows-specific APIs, or a mandatory background thread.

## Quote and conversion semantics

A directed quote `(from, to, rate)` means:

```text
1 unit of from = rate units of to
```

Rates must be finite and strictly positive. A reciprocal direction may be derived when permitted by policy. A conversion amount may be zero or negative (for example, when converting P&L); it must be finite. A conversion failure must be distinguishable from the valid numeric result `0`.

Conversion results should expose the computed amount, the selected route, and enough provenance/status information to tell whether live, cached, or synthetic data were used.

Automatic routing must be deterministic, avoid cycles, and enforce a configurable hop limit. A route with the numerically largest rate is **not** automatically the best route: source quality and quote age must be taken into account. Explicit routes should be supported as well.

## Fallback policy

A quote can be used from one of several clearly identified origins:

- **Observed:** supplied by an external provider and accepted by validation.
- **Cached:** a previously observed quote retained according to a maximum-age policy.
- **Synthetic:** an explicitly configured assumed rate, such as a `1:1` peg.

Synthetic values and stale quotes must never be silently presented as fresh observations. Their availability and precedence are configurable. In particular, a `USD/USDT = 1` assumption is not a guaranteed market price.

## Planned architecture

```text
include/
  rateweaver.hpp                  # Public umbrella header
  rateweaver/
    rateweaver.hpp                # Library umbrella header
    core/                         # Currency identifiers, quotes, results, errors
    providers/                    # ECB, CBR, CDN, custom providers
    routing/                      # Conversion graph and route selection
    aggregation/                  # Multi-provider quote combination
    policies/                     # Filtering, freshness, fallback, precedence
    runtime/                      # HTTP transport, refresh and lifecycle
external/                         # Third-party dependencies
examples/
tests/
cmake/
CMakeLists.txt
```

Each domain will expose its own umbrella header. The core models and conversion logic should not require network access.

## Dependencies (planned)

| Dependency | Purpose |
|---|---|
| [kurlyk](https://github.com/NewYaroslav/kurlyk) | HTTP requests |
| [nlohmann/json](https://github.com/nlohmann/json) | Parsing JSON-based providers |
| [pugixml](https://github.com/zeux/pugixml) | Parsing the ECB daily XML feed (ECB adapter only) |

The ECB adapter is planned to keep using the official daily XML feed instead of introducing SDMX-JSON parsing solely to eliminate `pugixml`. It should be optional. The C++ standard library's `<chrono>` facilities should be sufficient initially; `time-shield-cpp` is not a required dependency.

The target integration is **header-only C++17**, including an INTERFACE CMake target. Dependency build modes and optional provider switches will be documented once implemented.

## Initial implementation sequence

1. Core types, invariants, validation, and status/result model.
2. Quote store and a deterministic conversion router (direct, reciprocal, multi-hop, explicit routes).
3. Currency filters, synthetic rates, cache/freshness policy, and aggregation.
4. Transport abstraction and provider adapters (CDN, CBR, ECB).
5. Refresh orchestration, tests, examples, and packaging.

Until the first usable release, API examples and feature guarantees will be added only when verified by tests.

