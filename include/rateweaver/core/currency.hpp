#pragma once

#include "types.hpp"

#include <string_view>
#include <tuple>
#include <utility>

/// \file currency.hpp
/// \brief Provides currency-code normalization, validation and directed pairs.

namespace rateweaver {

namespace detail {

/// \brief Checks ASCII whitespace without discarding other control bytes.
inline constexpr bool is_ascii_whitespace(unsigned char ch) noexcept {
    return ch == ' ' || (ch >= '\t' && ch <= '\r');
}

} // namespace detail

/// \brief Normalizes a currency identifier to its canonical representation.
/// \param code Currency identifier to normalize.
/// \return Identifier with surrounding ASCII whitespace removed and ASCII
///         lowercase letters converted to uppercase.
/// \note Non-ASCII bytes and internal characters are preserved. This function
///       does not validate identifiers or resolve aliases (e.g. USD and USDT).
inline std::string normalize_currency_code(std::string_view code) {
    while (!code.empty() && detail::is_ascii_whitespace(
                                static_cast<unsigned char>(code.front()))) {
        code.remove_prefix(1);
    }
    while (!code.empty() && detail::is_ascii_whitespace(
                                static_cast<unsigned char>(code.back()))) {
        code.remove_suffix(1);
    }

    std::string result;
    result.reserve(code.size());
    for (char ch : code) {
        if (ch >= 'a' && ch <= 'z') {
            ch = static_cast<char>(ch - 'a' + 'A');
        }
        result.push_back(ch);
    }
    return result;
}

/// \brief Checks whether a currency identifier is in canonical form.
/// \param code Currency identifier to validate.
/// \return True for 1..32 ASCII characters, starting with an uppercase letter
///         or digit and continuing with uppercase letters, digits, `_`, `.`,
///         `:`, or `-`.
/// \note This bounded identifier grammar supports fiat, crypto, metals and
///       custom namespaces without an ISO whitelist or implicit alias mapping.
inline bool is_valid_currency_code(std::string_view code) noexcept {
    if (code.empty() || code.size() > 32) return false;
    bool first = true;
    for (char ch : code) {
        const bool alphanumeric = (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
        if (!alphanumeric && (first || (ch != '_' && ch != '.' && ch != ':' && ch != '-'))) return false;
        first = false;
    }
    return true;
}

/// \brief Identifies a directed currency pair without an exchange rate.
/// \note Both identifiers must be normalized before the pair is used as a key.
struct CurrencyPair {
    std::string from; ///< Base currency identifier.
    std::string to;   ///< Quote currency identifier.
};

/// \brief Compares two directed currency pairs for equality.
/// \param lhs First pair.
/// \param rhs Second pair.
/// \return True if both directed currency identifiers match.
inline bool operator==(const CurrencyPair& lhs, const CurrencyPair& rhs) noexcept {
    return lhs.from == rhs.from && lhs.to == rhs.to;
}

/// \brief Compares two directed currency pairs for inequality.
/// \param lhs First pair.
/// \param rhs Second pair.
/// \return True if the pairs differ.
inline bool operator!=(const CurrencyPair& lhs, const CurrencyPair& rhs) noexcept {
    return !(lhs == rhs);
}

/// \brief Orders currency pairs lexicographically for associative containers.
/// \param lhs First pair.
/// \param rhs Second pair.
/// \return True if \p lhs precedes \p rhs.
inline bool operator<(const CurrencyPair& lhs, const CurrencyPair& rhs) noexcept {
    return std::tie(lhs.from, lhs.to) < std::tie(rhs.from, rhs.to);
}

/// \brief Constructs a pair with normalized currency identifiers.
/// \param from Base currency identifier.
/// \param to Quote currency identifier.
/// \return Directed pair with canonicalized identifiers.
/// \note Validation is separate: the returned pair may still be invalid.
inline CurrencyPair make_currency_pair(std::string_view from, std::string_view to) {
    return {normalize_currency_code(from), normalize_currency_code(to)};
}

/// \brief Checks whether a directed currency pair is structurally valid.
/// \param pair Pair to validate.
/// \return True if both identifiers are canonical and distinct.
/// \note Same-currency conversion is handled by routing without a quote edge.
inline bool is_valid_currency_pair(const CurrencyPair& pair) noexcept {
    return is_valid_currency_code(pair.from) &&
           is_valid_currency_code(pair.to) && pair.from != pair.to;
}

} // namespace rateweaver
