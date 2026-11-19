#pragma once

#include <rateweaver/aggregation/aggregator.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

/// \file quote_policy.hpp
/// \brief Explicitly bounded reuse of previously observed quotes.

namespace rateweaver {

/// \brief Configures optional last-known fallback after normal selection fails.
struct QuotePolicyOptions {
    /// \brief Maximum total age of a last-known observed quote in milliseconds.
    /// \note Requires aggregation.max_age_ms and must be at least that age.
    ///       The default disables fallback. Quorum, source and outlier rules
    ///       remain in force; missing and future timestamps are never repaired.
    std::optional<ts_ms_t> max_last_known_age_ms;
};

/// \brief Retains aggregation diagnostics and independent per-input usage.
struct QuotePolicyResult {
    AggregationResult aggregation;   ///< Selected aggregate or failure diagnostics.
    std::vector<QuoteUsage> usage;   ///< One usage value per aggregation sample.

    explicit operator bool() const noexcept {
        return static_cast<bool>(aggregation);
    }
};

/// \brief Selects normal quotes first, then an explicitly enabled age fallback.
class QuotePolicy final {
public:
    /// \brief Selects a directed pair without modifying the immutable graph.
    static QuotePolicyResult select(const QuoteGraph& graph,
                                    std::string_view from, std::string_view to,
                                    AggregationOptions aggregation = {},
                                    const QuotePolicyOptions& policy = {}) {
        QuotePolicyResult result;
        if (policy.max_last_known_age_ms &&
            (!aggregation.max_age_ms || *policy.max_last_known_age_ms < 0 ||
             *policy.max_last_known_age_ms < *aggregation.max_age_ms)) {
            result.aggregation.status = AggregationStatus::InvalidOptions;
            return result;
        }

        // Freeze the clock across both attempts so the boundary cannot move.
        if (!aggregation.reference_time) aggregation.reference_time = current_time_ms();
        result.aggregation = RateAggregator::aggregate(graph, from, to, aggregation);
        result.usage.resize(result.aggregation.samples.size(), QuoteUsage::Normal);
        if (result || !policy.max_last_known_age_ms ||
            result.aggregation.status == AggregationStatus::InvalidOptions ||
            result.aggregation.status == AggregationStatus::InvalidPair ||
            result.aggregation.status == AggregationStatus::NoQuotes ||
            result.aggregation.status == AggregationStatus::ArithmeticError) {
            return result;
        }

        const auto normal_age = *aggregation.max_age_ms;
        aggregation.max_age_ms = policy.max_last_known_age_ms;
        result.aggregation = RateAggregator::aggregate(graph, from, to, aggregation);
        result.usage.assign(result.aggregation.samples.size(), QuoteUsage::Normal);
        if (result) {
            for (std::size_t i = 0; i < result.aggregation.samples.size(); ++i) {
                const auto& sample = result.aggregation.samples[i];
                if (!sample.contributed || sample.edge.quote.origin != QuoteOrigin::Observed) continue;
                const auto& quote = sample.edge.quote;
                const auto timestamp = quote.published_at ? quote.published_at : quote.retrieved_at;
                if (timestamp && *timestamp <= *aggregation.reference_time &&
                    static_cast<std::uint64_t>(*aggregation.reference_time) -
                        static_cast<std::uint64_t>(*timestamp) >
                        static_cast<std::uint64_t>(normal_age)) {
                    result.usage[i] = QuoteUsage::LastKnownFallback;
                }
            }
        }
        return result;
    }
};

} // namespace rateweaver
