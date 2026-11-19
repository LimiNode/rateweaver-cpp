#pragma once

#include <rateweaver/providers/common.hpp>
#include <pugixml.hpp>

#include <charconv>
#include <set>
#include <system_error>
#include <utility>

namespace rateweaver {
namespace provider_detail {

inline std::string_view xml_local_name(std::string_view name) noexcept {
    const auto colon = name.find(':');
    return colon == std::string_view::npos ? name : name.substr(colon + 1);
}

inline std::string xml_namespace(pugi::xml_node node) {
    const std::string_view name(node.name());
    const auto colon = name.find(':');
    const std::string attribute = colon == std::string_view::npos ? "xmlns" :
        "xmlns:" + std::string(name.substr(0, colon));
    for (auto ancestor = node; ancestor; ancestor = ancestor.parent()) {
        const auto declaration = ancestor.attribute(attribute.c_str());
        if (declaration) return declaration.value();
    }
    return {};
}

inline bool is_ecb_cube(pugi::xml_node node) {
    return xml_local_name(node.name()) == "Cube" &&
        xml_namespace(node) == "http://www.ecb.int/vocabulary/2002-08-01/eurofxref";
}

} // namespace provider_detail

/// \brief Parses the official daily ECB XML feed without requiring JSON or HTTP.
inline Provider ecb_provider() {
    const std::string source = "ecb-daily";
    return {source, "https://www.ecb.europa.eu/stats/eurofxref/eurofxref-daily.xml",
        [source](std::string_view body, ts_ms_t retrieved) {
            ParseResult result;
            pugi::xml_document document;
            if (!document.load_buffer(body.data(), body.size())) {
                result.diagnostic = "Malformed ECB XML";
                return result;
            }
            const auto root = document.document_element();
            std::size_t root_elements = 0;
            for (const auto& child : document.children()) if (child.type() == pugi::node_element) ++root_elements;
            if (root_elements != 1 || provider_detail::xml_local_name(root.name()) != "Envelope" ||
                provider_detail::xml_namespace(root) != "http://www.gesmes.org/xml/2002-08-01") {
                result.diagnostic = "Invalid ECB envelope";
                return result;
            }
            // Reject ambiguous XML attributes even when the XML parser accepts
            // them. A daily feed must contain exactly one dated rate group.
            std::vector<pugi::xml_node> pending{root};
            while (!pending.empty()) {
                const auto node = pending.back();
                pending.pop_back();
                std::set<std::string> names;
                for (const auto& attribute : node.attributes()) {
                    if (!names.insert(attribute.name()).second) {
                        result.diagnostic = "Duplicate ECB XML attribute";
                        return result;
                    }
                }
                for (const auto& child : node.children()) if (child.type() == pugi::node_element) pending.push_back(child);
            }
            pugi::xml_node outer, dated;
            for (const auto& child : root.children()) if (provider_detail::is_ecb_cube(child)) {
                if (outer) { result.diagnostic = "Multiple ECB rate groups"; return result; }
                outer = child;
            }
            for (const auto& child : outer.children()) if (child.type() == pugi::node_element) {
                if (!provider_detail::is_ecb_cube(child)) { result.diagnostic = "Invalid ECB rate group"; return result; }
                if (dated) { result.diagnostic = "Multiple ECB dates"; return result; }
                dated = child;
            }
            const auto published = provider_detail::date_ms(dated.attribute("time").value());
            if (!dated || !published) { result.diagnostic = "Missing or invalid ECB source date"; return result; }
            std::set<std::string> currencies;
            for (const auto& child : dated.children()) {
                if (child.type() != pugi::node_element) continue;
                const std::string_view raw(child.attribute("currency").value());
                const auto currency = normalize_currency_code(raw);
                const std::string_view value(child.attribute("rate").value());
                double rate = 0;
                const auto number = std::from_chars(value.data(), value.data() + value.size(), rate);
                if (!provider_detail::is_ecb_cube(child) || !provider_detail::is_feed_currency(raw) ||
                    value.empty() || number.ec != std::errc{} || number.ptr != value.data() + value.size() ||
                    !is_valid_rate(rate) || !currencies.insert(currency).second || (currency == "EUR" && rate != 1.0)) {
                    ++result.rejected_entries;
                    continue;
                }
                if (currency != "EUR") result.quotes.emplace_back("EUR", currency, rate, source,
                    QuoteOrigin::Observed, published, retrieved);
            }
            provider_detail::finish(result);
            if (result.rejected_entries) result.diagnostic = "ECB entries rejected";
            return result;
        }};
}

} // namespace rateweaver
