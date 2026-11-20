# Implementation inventory and gaps

Audit baseline: `848bad90818a1d0f8625ab76d1db1feb08876b9c` (`main`).
Audit date: 2026-11-19. This inventory separates source evidence at that
revision from checks performed for the consumer foundation change.

## Baseline source inventory

| Area | Implemented source at baseline | Missing or unverified at baseline |
| --- | --- | --- |
| Core types | Quote origin/usage, optional publication/retrieval timestamps, route contributors, optional conversion amount and explicit status | No compilable consumer validation; `types.hpp` depends on a nonexistent path in MGC's pinned time-shield-cpp. |
| Currency | ASCII normalization, structural canonical-code validation, directed pairs | No regression tests. Validation intentionally permits punctuation/non-ASCII bytes and is not an ISO whitelist or UTF-8 check. |
| QuoteBook | Per-source atomic replacement, duplicate validation, shared-mutex reads, owned snapshots and revisions | No concurrency or failed-publication regressions. Empty batches intentionally erase data; providers must not publish failures as empty success. |
| Graph | Immutable owned quotes, deterministic edge order, reciprocal edges and explicit-reverse preference | Duplicate `GraphEdge`/`QuoteGraph` definitions in `core/graph.hpp` and `routing/graph.hpp`; `rateweaver/graph.hpp` includes absent `graph/graph.hpp`. |
| Aggregation | Median, mean, priority, source disabling, age/skew checks, diagnostics, outlier filtering, source quorum and synthetic fallback | No tests; direct time-shield includes are incompatible with the target consumer pin. |
| Quote policy | Umbrella/router references to QuotePolicy and LastKnown usage types | `policies/quote_policy.hpp` is absent. LastKnown behavior cannot be called implemented at this baseline. |
| Routing | Breadth-first simple paths, hop/search limits, explicit routes, amount validation, provenance assembly, overflow/zero-underflow checks | Header lacks required domain includes and has an extra namespace close; depends on absent quote policy. No executable correctness evidence. |
| Umbrellas | Root and domain include entry points | Broken graph/policy includes and duplicate graph ownership prevent self-contained consumer use. |
| Package | Header-only layout and license | No CMake target, install/export config, tests, examples, CI or independent installed consumer. |
| Providers/runtime | Intended architecture in README only | No HTTP transport, parsers, fixtures, refresh/cancellation implementation or network evidence. |

The baseline README says the API is unimplemented while substantial source
exists, and says standard chrono is sufficient while headers require
time-shield-cpp. Neither statement establishes a usable package.

## Foundation decisions

Preserve the QuoteBook → immutable QuoteGraph → aggregation/quote policy →
RateRouter structure. Repair header/domain ownership rather than introduce a
second graph engine. Reconstruct the missing policy with explicit bounded
LastKnown semantics and provenance; do not describe this as a tested baseline
feature.

Use a signed 64-bit Unix millisecond timestamp and C++17 clock for defaults.
This retains the documented millisecond representation while removing a
mandatory dependency and the incompatible time-shield include paths. Tests
must supply deterministic reference timestamps.

Keep transport, parsing and quote publication separate. JSON providers may use
the embedding project's nlohmann JSON target; ECB XML is optional and alone
requires pugixml. The embedding host owns HTTP execution, cancellation and
refresh scheduling. Core/router checks must run without network access or
provider dependencies.

## Verification ledger

| Current foundation area | Status and evidence |
| --- | --- |
| Core, graph and router | Implemented; core regressions exercise publication, revisions, numeric limits, deterministic routes and concurrent snapshots. |
| Quote policy | Missing baseline implementation reconstructed; bounded LastKnown, per-input usage and preserved filtering are exercised by core tests. |
| Package and umbrellas | Install/export, standalone/superproject builds and relocated core/component consumers; optional dependencies are isolated. |
| Graph ownership | Canonical implementation in Core; Routing depends on Core. No old routing graph, forwarding graph header or GraphBuildOptions remains. Reciprocals are automatic. |
| Quote policy correction | Implementation belongs to Aggregation. LastKnown cannot retry NoConsensus or rehabilitate already rejected outliers by adding stale votes. |
| CDN/CBR/ECB | Separate JSON/XML parsers, offline fixtures, real loopback HTTP tests and an opt-in live example. See validation for observed results. |
| Refresh | Native KurlykRefresh start/poll API; persistent host-owned HttpClient, source ETag, overlap gate and publication fence. No RateWeaver background thread or HTTP retry/limiter. |
| Removed transport layer | HttpTransport, RateWeaver HttpRequest/HttpResponse, CancellationToken and the synchronous RefreshController have been removed. |
| Pinned HTTP constraints | The exact MGC pin cannot enforce a pre-append receive memory cap or expose nonwaiting client cancellation. See the separate kurlyk proposal; these requirements remain gated. |
| Historical valuation/MGC | No historical store or MGC adapter is implemented; the separate integration gate is blocked by the audited consumer defects. |

Source inventory is verified by reading the baseline commit. Packaging and
behavioral claims require the actual Release-active tests, not this inventory.
Record the final revision, toolchains, test results and unavailable checks in
the change's validation report/PR; this section must not imply a passing run
before one has occurred.

The [validation report](validation.md) records the observed foundation checks.

Required evidence for this change includes self-contained public headers and
umbrellas; standalone and superproject configuration; an independent relocated
installed consumer; currency/QuoteBook/graph/aggregation/router/policy
regressions; concurrent reader/writer snapshots; and offline provider
success/malformed/no-data/partial-data/error/cancellation publication behavior.
Network availability is not a prerequisite or proof of these tests.

## Remaining product boundaries

There is no broker execution/tick feed, historical quote store, accounting
rounding/decimal-money policy or automatic permission to use stale/synthetic
rates. Daily reference feeds require a consumer valuation policy. A current
quote snapshot plus a historical `reference_time` does not implement historical
valuation. The [MGC proposal](mgc-integration.md) records the audited pins,
consumer contract and current fail-closed blockers. No MGC submodule or runtime
integration is included in the library foundation change.
