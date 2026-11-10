#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// \file router.hpp
/// \brief Converts amounts using aggregated exchange rates and currency routes.

namespace rateweaver {

/// \brief Configures route discovery and per-hop exchange-rate aggregation.
struct RouterOptions {
    std::size_t max_hops = 4; ///< Maximum number of conversion steps in an automatic or explicit route.
    std::size_t max_search_states = 10000; ///< Maximum number of partial routes examined by automatic search. Reaching the limit returns PolicyRejected; increase this limit if a dense graph requires more exhaustive route discovery.
    AggregationOptions aggregation;  ///< Controls how independent quotes are combined at each hop.
    QuotePolicyOptions quote_policy; ///< Optional bounded last-known fallback after normal aggregation fails. 
};

/// \brief Converts amounts along graph routes using aggregate rates.
///
/// The router does not mutate the graph and owns no quote data. Each hop
/// is evaluated with QuotePolicy and RateAggregator, including source exclusions,
/// freshness, bounded last-known fallback and outlier policies. Successful RouteStep values preserve contributing
/// quotes and effective rates for auditability.
///
/// Automatic routing searches simple paths in increasing hop count. Ties are
/// broken lexicographically by currency identifiers, not by the resulting
/// monetary amount; it does not optimize for arbitrage or execution costs.
/// No market order can be placed by this class.
///
/// \note Routing results depend on the supplied immutable graph and options.
///       Without an explicit aggregation.reference_time, freshness is evaluated
///       against a single timestamp captured at the start of conversion.
class RateRouter final {
public:
    /// \brief Creates a router referencing an existing immutable graph.
    /// \param graph Graph that must outlive this router.
    /// \param options Routing and aggregation configuration.
    explicit RateRouter(const QuoteGraph& graph, RouterOptions options = {})
        : m_graph(graph), m_options(std::move(options)) {}

    /// \brief Converts an amount using the shortest acceptable simple path.
    /// \param amount Finite amount to convert; zero and negative values are valid.
    /// \param from Source currency (normalized internally).
    /// \param to Destination currency (normalized internally).
    /// \return Conversion outcome with effective route and quote contributors.
    ConversionResult convert(double amount, std::string_view from,
                             std::string_view to) const {
        ConversionResult result;
        if (!is_valid_amount(amount)) {
            result.status = ConversionStatus::InvalidAmount;
            return result;
        }
        const std::string source = normalize_currency_code(from);
        const std::string target = normalize_currency_code(to);
        if (!is_valid_currency_code(source) || !is_valid_currency_code(target)) {
            result.status = ConversionStatus::InvalidCurrency;
            return result;
        }
        if (!valid_options()) {
            result.status = ConversionStatus::PolicyRejected;
            return result;
        }
        if (source == target) {
            result.status = ConversionStatus::Success;
            result.amount = amount;
            return result;
        }

        // A single reference time makes freshness decisions consistent across
        // all candidate routes in this conversion call.
        const auto aggregation = fixed_time_aggregation();
        std::map<std::pair<std::string, std::string>, QuotePolicyResult> cache;
        bool rejected = false;
        bool arithmetic_error = false;
        std::size_t examined = 0;
        std::deque<Path> queue;
        queue.push_back(Path{{source}, amount, {}});

        while (!queue.empty()) {
            if (examined >= m_options.max_search_states) {
                result.status = ConversionStatus::PolicyRejected;
                return result;
            }
            Path current = std::move(queue.front());
            queue.pop_front();
            ++examined;

            const auto& node = current.currencies.back();
            if (current.steps.size() >= m_options.max_hops) continue;

            // QuoteGraph::outgoing() is sorted by destination/source; the set
            // prevents multiple source quotes from expanding the same hop.
            std::set<std::string> next_currencies;
            for (const auto& edge : m_graph.outgoing(node)) {
                const auto& next = edge.to();
                if (std::find(current.currencies.begin(),
                              current.currencies.end(), next) == current.currencies.end()) {
                    next_currencies.insert(next);
                }
            }

            for (const auto& next : next_currencies) {
                const auto& aggregate = aggregate_hop(cache, aggregation, node, next);
                if (!aggregate) {
                    rejected = true;
                    continue;
                }

                const auto converted = multiply(current.amount, *aggregate.aggregation.rate);
                if (!converted) {
                    arithmetic_error = true;
                    continue;
                }

                Path candidate = current;
                candidate.currencies.push_back(next);
                candidate.amount = *converted;
                candidate.steps.push_back(make_step(node, next, aggregate));
                if (next == target) {
                    result.status = ConversionStatus::Success;
                    result.amount = candidate.amount;
                    result.route = std::move(candidate.steps);
                    return result;
                }
                if (candidate.steps.size() < m_options.max_hops) {
                    queue.push_back(std::move(candidate));
                }
            }
        }

        result.status = rejected ? ConversionStatus::PolicyRejected
                                : arithmetic_error ? ConversionStatus::ArithmeticError
                                                   : ConversionStatus::NoRoute;
        return result;
    }

