#pragma once

#include <rateweaver/routing/graph.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// \file aggregator.hpp
/// \brief Aggregates directed exchange-rate quotes without losing provenance.

namespace rateweaver {

/// \brief Selects how eligible rates are combined into one rate.
enum class AggregationStrategy {
    Median,   ///< Median of the eligible effective rates.
    Mean,     ///< Arithmetic mean of the eligible effective rates.
    Priority  ///< Single rate selected by source priority.
};

/// \brief Identifies the result of aggregating a directed currency pair.
enum class AggregationStatus {
    Success,             ///< A finite positive rate was produced.
    InvalidPair,         ///< Currency identifiers are invalid or identical.
    InvalidOptions,      ///< One or more aggregation options are invalid.
    NoQuotes,            ///< No directed candidate edges exist for this pair.
    NoEligibleQuotes,    ///< No candidate passed the initial eligibility checks.
    InsufficientSources, ///< Too few distinct sources survived filtering.
    NoConsensus,         ///< Outlier filtering rejected every eligible candidate.
    ArithmeticError      ///< Aggregation could not produce a valid rate.
};

/// \brief Explains why an input quote was excluded from aggregation.
enum class QuoteExclusionReason {
    None,              ///< Eligible quote; check `contributed` for final usage.
    InvalidRate,       ///< Effective direct or reciprocal rate is not valid.
    MissingTimestamp,  ///< Freshness was required but no timestamp was available.
    FutureTimestamp,   ///< Quote timestamp exceeds the allowed future skew.
    Stale,             ///< Quote age exceeds the configured maximum.
    SyntheticDisabled, ///< Synthetic quotes are not enabled by the caller.
    ObservedPreferred, ///< Eligible observed quotes take precedence over synthetic ones.
    DuplicateSource,   ///< Another candidate from the same source was already accepted.
    Outlier            ///< Rate differs too much from the pre-filter median.
};

/// \brief Configures rate aggregation for a single directed currency pair.
///
/// A distinct source is identified by `source_id`. This prevents one source
/// from voting twice, but does not prove statistical independence: providers
/// may themselves redistribute the same upstream data.
struct AggregationOptions {
    AggregationStrategy strategy = AggregationStrategy::Median; ///< Combination method.

    /// \brief Minimum distinct eligible source IDs after outlier filtering.
    /// \note One source is allowed for availability, but a one-source median
    ///       gives no protection against an inaccurate provider. Set this to
    ///       two or more when a quorum is required.
    std::size_t min_sources = 1;

    /// \brief Optional maximum deviation from the preliminary median.
    ///
    /// Uses max(rate / median, median / rate) - 1, so a 0.20 limit
    /// accepts multiplicative deviations of at most 20% in either direction.
    /// The rule is symmetric when converting reciprocal rates.
    /// Applies before Median, Mean and Priority.
    /// With two widely divergent sources both may be excluded: this is a
    /// deliberate refusal to guess which source is correct.
    /// \note std::nullopt disables outlier filtering.
    std::optional<double> max_relative_deviation;

    /// \brief Optional maximum age of a provider quote.
    ///
    /// Uses `published_at` if present, otherwise `retrieved_at`. A missing
    /// timestamp excludes an observed quote when this limit is set.
    /// \note std::nullopt disables freshness filtering, not fallback policy.
    std::optional<std::chrono::seconds> max_age;

    /// \brief Reference time used for freshness checks.
    /// \note If absent, the aggregator samples system_clock::now() once.
    std::optional<Timestamp> reference_time;

    /// \brief Allowed future clock skew for provider timestamps.
    std::chrono::seconds max_future_skew{0};

    /// \brief Allow synthetic rates only when no eligible observed rate exists.
    ///
    /// Synthetic assumptions never participate in an observed-rate median.
    /// They are not subject to provider timestamp requirements. Policies can
    /// impose further restrictions on synthetic fallback at a higher level.
    bool allow_synthetic_fallback = false;

    /// \brief Source IDs in descending order of preference for Priority.
    /// \note Unlisted sources follow listed sources, ordered by source ID.
    std::vector<std::string> source_priority;
};

/// \brief Captures one candidate and its aggregation decision.
struct AggregationSample {
    GraphEdge edge;                   ///< Original graph edge with source metadata.
    std::optional<double> rate;      ///< Effective rate after direction normalization.
    QuoteExclusionReason exclusion = QuoteExclusionReason::None; ///< Rejection reason.
    bool contributed = false;        ///< True if this quote affected the final rate.
};

/// \brief Contains the aggregate rate and auditable source decisions.
///
/// On success, `rate` is finite and positive. On failure it is empty.
/// `samples` preserves every graph candidate, including excluded quotes.
struct AggregationResult {
    AggregationStatus status = AggregationStatus::NoQuotes; ///< Aggregation outcome.
    std::optional<double> rate;                           ///< Effective directed rate.
    std::vector<AggregationSample> samples;                ///< Input and decisions.
    std::size_t eligible_sources = 0;                     ///< Distinct post-filter sources.

