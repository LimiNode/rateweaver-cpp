#pragma once

#include "types.hpp"

#include <string_view>
#include <tuple>
#include <utility>

/// \file currency.hpp
/// \brief Provides currency-code normalization, validation and directed pairs.

namespace rateweaver {

namespace detail {

/// \brief Checks whether a byte is ASCII whitespace or a control character.
inline constexpr bool is_ascii_space_or_control(unsigned char ch) noexcept {
    return ch <= 0x20 || ch == 0x7F;
}

} // namespace detail

/// \brief Normalizes a currency identifier to its canonical representation.
/// \param code Currency identifier to normalize.
/// \return Identifier with surrounding ASCII whitespace removed and ASCII
///         lowercase letters converted to uppercase.
/// \note Non-ASCII bytes and internal characters are preserved. This function
///       does not validate identifiers or resolve aliases (e.g. USD and USDT).
inline CurrencyCode normalize_currency_code(std::string_view code) {
    while (!code.empty() && detail::is_ascii_space_or_control(
                                static_cast<unsigned char>(code.front()))) {
        code.remove_prefix(1);
    }
    while (!code.empty() && detail::is_ascii_space_or_control(
                                static_cast<unsigned char>(code.back()))) {
        code.remove_suffix(1);
    }

    CurrencyCode result;
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
/// \return True if the identifier is non-empty, contains no ASCII whitespace
///         or control characters, and contains no lowercase ASCII letters.
/// \note This is not a currency whitelist or a UTF-8 validator. Arbitrary
///       non-ASCII bytes and punctuation are permitted by this core policy.
inline bool is_valid_currency_code(std::string_view code) noexcept {
    if (code.empty()) return false;
    for (char ch : code) {
        const auto byte = static_cast<unsigned char>(ch);
        if (detail::is_ascii_space_or_control(byte) ||
            (ch >= 'a' && ch <= 'z')) {
            return false;
        }
    }
    return true;
}

/// \brief Identifies a directed currency pair without an exchange rate.
/// \note Both identifiers must be normalized before the pair is used as a key.
struct CurrencyPair {
    CurrencyCode from; ///< Base currency identifier.
    CurrencyCode to;   ///< Quote currency identifier.
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
