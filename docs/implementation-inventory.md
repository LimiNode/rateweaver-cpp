# Implementation inventory and boundaries

Recovered public baseline: `848bad90818a1d0f8625ab76d1db1feb08876b9c` (`main`).
This inventory distinguishes source recovered at that revision from the current
foundation. Actual executed checks belong to [validation](validation.md).

## Recovered baseline

| Area | Existing source | Missing or unverified evidence |
| --- | --- | --- |
| Core | Quote provenance, publication/retrieval times, optional conversion amount/status | No consumer build; assumed Time Shield include layout was not validated. |
| Currency | ASCII normalization and permissive structural validation | No tests or bounded canonical identifier contract. |
| QuoteBook | Atomic source replacement, duplicate validation, shared-mutex snapshots/revisions | No concurrent or failed-publication regressions. |
| Graph | Owned deterministic edges, reciprocals and explicit reverse preference | Duplicate graph definitions and broken forwarding include. |
| Aggregation | Median/mean/priority, filters, outliers/quorum and synthetic selection | No executable correctness evidence. |
| Policy/routing | Router and LastKnown-related references | Missing quote-policy file, incomplete includes and a namespace error prevented standalone use. |
| Package | Header-only source layout and license | No CMake exports, installed consumers, tests, examples or CI. |
| Providers/runtime | README intentions | No parsers, transport, refresh or provider tests. |

## Current ownership and contracts

The runtime path remains QuoteBook -> immutable Core QuoteGraph -> Aggregation
and QuotePolicy -> RateRouter. No second graph engine or synchronous transport
layer is introduced. Core owns the graph; Aggregation owns QuotePolicy and the
bounded LastKnown policy. NoConsensus stays a final refusal.

Time Shield 2.0 is a declared Core dependency. UTC quote/valuation timestamps
and monotonic refresh intervals have different clock functions. Date validation
and conversion use the dependency where semantics match; provider-specific
schema and precision rules stay in provider parsing.

Quotes retain separate optional publication, effective and retrieval timestamps.
Daily providers use effective-date markers; future effective quotes cannot value
current money. Freshness uses publication, then effective, then retrieval time.
LastKnown relaxes only age and preserves all other exclusions and provenance.

| Area | Implemented foundation | Boundary |
| --- | --- | --- |
| Core/graph/router | Atomic snapshots, owned deterministic reciprocal graph, bounded routes, amount/error status, provenance | No broker tick/execution model. |
| Aggregation/policy | Deterministic strategies, source disabling/quorum, outliers, explicit synthetic fallback, bounded LastKnown | Source identity is not proof of statistical independence. |
| Time/identifiers | Time Shield clocks, effective-time separation, bounded ASCII canonical identifiers | No ISO currency registry, aliases or historical store. |
| Diagnostics | Existing policy result available through direct-pair `diagnose_pair()` | No automatic route-wide diagnostics tree in every conversion result. |
| Providers | Separate CDN/CBR JSON and optional official ECB XML parsing | Network connectivity remains optional for Core and parser tests. |
| Refresh | Native async start/poll, persistent dedicated client, monotonic cadence, ETag, final-callback publication fence | Host owns native runtime, I/O cancellation, HTTP retries/rate limits and scheduling. |
| Dependency delivery | Pinned submodules, explicit bundled/existing modes, installed dependency discovery | Embedded hosts own compatible shared targets; no second runtime or hidden version substitution. |
| kurlyk v1.1.1 | Continuous native outstanding-group accounting; exception-isolated final completion | No pre-append receive cap or client-default-preserving nonwaiting cancel handle. See [audit](kurlyk-limitations.md). |

The removed generic transport/request/response/cancellation-token framework is
not part of this API. Neither calendar/clock fallbacks to another Time Shield
major version nor duplicate HTTP retry/rate-limit machinery are provided.

## Remaining consumer responsibilities

The library has no historical rate repository, decimal-money type or accounting
rounding policy. Hosts must choose explicit freshness/skew/quorum policies and
preserve unavailable results. A mixed-currency total needs known components;
missing historical quotes cannot be replaced by today's rates. See the neutral
[integration guide](integration.md) for that contract and the
[dependency registry](dependencies.md) for reproducible builds.
