#pragma once

#include <chrono>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/// \file types.hpp
/// \brief Defines fundamental currency, quote, route and conversion types.

namespace rateweaver {

/// \brief Identifies a fiat currency, cryptocurrency or other exchangeable asset.
///
/// Currency identifiers are open-ended (e.g. "USD", "USDT", "BTC").
/// They must be normalized before storage or comparison. No fixed currency
/// enumeration is imposed by the core domain.
using CurrencyCode = std::string;

/// \brief Represents a point in time using the system clock.
using Timestamp = std::chrono::system_clock::time_point;

/// \brief Describes the origin of an exchange rate.
///
/// Origin is independent of the way a quote is selected for conversion.
/// A provider quote remains Observed when reused as a fallback.
enum class QuoteOrigin {
    Observed,   ///< Rate received from an external data source.
    Synthetic   ///< Rate supplied by a user-defined assumption or rule.
};

/// \brief Describes how a quote is used in a conversion route.
///
/// Usage does not identify the quote's origin or imply freshness.
/// Freshness and fallback eligibility are determined by policies.
enum class QuoteUsage {
    Normal,             ///< Quote selected by the normal routing policy.
    LastKnownFallback   ///< Last-known quote selected by a fallback policy.
};

/// \brief Represents a directed exchange-rate quote.
///
/// A quote from A to B with rate R means that 1 unit of A is worth R units of B.
/// Currency identifiers must be normalized and distinct, and the rate must be
/// finite and strictly positive.
///
/// Publication and retrieval timestamps are optional and have different
/// meanings: the former belongs to the source data, the latter to retrieval.
struct Quote {
    CurrencyCode from;                    ///< Base currency (one unit).
    CurrencyCode to;                      ///< Quote currency (units per base unit).
    double       rate = 0.0;              ///< Units of `to` per one unit of `from`.
    std::string  source_id;                ///< Provider ID or synthetic source ID.
    QuoteOrigin  origin = QuoteOrigin::Observed; ///< Origin of the rate.
    std::optional<Timestamp> published_at; ///< Publication time reported by the source.
    std::optional<Timestamp> retrieved_at; ///< Time of successful retrieval.

    /// \brief Constructs an empty quote with default field values.
    Quote() = default;

    /// \brief Constructs a directed exchange-rate quote.
    /// \param from_         Base currency identifier.
    /// \param to_           Quote currency identifier.
    /// \param rate_         Units of \p to_ per unit of \p from_.
    /// \param source_id_    Provider or synthetic source identifier.
    /// \param origin_       Origin of the rate.
    /// \param published_at_ Optional source publication time.
    /// \param retrieved_at_ Optional retrieval time.
    /// \note The constructor does not validate or normalize its arguments.
    Quote(CurrencyCode from_, CurrencyCode to_, double rate_,
          std::string source_id_ = {},
          QuoteOrigin origin_ = QuoteOrigin::Observed,
          std::optional<Timestamp> published_at_ = std::nullopt,
          std::optional<Timestamp> retrieved_at_ = std::nullopt)
        : from(std::move(from_)),
          to(std::move(to_)),
          rate(rate_),
          source_id(std::move(source_id_)),
          origin(origin_),
          published_at(published_at_),
          retrieved_at(retrieved_at_) {}
};

/// \brief Represents one directed step of a conversion route.
///
/// The step owns a copy of the selected quote, including its source and
/// timestamps. Therefore, a completed route remains independent of later
/// updates to the quote storage.
struct RouteStep {
    Quote quote;                          ///< Selected quote and its metadata.
    bool reversed = false;               ///< Use the reciprocal of `quote.rate` if true.
    QuoteUsage usage = QuoteUsage::Normal; ///< Policy under which the quote was selected.

    /// \brief Returns the source currency for this route step.
    /// \return Reference to the effective source currency identifier.
    const CurrencyCode& from() const noexcept {
        return reversed ? quote.to : quote.from;
    }

    /// \brief Returns the destination currency for this route step.
    /// \return Reference to the effective destination currency identifier.
    const CurrencyCode& to() const noexcept {
        return reversed ? quote.from : quote.to;
    }
};

/// \brief Identifies the outcome of a currency conversion.
enum class ConversionStatus {
    Success,         ///< Conversion completed successfully.
    InvalidAmount,   ///< Input amount is invalid.
    InvalidCurrency, ///< A currency identifier is invalid.
    InvalidRoute,    ///< The requested route is invalid.
    NoRoute,         ///< No suitable route could be found.
    PolicyRejected,  ///< Available quotes or routes were rejected by policy.
    ArithmeticError  ///< Arithmetic failed to produce a valid result.
};

/// \brief Contains the outcome and selected route of a conversion.
///
/// On success, `amount` must contain a finite value, which may be zero or
/// negative. On failure, `amount` must be empty. The conversion router is
/// responsible for enforcing these invariants.
struct ConversionResult {
    ConversionStatus status = ConversionStatus::NoRoute; ///< Conversion outcome.
    std::optional<double> amount;                       ///< Converted amount on success.
    std::vector<RouteStep> route;                       ///< Selected route and quote snapshots.

    /// \brief Checks whether the conversion succeeded and produced an amount.
    /// \return True if status is Success and amount contains a value.
    /// \note This check does not independently validate that the amount is finite.
    explicit operator bool() const noexcept {
        return status == ConversionStatus::Success && amount.has_value();
    }
};

/// \brief Checks whether an exchange rate is valid.
/// \param rate Exchange rate to validate.
/// \return True if \p rate is finite and strictly positive.
inline bool is_valid_rate(double rate) noexcept {
    return std::isfinite(rate) && rate > 0.0;
}

/// \brief Checks whether a monetary amount is valid.
/// \param amount Amount to validate.
/// \return True if \p amount is finite, including zero and negative values.
inline bool is_valid_amount(double amount) noexcept {
    return std::isfinite(amount);
}

/// \brief Performs structural validation of a quote.
/// \param quote Quote to validate.
/// \return True if currency identifiers are non-empty and distinct, and the
/// exchange rate is finite and strictly positive.
/// \note Currency normalization, source authorization, timestamps and
/// freshness are validated by their respective domains.
inline bool is_valid_quote(const Quote& quote) noexcept {
    return !quote.from.empty() && !quote.to.empty() &&
           quote.from != quote.to && is_valid_rate(quote.rate);
}

} // namespace rateweaver
