#pragma once

#include "currency.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// \file quote_book.hpp
/// \brief Thread-safe storage of exchange-rate quotes grouped by source.

namespace rateweaver {

/// \brief Describes the result of publishing a source's quote snapshot.
enum class QuotePublishStatus {
    Success,       ///< Publication completed successfully.
    InvalidSource, ///< Source ID is empty or does not match a quote's source ID.
    InvalidQuote,  ///< A quote has an invalid rate or currency identifiers.
    DuplicatePair  ///< The batch contains the same directed pair more than once.
};

/// \brief Stores the latest successfully published quotes for each source.
///
/// A source publishes a complete set of directed quotes, replacing its
/// previous set atomically. Quotes from other sources are unaffected.
/// The book does not aggregate, normalize, expire or select quotes.
///
/// A failed provider request must not be published as an empty batch:
/// retaining previous data is the responsibility of the caller, while
/// freshness and last-known fallback are decided by separate policies.
///
/// All public operations are thread-safe. Returned quotes and snapshots own
/// their data and remain valid after later updates to the book.
class QuoteBook final {
public:
    /// \brief Represents a consistent, independently owned view of the book.
    struct Snapshot {
        std::uint64_t revision = 0; ///< Book revision at the time of capture.
        std::vector<Quote> quotes;  ///< Copies of the quotes in deterministic order.
    };

    /// \brief Constructs an empty quote book.
    QuoteBook() = default;

    QuoteBook(const QuoteBook&) = delete;
    QuoteBook& operator=(const QuoteBook&) = delete;
    QuoteBook(QuoteBook&&) = delete;
    QuoteBook& operator=(QuoteBook&&) = delete;

    /// \brief Replaces all stored quotes for a source as one atomic update.
    /// \param source_id Non-empty identifier of the publishing source.
    /// \param quotes Complete new set of quotes from the source.
    /// \return Success, or a validation error without modifying the book.
    /// \note Every quote must have matching `source_id`, canonical currency
    ///       codes, a valid directed pair and a finite positive rate.
    ///       Duplicate directed pairs within a batch are rejected.
    /// \note An empty valid batch removes the source's existing quotes.
    ///       Do not use an empty batch to report a retrieval or parse failure.
    /// \note Allocation failures may throw; the book remains unchanged.
    QuotePublishStatus replace_source(std::string_view source_id,
                                      std::vector<Quote> quotes) {
        // Own the ID before moving quotes: source_id may refer to quote data.
        const std::string key(source_id);
        if (key.empty()) return QuotePublishStatus::InvalidSource;

        RateMap prepared;
        for (auto& quote : quotes) {
            if (quote.source_id != key) {
                return QuotePublishStatus::InvalidSource;
            }
            if (!is_valid_quote(quote) ||
                !is_valid_currency_code(quote.from) ||
                !is_valid_currency_code(quote.to)) {
                return QuotePublishStatus::InvalidQuote;
            }

            CurrencyPair pair{quote.from, quote.to};
            if (!prepared.emplace(std::move(pair), std::move(quote)).second) {
                return QuotePublishStatus::DuplicatePair;
            }
        }

        std::unique_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_sources.find(key);

        if (prepared.empty()) {
            if (it != m_sources.end()) {
                m_sources.erase(it);
                ++m_revision;
            }
        } else if (it != m_sources.end()) {
            it->second.swap(prepared);
            ++m_revision;
        } else {
            m_sources.emplace(key, std::move(prepared));
            ++m_revision;
        }
        return QuotePublishStatus::Success;
    }

    /// \brief Removes all quotes belonging to one source.
    /// \param source_id Identifier of the source to remove.
    /// \return True if a source was removed; false if it was not present.
    bool erase_source(std::string_view source_id) {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        const auto removed = m_sources.erase(std::string(source_id));
        if (removed != 0) ++m_revision;
        return removed != 0;
    }

    /// \brief Removes all stored sources and their quotes.
    /// \note The revision changes only if the book was not already empty.
    void clear() {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (!m_sources.empty()) {
            m_sources.clear();
            ++m_revision;
        }
    }

    /// \brief Returns a consistent snapshot of quotes from every source.
    /// \return Owned quotes ordered by source ID, then directed pair.
    Snapshot snapshot() const {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        Snapshot result;
        result.revision = m_revision;
        for (const auto& source : m_sources) {
            for (const auto& entry : source.second) {
                result.quotes.push_back(entry.second);
            }
        }
        return result;
    }

    /// \brief Returns a consistent snapshot for a particular source.
    /// \param source_id Identifier of the source to inspect.
    /// \return Owned quotes ordered by directed pair, and book revision.
    /// \note An unknown source produces an empty snapshot.
    Snapshot snapshot_source(std::string_view source_id) const {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        Snapshot result;
        result.revision = m_revision;
        const auto it = m_sources.find(std::string(source_id));
        if (it != m_sources.end()) {
            for (const auto& entry : it->second) {
                result.quotes.push_back(entry.second);
            }
        }
        return result;
    }

    /// \brief Finds quotes for one directed pair across all sources.
    /// \param pair Canonical, directed currency pair to look up.
    /// \return Owned quotes ordered by source ID; empty if none exist.
    /// \note The reciprocal direction is not searched implicitly.
    std::vector<Quote> find_quotes(const CurrencyPair& pair) const {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        std::vector<Quote> result;
        for (const auto& source : m_sources) {
            const auto it = source.second.find(pair);
            if (it != source.second.end()) result.push_back(it->second);
        }
        return result;
    }

private:
    using RateMap = std::map<CurrencyPair, Quote>;
    using SourceMap = std::map<std::string, RateMap>;

    mutable std::shared_mutex m_mutex;
    SourceMap m_sources;
    std::uint64_t m_revision = 0;
};

} // namespace rateweaver
