#pragma once

#include <rateweaver/core/currency.hpp>
#include <time_shield/text/time_parser.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rateweaver {

/// \brief Describes parsing independently of transport and publication.
enum class ParseStatus { Success, Partial, NoData, Malformed };

/// \brief Owns normalized quotes and explicit partial-response diagnostics.
struct ParseResult {
    ParseStatus status = ParseStatus::Malformed;
    std::vector<Quote> quotes;
    std::size_t rejected_entries = 0;
    std::string diagnostic;

    explicit operator bool() const noexcept {
        return (status == ParseStatus::Success || status == ParseStatus::Partial) &&
               !quotes.empty();
    }
};

/// \brief Describes a feed without owning HTTP execution or quote storage.
struct Provider {
    std::string source_id;
    std::string url;
    std::function<ParseResult(std::string_view, ts_ms_t)> parse;
};

namespace provider_detail {

inline bool is_feed_currency(std::string_view value) noexcept {
    if (value.empty() || value.size() > 32) return false;
    for (const unsigned char ch : value) {
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9'))) return false;
    }
    return true;
}

inline std::optional<int> digits(std::string_view value) noexcept {
    int result = 0;
    if (value.empty() || value.size() > 4) return std::nullopt;
    for (const char ch : value) {
        if (ch < '0' || ch > '9') return std::nullopt;
        result = result * 10 + ch - '0';
    }
    return result;
}

/// \brief Maps a source's calendar date to UTC midnight, not an exact release instant.
inline std::optional<ts_ms_t> date_ms(std::string_view value) noexcept {
    if (value.size() != 10 || value[4] != '-' || value[7] != '-' ||
        value.substr(0, 4) == "0000") return std::nullopt;
    ts_ms_t timestamp = 0;
    if (!time_shield::str_to_ts_ms(value.data(), value.size(), timestamp)) return std::nullopt;
    return timestamp;
}

/// \brief Parses an ISO timestamp with explicit UTC offset and millisecond precision.
inline std::optional<ts_ms_t> instant_ms(std::string_view value) noexcept {
    if (value.size() < 20 || value[4] != '-' || value[7] != '-' ||
        value[10] != 'T' || value[13] != ':' || value[16] != ':' ||
        value.substr(0, 4) == "0000") return std::nullopt;
    // Time Shield accepts more ISO forms than these feeds. Keep their explicit
    // zone and at-most-millisecond grammar, then delegate validation/conversion.
    std::size_t offset = 19;
    if (value[offset] == '.') {
        const auto start = ++offset;
        while (offset < value.size() && value[offset] >= '0' && value[offset] <= '9') ++offset;
        const auto count = offset - start;
        if (count == 0 || count > 3) return std::nullopt;
    }
    if (offset >= value.size()) return std::nullopt;
    if (value[offset] == 'Z') {
        if (offset + 1 != value.size()) return std::nullopt;
    } else {
        if ((value[offset] != '+' && value[offset] != '-') || value.size() != offset + 6 ||
            value[offset + 3] != ':') return std::nullopt;
        const auto zone_hour = digits(value.substr(offset + 1, 2));
        const auto zone_minute = digits(value.substr(offset + 4, 2));
        if (!zone_hour || !zone_minute || *zone_hour > 14 || *zone_minute > 59 ||
            (*zone_hour == 14 && *zone_minute != 0)) return std::nullopt;
    }
    ts_ms_t timestamp = 0;
    if (!time_shield::str_to_ts_ms(value.data(), value.size(), timestamp)) return std::nullopt;
    return timestamp;
}

inline void finish(ParseResult& result) {
    if (result.quotes.empty()) {
        result.status = result.rejected_entries ? ParseStatus::Malformed : ParseStatus::NoData;
    } else {
        result.status = result.rejected_entries ? ParseStatus::Partial : ParseStatus::Success;
    }
}

} // namespace provider_detail
} // namespace rateweaver
