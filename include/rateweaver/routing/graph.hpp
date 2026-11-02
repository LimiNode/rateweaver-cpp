#pragma once

#include <rateweaver/core/quote_book.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

/// \file graph.hpp
/// \brief Builds an immutable currency graph from independent source quotes.

namespace rateweaver {

/// \brief Controls which directed edges are exposed by a quote graph.
struct GraphBuildOptions {
    /// \brief Add reciprocal edges when a source has no explicit reverse quote.
    /// \note Reciprocal edges are derived from the same observation, not an
    ///       additional independent source for median-based aggregation.
    bool include_reciprocals = true;
};

/// \brief Represents one possible directed conversion in the graph.
///
/// A graph edge owns its original quote. When `reversed` is true, the edge
/// uses the inverse direction; its effective rate is 1 / `quote.rate`.
/// The reciprocal is deliberately not calculated during graph construction
/// to avoid premature overflow or underflow. The router checks arithmetic.
///
/// Graph edges are candidates, not selected route steps. Quote freshness,
/// fallback usage and aggregation are evaluated by separate domains.
struct GraphEdge {
    Quote quote;            ///< Original directed quote and its metadata.
    bool reversed = false;  ///< Whether this edge uses the reverse direction.

    /// \brief Returns the effective source currency of this edge.
    /// \return Reference to the canonical source currency identifier.
    const CurrencyCode& from() const noexcept {
        return reversed ? quote.to : quote.from;
    }

    /// \brief Returns the effective destination currency of this edge.
    /// \return Reference to the canonical destination currency identifier.
    const CurrencyCode& to() const noexcept {
        return reversed ? quote.from : quote.to;
    }
};

/// \brief Immutable directed multigraph of currency exchange quotes.
///
/// Every source quote remains a separate candidate edge. Parallel edges
/// allow later aggregation (including median) without losing provenance.
/// The graph never averages, filters by age, selects or prioritizes sources.
/// It also never treats an inferred reciprocal as another independent quote.
///
/// Construction validates the input and throws std::invalid_argument for
/// malformed quotes or duplicate (source, directed pair) entries. Once built,
/// read-only operations are safe to perform concurrently. The graph owns all
/// its quotes and remains independent of future QuoteBook updates.
class QuoteGraph final {
public:
    using EdgeList = std::vector<GraphEdge>;

    /// \brief Builds a graph from a consistent quote-book snapshot.
    /// \param snapshot Source snapshot with canonical quote identifiers.
    /// \param options Controls reciprocal-edge generation.
    /// \throws std::invalid_argument If any quote or source key is invalid,
    ///         or if a source repeats the same directed pair.
    explicit QuoteGraph(const QuoteBook::Snapshot& snapshot,
                        GraphBuildOptions options = {})
        : QuoteGraph(snapshot.quotes, snapshot.revision, options) {}

    /// \brief Builds a graph from an independent collection of quotes.
    /// \param quotes Source quotes with canonical currency identifiers.
    /// \param options Controls reciprocal-edge generation.
    /// \throws std::invalid_argument If any quote or source key is invalid,
    ///         or if a source repeats the same directed pair.
    explicit QuoteGraph(const std::vector<Quote>& quotes,
                        GraphBuildOptions options = {})
        : QuoteGraph(quotes, 0, options) {}

    /// \brief Returns the revision of the source quote-book snapshot.
    /// \return Snapshot revision, or zero for a direct quote collection.
    std::uint64_t revision() const noexcept { return m_revision; }

    /// \brief Returns the number of original source quotes.
    /// \return Number of independent input quote entries.
    std::size_t quote_count() const noexcept { return m_quote_count; }

    /// \brief Returns the number of directed edges, including reciprocals.
    /// \return Total number of stored directed graph edges.
    std::size_t edge_count() const noexcept { return m_edge_count; }

