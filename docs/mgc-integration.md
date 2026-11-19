# MGC consumer integration proposal

Status: draft; no MGC integration is implemented by this change. Audit date:
2026-11-19. Consumer revision: `LimiNode/mgc-platform`
`48f69293603032e33be88a783cfd5b078eaf6c5a`, the open Draft
[PR #63](https://github.com/LimiNode/mgc-platform/pull/63), based on
`feat/versioned-demo-profile`.

RateWeaver supplies valuation exchange rates. Its daily reference feeds are
neither broker tick streams nor executable prices.

## Existing dependencies and build boundary

The gitlinks at the audited MGC revision are the pin authority. Its
`docs/dependencies.md` has stale entries for kurlyk and time-shield-cpp.

| Dependency | Actual gitlink | Consumer compatibility |
| --- | --- | --- |
| nlohmann/json | `55f93686c01528224f448c19128836e7df245f72` (`v3.12.0`) | Reuse MGC's existing `nlohmann_json::nlohmann_json` target for JSON providers. |
| kurlyk | `fe2698dec259a7e417883e1f41985ed21ec28b55` | MGC propagates its headers through `mgc_optionx_headers`; its pinned CMake project declares version 1.0.2. A transport adapter must use this host-owned runtime rather than initialize a second global runtime. |
| time-shield-cpp | `d3c251bf173ee6222f6e79186a34c4a0592d5767` | The pin lacks `time_shield/core/types.hpp`, which baseline RateWeaver included. Using RateWeaver's signed millisecond timestamps and standard C++17 clock removes this incompatible include dependency. |
| pugixml | No direct submodule | Only needed when the optional ECB adapter is enabled; review one explicit shared dependency pin in a later integration change. |

MGC creates `nlohmann_json::nlohmann_json` as an imported interface target in
`cmake/deps/json.cmake`. RateWeaver must accept it without fetching another
copy. Core-only use needs none of these dependencies. Do not copy dependencies
into `external/rateweaver-cpp` or link all of `mgc_optionx_headers` merely to
obtain JSON.

Keep FX policy and refresh in the engine. `mgc_engine_host` owns production
engine implementation sources; a capability detail adapter can link
`rateweaver::rateweaver` and the selected provider targets privately. The
composition root owns its lifecycle. GUI controllers receive immutable
versioned DTOs/events and only render status and amounts. Introduce shared
contracts only for fields that actually cross engine/GUI boundaries.

## Conversion projection contract

The MGC adapter accepts an individually known finite amount, its source
currency, target currency, valuation timestamp and an immutable quote snapshot.
It returns the following projection; this is an integration contract, not a
new promise that RateWeaver stores historical data:

| Field | Required meaning |
| --- | --- |
| Status | `Native`, `Converted`, or `Unavailable`, with a diagnostic reason; `NotRequested` belongs to the presentation query. |
| Source and target currency | Canonical validated identifiers; unknown currency is unavailable. |
| Amount | Optional finite value, present only for Native/Converted; zero and negative P&L are valid. |
| Valuation timestamp | UTC millisecond instant at which the monetary value is being valued. |
| Snapshot revision | Identifies the immutable QuoteBook snapshot used for the entire calculation. |
| Route | Ordered conversion steps and effective rates from `ConversionResult::route`. |
| Provenance | Every contributing original quote's source ID, direction and `QuoteOrigin`; retain source publication and successful retrieval timestamps separately. |
| Freshness | Evaluation timestamp, effective quote timestamp, age and applied maximum age/skew; preserve per-contributor evidence, and use the oldest contributing observation for a route age summary. |
| Cached and synthetic flags | Cached means `QuoteUsage::LastKnownFallback`; synthetic means `QuoteOrigin::Synthetic`. They are independent and must not be described as fresh observed quotes. |

Native requires known, equal source and target currencies. RateRouter identity
conversion is not evidence that an unknown source currency equals the requested
currency. Unavailable never carries a converted amount and must never fall back
to the original number under the target currency label. Source amounts may
remain visible only with their original currency and explicit unavailability.

The host must set a finite age limit, a bounded future-skew allowance, reference
time and source policy. LastKnown and synthetic conversion require an explicit
consumer decision and visible provenance. A generic library option that permits
old quotes is not authorization to use them in financial reporting.

## Historical valuation and mixed currencies

Choose the timestamp from the monetary event: realized P&L at settlement/close,
cash flows at booking, and balance/equity at the snapshot's `as_of` instant.
Account time-series values use each point's timestamp. Opening time is not a
substitute for settlement time. A missing close/settlement timestamp leaves
historical realized-P&L conversion unavailable until the data contract is
resolved.

`reference_time` applies eligibility to a supplied snapshot; it does not perform
historical lookup or establish that a quote was valid on that day. The host
must obtain an appropriate historical observation and preserve its effective
date/publication semantics. Today's CDN/CBR/ECB snapshot must not value an old
trade silently. No suitable historical observation means Unavailable.

Convert each known `(amount, currency, valuation timestamp)` before aggregation.
An aggregate containing USD and EUR cannot be converted from a single total
unless separate source components are known. Do not infer a currency from an
account label, trading symbol, selected target or empty `aggregate_currency`.

## Current blockers and acceptance gate

At audited PR #63, `src/mgc/ui/data/trading/TradingStatistics.cpp` classifies an
empty `aggregate_currency` as `ConversionStatus::Native` (the condition at line
51 uses `empty() || equals-target`). This violates the contract above. The
current `tests/trading_statistics_smoke.cpp` uses `assert`, so its checks do not
run in Release with `NDEBUG`. These are verified source findings, not runtime
test results. They must be corrected by the owning MGC work before integration
can claim preserved fail-closed behavior.

The existing controller displays an unavailability message and blocks the
Profit calculation when status is Unavailable. That does not repair incorrect
Native classification, and a message alone does not prove every amount retains
its source-currency label.

A separate MGC Draft PR should be prepared only after a reviewed exact
RateWeaver commit passes standalone Release tests, public-header compilation,
relocated install-consumer verification and provider fixture tests. Review is
still required; passing tests does not make the current working revision a
reviewed pin. MGC remains unchanged by this proposal.

That PR should:

1. Add `external/rateweaver-cpp` as a submodule at the reviewed SHA and update
   `.gitmodules` and `docs/dependencies.md`, reconciling the stale shared pins.
2. Reuse the existing JSON/HTTP runtime and explicitly select optional ECB
   dependencies; add the engine adapter under the owning capability detail
   directory and compile its implementation into `mgc_engine_host`.
3. Add a deterministic Release-active consumer test and an immutable conversion
   DTO projection. No FX business logic belongs in controllers.
4. Verify rendering uses projection status and optional amount before assigning
   a target currency label.

Minimum consumer regressions: known identical currency yields Native; unknown
currency, missing route, expired/rejected/future quotes and unavailable
historical quotes yield no converted amount; valid zero differs from failure;
negative P&L converts; cached/synthetic use is explicit; each route contributor
retains source and timestamps; mixed-currency totals are unavailable without
components; component conversion uses event-specific timestamps; unavailable
projections cannot enter Native/Converted display paths. Run these against the
reviewed SHA and preserve MGC's mandatory CTest/GUI smoke checks.
