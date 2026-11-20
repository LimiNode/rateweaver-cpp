#pragma once

#include <rateweaver/providers/json_detail.hpp>

#include <set>
#include <stdexcept>
#include <utility>

namespace rateweaver {

/// \brief Uses the CDN rate date as day-resolution applicability at UTC midnight.
inline Provider cdn_provider(std::string base = "USD") {
    base = normalize_currency_code(base);
    if (!provider_detail::is_feed_currency(base)) throw std::invalid_argument("Invalid CDN base currency");
    std::string lower = base;
    for (auto& ch : lower) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    const std::string source = "cdn:" + lower;
    return {source, "https://cdn.jsdelivr.net/npm/@fawazahmed0/currency-api@latest/v1/currencies/" + lower + ".json",
        [base, lower, source](std::string_view body, ts_ms_t retrieved) {
            ParseResult result;
            const auto document = provider_detail::read_json(body);
            if (!document || !document->is_object() || !document->contains("date") ||
                !(*document)["date"].is_string() || !document->contains(lower) || !(*document)[lower].is_object()) {
                result.diagnostic = "Invalid CDN response schema";
                return result;
            }
            const auto effective = provider_detail::date_ms((*document)["date"].get<std::string>());
            if (!effective) { result.diagnostic = "Invalid CDN source date"; return result; }
            std::set<std::string> currencies;
            for (const auto& entry : (*document)[lower].items()) {
                const auto currency = normalize_currency_code(entry.key());
                const auto rate = provider_detail::positive_number(entry.value());
                if (!provider_detail::is_feed_currency(entry.key()) || !rate ||
                    !currencies.insert(currency).second || (currency == base && *rate != 1.0)) {
                    ++result.rejected_entries;
                    continue;
                }
                if (currency != base) result.quotes.emplace_back(base, currency, *rate, source,
                    QuoteOrigin::Observed, std::nullopt, retrieved, effective);
            }
            provider_detail::finish(result);
            if (result.rejected_entries) result.diagnostic = "CDN entries rejected";
            return result;
        }};
}

} // namespace rateweaver