    /// \brief Returns the number of distinct currencies in the graph.
    /// \return Number of currency identifiers appearing in input quotes.
    std::size_t currency_count() const noexcept { return m_currencies.size(); }

    /// \brief Checks whether a currency appears in the graph.
    /// \param currency Canonical currency identifier.
    /// \return True if at least one original quote references the currency.
    bool has_currency(const CurrencyCode& currency) const noexcept {
        return m_currencies.find(currency) != m_currencies.end();
    }

    /// \brief Returns every outgoing candidate edge for a currency.
    /// \param currency Canonical source currency identifier.
    /// \return Owned-by-graph edges, or a reference to an empty list.
    /// \note The reference is valid only while the graph is alive.
    ///       Edges are ordered by destination, source ID and direction.
    const EdgeList& outgoing(const CurrencyCode& currency) const noexcept {
        const auto it = m_adjacency.find(currency);
        return it == m_adjacency.end() ? empty_edges() : it->second;
    }

    /// \brief Finds all candidate edges for one effective directed pair.
    /// \param from Canonical source currency identifier.
    /// \param to Canonical destination currency identifier.
    /// \return Independent copies of the candidate edges in stable order.
    /// \note Both direct and derived reciprocal candidates can be returned.
    ///       Before computing a median, an aggregation policy must account
    ///       for source independence, origin, timestamps and freshness.
    EdgeList edges_between(const CurrencyCode& from,
                           const CurrencyCode& to) const {
        EdgeList result;
        for (const auto& edge : outgoing(from)) {
            if (edge.to() == to) result.push_back(edge);
        }
        return result;
    }

private:
    using SourcePairKey = std::tuple<std::string, CurrencyCode, CurrencyCode>;
    using Adjacency = std::map<CurrencyCode, EdgeList>;

    QuoteGraph(const std::vector<Quote>& quotes,
               std::uint64_t revision,
               GraphBuildOptions options)
        : m_revision(revision), m_quote_count(quotes.size()) {
        std::set<SourcePairKey> available;
        for (const auto& quote : quotes) {
            if (quote.source_id.empty() || !is_valid_quote(quote) ||
                !is_valid_currency_code(quote.from) ||
                !is_valid_currency_code(quote.to)) {
                throw std::invalid_argument("QuoteGraph: invalid source quote");
            }
            if (!available.emplace(quote.source_id, quote.from, quote.to).second) {
                throw std::invalid_argument("QuoteGraph: duplicate source pair");
            }
        }

        for (const auto& quote : quotes) {
            m_currencies.insert(quote.from);
            m_currencies.insert(quote.to);
            add_edge(quote, false);

            // Prefer the provider's explicit reverse quote when it exists.
            // This also avoids counting a source twice for the same direction.
            if (options.include_reciprocals &&
                available.find(SourcePairKey{quote.source_id, quote.to, quote.from}) ==
                    available.end()) {
                add_edge(quote, true);
            }
        }

        for (auto& item : m_adjacency) {
            auto& edges = item.second;
            std::sort(edges.begin(), edges.end(),
                      [](const GraphEdge& lhs, const GraphEdge& rhs) {
                return std::tie(lhs.to(), lhs.quote.source_id, lhs.reversed,
                                lhs.quote.from, lhs.quote.to) <
                       std::tie(rhs.to(), rhs.quote.source_id, rhs.reversed,
                                rhs.quote.from, rhs.quote.to);
            });
        }
    }

    void add_edge(const Quote& quote, bool reversed) {
        GraphEdge edge{quote, reversed};
        m_adjacency[edge.from()].push_back(std::move(edge));
        ++m_edge_count;
    }

    static const EdgeList& empty_edges() noexcept {
        static const EdgeList empty;
        return empty;
    }

    std::uint64_t m_revision = 0;
    std::size_t m_quote_count = 0;
    std::size_t m_edge_count = 0;
    std::set<CurrencyCode> m_currencies;
    Adjacency m_adjacency;
};

} // namespace rateweaver
