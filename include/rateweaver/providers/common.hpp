#pragma once

#include <rateweaver/core/currency.hpp>

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
    if (value.size() != 10 || value[4] != '-' || value[7] != '-') return std::nullopt;
    const auto year = digits(value.substr(0, 4));
    const auto month = digits(value.substr(5, 2));
    const auto day = digits(value.substr(8, 2));
    if (!year || !month || !day || *year < 1 || *month < 1 || *month > 12) return std::nullopt;
    const bool leap = *year % 4 == 0 && (*year % 100 != 0 || *year % 400 == 0);
    constexpr int month_days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (*day < 1 || *day > month_days[*month - 1] + (*month == 2 && leap ? 1 : 0)) return std::nullopt;
    const auto leaps_before = [](int y) { return (y - 1) / 4 - (y - 1) / 100 + (y - 1) / 400; };
    ts_ms_t days = 365LL * (*year - 1970) + leaps_before(*year) - leaps_before(1970);
    for (int m = 1; m < *month; ++m) days += month_days[m - 1] + (m == 2 && leap ? 1 : 0);
    days += *day - 1;
    return days * 86400000LL;
}

/// \brief Parses an ISO timestamp with explicit UTC offset and millisecond precision.
inline std::optional<ts_ms_t> instant_ms(std::string_view value) noexcept {
    if (value.size() < 20 || value[10] != 'T' || value[13] != ':' || value[16] != ':') return std::nullopt;
    const auto date = date_ms(value.substr(0, 10));
    const auto hour = digits(value.substr(11, 2));
    const auto minute = digits(value.substr(14, 2));
    const auto second = digits(value.substr(17, 2));
    if (!date || !hour || !minute || !second || *hour > 23 || *minute > 59 || *second > 59) return std::nullopt;
    std::size_t offset = 19;
    int milliseconds = 0;
    if (value[offset] == '.') {
        const auto start = ++offset;
        while (offset < value.size() && value[offset] >= '0' && value[offset] <= '9') ++offset;
        const auto count = offset - start;
        if (count == 0 || count > 3) return std::nullopt;
        milliseconds = *digits(value.substr(start, count));
        for (std::size_t i = count; i < 3; ++i) milliseconds *= 10;
    }
    if (offset >= value.size()) return std::nullopt;
    int zone_minutes = 0;
    if (value[offset] == 'Z') {
        if (offset + 1 != value.size()) return std::nullopt;
    } else {
        if ((value[offset] != '+' && value[offset] != '-') || value.size() != offset + 6 ||
            value[offset + 3] != ':') return std::nullopt;
        const auto zone_hour = digits(value.substr(offset + 1, 2));
        const auto zone_minute = digits(value.substr(offset + 4, 2));
        if (!zone_hour || !zone_minute || *zone_hour > 14 || *zone_minute > 59 ||
            (*zone_hour == 14 && *zone_minute != 0)) return std::nullopt;
        zone_minutes = (*zone_hour * 60 + *zone_minute) * (value[offset] == '+' ? 1 : -1);
    }
    return *date + *hour * 3600000LL + *minute * 60000LL + *second * 1000LL +
           milliseconds - zone_minutes * 60000LL;
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