    /// \brief Converts an amount along a caller-specified currency sequence.
    /// \param amount Finite amount to convert.
    /// \param currencies Currency codes including both endpoints, in order.
    /// \return Conversion result; no alternative route is searched on failure.
    /// \note Repeated currencies and routes with too many hops are invalid.
    ConversionResult convert(double amount,
                             const std::vector<std::string>& currencies) const {
        ConversionResult result;
        if (!is_valid_amount(amount)) {
            result.status = ConversionStatus::InvalidAmount;
            return result;
        }
        if (!valid_options()) {
            result.status = ConversionStatus::PolicyRejected;
            return result;
        }
        if (currencies.empty()) {
            result.status = ConversionStatus::InvalidRoute;
            return result;
        }
        std::vector<std::string> route;
        route.reserve(currencies.size());
        std::set<std::string> seen;
        for (const auto& code : currencies) {
            auto normalized = normalize_currency_code(code);
            if (!is_valid_currency_code(normalized)) {
                result.status = ConversionStatus::InvalidCurrency;
                return result;
            }
            if (!seen.insert(normalized).second) {
                result.status = ConversionStatus::InvalidRoute;
                return result;
            }
            route.push_back(std::move(normalized));
        }
        if (route.size() - 1 > m_options.max_hops) {
            result.status = ConversionStatus::InvalidRoute;
            return result;
        }
        if (route.size() == 1) {
            result.status = ConversionStatus::Success;
            result.amount = amount;
            return result;
        }

        const auto aggregation = fixed_time_aggregation();
        double value = amount;
        std::vector<RouteStep> steps;
        steps.reserve(route.size() - 1);
        for (std::size_t i = 1; i < route.size(); ++i) {
            const auto aggregate = QuotePolicy::select(
                m_graph, route[i - 1], route[i], aggregation, m_options.quote_policy);
            if (!aggregate) {
                result.status = aggregate.aggregation.status == AggregationStatus::NoQuotes
                    ? ConversionStatus::NoRoute : ConversionStatus::PolicyRejected;
                return result;
            }
            const auto converted = multiply(value, *aggregate.aggregation.rate);
            if (!converted) {
                result.status = ConversionStatus::ArithmeticError;
                return result;
            }
            value = *converted;
            steps.push_back(make_step(route[i - 1], route[i], aggregate));
        }
        result.status = ConversionStatus::Success;
        result.amount = value;
        result.route = std::move(steps);
        return result;
    }

private:
    struct Path {
        std::vector<std::string> currencies;
        double amount = 0.0;
        std::vector<RouteStep> steps;
    };

    bool valid_options() const noexcept {
        return m_options.max_hops > 0 && m_options.max_search_states > 0;
    }

    AggregationOptions fixed_time_aggregation() const {
        AggregationOptions options = m_options.aggregation;
        if (!options.reference_time) options.reference_time = time_shield::ts_ms();
        return options;
    }

    const QuotePolicyResult& aggregate_hop(
        std::map<std::pair<std::string, std::string>, QuotePolicyResult>& cache,
        const AggregationOptions& options, const std::string& from,
        const std::string& to) const {
        const auto key = std::make_pair(from, to);
        auto it = cache.find(key);
        if (it == cache.end()) {
            it = cache.emplace(key, QuotePolicy::select(m_graph, from, to, options,
                                                         m_options.quote_policy)).first;
        }
        return it->second;
    }

    static std::optional<double> multiply(double amount, double rate) noexcept {
        if (!is_valid_amount(amount) || !is_valid_rate(rate)) return std::nullopt;
        const double converted = amount * rate;
        if (!is_valid_amount(converted)) return std::nullopt;
        // A nonzero amount must not silently underflow to zero.
        if (amount != 0.0 && converted == 0.0) return std::nullopt;
        return converted;
    }

    static RouteStep make_step(const std::string& from, const std::string& to,
                               const QuotePolicyResult& selected) {
        RouteStep step;
        step.from_currency = from;
        step.to_currency = to;
        step.rate = *selected.aggregation.rate;
        for (std::size_t i = 0; i < selected.aggregation.samples.size(); ++i) {
            const auto& sample = selected.aggregation.samples[i];
            if (!sample.contributed) continue;
            QuoteContribution contribution;
            contribution.quote = sample.edge.quote;
            contribution.reversed = sample.edge.reversed;
            contribution.effective_rate = *sample.rate;
            contribution.usage = selected.usage[i];
            step.contributors.push_back(std::move(contribution));
        }
        return step;
    }

    const QuoteGraph& m_graph;
    RouterOptions m_options;
};

} // namespace rateweaver


} // namespace rateweaver