    /// \brief Checks whether aggregation produced a rate.
    /// \return True when the status is Success and a rate is present.
    explicit operator bool() const noexcept {
        return status == AggregationStatus::Success && rate.has_value();
    }
};

/// \brief Performs deterministic, provenance-preserving quote aggregation.
///
/// Input quotes are taken from an immutable QuoteGraph. Explicit and derived
/// reciprocal edges are converted to the requested direction before computing
/// statistics. The graph's source metadata and timestamps are not modified.
///
/// Synthetic values are never mixed into observed statistics. Last-known
/// fallback is outside this class: stale observed quotes are excluded when
/// an age limit is enabled, not silently accepted with a different usage tag.
class RateAggregator final {
public:
    /// \brief Aggregates all candidate quotes for a currency pair.
    /// \param graph Immutable quote graph.
    /// \param from Currency to convert from (normalized internally).
    /// \param to Currency to convert to (normalized internally).
    /// \param options Aggregation strategy and eligibility requirements.
    /// \return Result with rate, source decisions and diagnostic status.
    static AggregationResult aggregate(const QuoteGraph& graph,
                                       std::string_view from,
                                       std::string_view to,
                                       const AggregationOptions& options = {}) {
        return aggregate(graph, make_currency_pair(from, to), options);
    }

    /// \brief Aggregates a canonical directed pair from a quote graph.
    /// \param graph Immutable quote graph.
    /// \param pair Canonical directed pair (not automatically normalized).
    /// \param options Aggregation strategy and eligibility requirements.
    /// \return Result with rate, source decisions and diagnostic status.
    static AggregationResult aggregate(const QuoteGraph& graph,
                                       const CurrencyPair& pair,
                                       const AggregationOptions& options = {}) {
        AggregationResult result;
        if (!is_valid_currency_pair(pair)) {
            result.status = AggregationStatus::InvalidPair;
            return result;
        }
        if (!valid_options(options)) {
            result.status = AggregationStatus::InvalidOptions;
            return result;
        }

        const auto edges = graph.edges_between(pair.from, pair.to);
        if (edges.empty()) return result;

        const Timestamp now = options.reference_time
            ? *options.reference_time : std::chrono::system_clock::now();
        std::vector<std::size_t> observed;
        std::vector<std::size_t> synthetic;
        std::set<std::string> observed_sources;
        std::set<std::string> synthetic_sources;
        result.samples.reserve(edges.size());

        for (const auto& edge : edges) {
            AggregationSample sample;
            sample.edge = edge;
            const std::size_t index = result.samples.size();

            const double effective = edge.reversed ? 1.0 / edge.quote.rate
                                                   : edge.quote.rate;
            if (!is_valid_rate(effective)) {
                sample.exclusion = QuoteExclusionReason::InvalidRate;
            } else {
                sample.rate = effective;
                if (edge.quote.origin == QuoteOrigin::Synthetic) {
                    if (!options.allow_synthetic_fallback) {
                        sample.exclusion = QuoteExclusionReason::SyntheticDisabled;
                    } else if (!synthetic_sources.insert(edge.quote.source_id).second) {
                        sample.exclusion = QuoteExclusionReason::DuplicateSource;
                    } else {
                        synthetic.push_back(index);
                    }
                } else {
                    sample.exclusion = freshness_status(edge.quote, options, now);
                    if (sample.exclusion == QuoteExclusionReason::None) {
                        if (!observed_sources.insert(edge.quote.source_id).second) {
                            sample.exclusion = QuoteExclusionReason::DuplicateSource;
                        } else {
                            observed.push_back(index);
                        }
                    }
                }
            }
            result.samples.push_back(std::move(sample));
        }

        std::vector<std::size_t> eligible;
        if (!observed.empty()) {
            eligible = std::move(observed);
            for (const auto index : synthetic) {
                result.samples[index].exclusion = QuoteExclusionReason::ObservedPreferred;
            }
        } else {
            eligible = std::move(synthetic);
        }
        if (eligible.empty()) {
            result.status = AggregationStatus::NoEligibleQuotes;
            return result;
        }

        if (options.max_relative_deviation) {
            const double baseline = median_rate(result.samples, eligible);
            std::vector<std::size_t> retained;
            retained.reserve(eligible.size());
            for (const auto index : eligible) {
                const double value = *result.samples[index].rate;
                const double relative = std::max(value / baseline,
                                                 baseline / value) - 1.0;
                if (relative > *options.max_relative_deviation) {
                    result.samples[index].exclusion = QuoteExclusionReason::Outlier;
                } else {
                    retained.push_back(index);
                }
            }
            eligible = std::move(retained);
            if (eligible.empty()) {
                result.status = AggregationStatus::NoConsensus;
                return result;
            }
        }

        result.eligible_sources = eligible.size();
        if (eligible.size() < options.min_sources) {
            result.status = AggregationStatus::InsufficientSources;
            return result;
        }

        double rate = 0.0;
        switch (options.strategy) {
        case AggregationStrategy::Median:
            rate = median_rate(result.samples, eligible);
            break;
        case AggregationStrategy::Mean:
            rate = mean_rate(result.samples, eligible);
            break;
        case AggregationStrategy::Priority: {
            std::size_t best = eligible.front();
            for (const auto index : eligible) {
                if (preferred(result.samples[index].edge.quote.source_id,
                              result.samples[best].edge.quote.source_id,
                              options.source_priority)) {
                    best = index;
                }
            }
            rate = *result.samples[best].rate;
            result.samples[best].contributed = true;
            break;
        }
        default:
            result.status = AggregationStatus::InvalidOptions;
            return result;
        }

        if (!is_valid_rate(rate)) {
            result.status = AggregationStatus::ArithmeticError;
            return result;
        }
        if (options.strategy != AggregationStrategy::Priority) {
            for (const auto index : eligible) result.samples[index].contributed = true;
        }
        result.rate = rate;
        result.status = AggregationStatus::Success;
        return result;
    }

private:
    static bool valid_options(const AggregationOptions& options) noexcept {
        if (options.min_sources == 0 || options.max_future_skew.count() < 0 ||
            (options.max_age && options.max_age->count() < 0) ||
            (options.max_relative_deviation &&
             (!std::isfinite(*options.max_relative_deviation) ||
              *options.max_relative_deviation < 0.0))) {
            return false;
        }
        switch (options.strategy) {
        case AggregationStrategy::Median:
        case AggregationStrategy::Mean:
        case AggregationStrategy::Priority:
            break;
        default:
            return false;
        }
        for (std::size_t i = 0; i < options.source_priority.size(); ++i) {
            const auto& id = options.source_priority[i];
            if (id.empty() ||
                std::find(options.source_priority.begin(),
                          options.source_priority.begin() + i, id) !=
                    options.source_priority.begin() + i) {
                return false;
            }
        }
        return true;
    }

