#pragma once

#include <rateweaver/providers/json_detail.hpp>

#include <cmath>
#include <set>

namespace rateweaver {

/// \brief Parses the third-party cbr-xml-daily.ru redistribution of CBR rates.
inline Provider cbr_provider() {
    const std::string source = "cbr-daily";
    return {source, "https://www.cbr-xml-daily.ru/daily_json.js",
        [source](std::string_view body, ts_ms_t retrieved) {
            ParseResult result;
            const auto document = provider_detail::read_json(body);
            if (!document || !document->is_object() || !document->contains("Date") ||
                !(*document)["Date"].is_string() || !document->contains("Valute") || !(*document)["Valute"].is_object()) {
                result.diagnostic = "Invalid CBR response schema";
                return result;
            }
            const auto published = provider_detail::instant_ms((*document)["Date"].get<std::string>());
            if (!published) { result.diagnostic = "Invalid CBR source timestamp"; return result; }
            std::set<std::string> currencies;
            for (const auto& entry : (*document)["Valute"].items()) {
                const auto& value = entry.value();
                if (!value.is_object() || !value.contains("CharCode") || !value["CharCode"].is_string() ||
                    !value.contains("Nominal") || !value.contains("Value")) {
                    ++result.rejected_entries;
                    continue;
                }
                const auto raw = value["CharCode"].get<std::string>();
                const auto currency = normalize_currency_code(raw);
                const auto nominal = provider_detail::positive_number(value["Nominal"]);
                const auto amount = provider_detail::positive_number(value["Value"]);
                const double rate = nominal && amount ? *amount / *nominal : 0.0;
                if (!provider_detail::is_feed_currency(raw) || !nominal || std::floor(*nominal) != *nominal ||
                    !is_valid_rate(rate) || !currencies.insert(currency).second || (currency == "RUB" && rate != 1.0)) {
                    ++result.rejected_entries;
                    continue;
                }
                if (currency != "RUB") result.quotes.emplace_back(currency, "RUB", rate, source,
                    QuoteOrigin::Observed, published, retrieved);
            }
            provider_detail::finish(result);
            if (result.rejected_entries) result.diagnostic = "CBR entries rejected";
            return result;
        }};
}

} // namespace rateweaver