    static QuoteExclusionReason freshness_status(const Quote& quote,
                                                 const AggregationOptions& options,
                                                 Timestamp now) noexcept {
        if (!options.max_age) return QuoteExclusionReason::None;
        const auto timestamp = quote.published_at ? quote.published_at
                                                  : quote.retrieved_at;
        if (!timestamp) return QuoteExclusionReason::MissingTimestamp;
        if (*timestamp > now) {
            return (*timestamp - now) > options.max_future_skew
                       ? QuoteExclusionReason::FutureTimestamp
                       : QuoteExclusionReason::None;
        }
        return (now - *timestamp) > *options.max_age
                   ? QuoteExclusionReason::Stale
                   : QuoteExclusionReason::None;
    }

    static double median_rate(const std::vector<AggregationSample>& samples,
                              const std::vector<std::size_t>& indices) {
        std::vector<double> values;
        values.reserve(indices.size());
        for (const auto index : indices) values.push_back(*samples[index].rate);
        std::sort(values.begin(), values.end());
        const auto upper = values.size() / 2;
        if (values.size() % 2 != 0) return values[upper];
        // Positive finite operands: this cannot overflow as (a + b) can.
        return values[upper - 1] + (values[upper] - values[upper - 1]) / 2.0;
    }

    static double mean_rate(const std::vector<AggregationSample>& samples,
                            const std::vector<std::size_t>& indices) noexcept {
        double mean = 0.0;
        std::size_t count = 0;
        for (const auto index : indices) {
            ++count;
            const double value = *samples[index].rate;
            mean += (value - mean) / static_cast<double>(count);
        }
        return mean;
    }

    static bool preferred(const std::string& candidate,
                          const std::string& current,
                          const std::vector<std::string>& priority) {
        const auto a = std::find(priority.begin(), priority.end(), candidate);
        const auto b = std::find(priority.begin(), priority.end(), current);
        if (a != b) return a < b;
        return candidate < current;
    }
};

} // namespace rateweaver
