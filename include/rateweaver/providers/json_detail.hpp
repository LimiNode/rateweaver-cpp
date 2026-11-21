#pragma once

#include <rateweaver/providers/common.hpp>
#include <nlohmann/json.hpp>

#include <set>
#include <vector>

namespace rateweaver {
namespace provider_detail {

/// \brief Rejects duplicate object members instead of silently retaining the last one.
inline std::optional<nlohmann::json> read_json(std::string_view body) {
    std::vector<std::set<std::string>> keys;
    bool duplicate = false;
    const auto callback = [&](int, nlohmann::json::parse_event_t event, nlohmann::json& value) {
        if (event == nlohmann::json::parse_event_t::object_start) keys.emplace_back();
        else if (event == nlohmann::json::parse_event_t::key) {
            if (!keys.back().insert(value.get<std::string>()).second) duplicate = true;
        } else if (event == nlohmann::json::parse_event_t::object_end) keys.pop_back();
        return true;
    };
    auto document = nlohmann::json::parse(body.begin(), body.end(), callback, false);
    if (duplicate || document.is_discarded()) return std::nullopt;
    return document;
}

inline std::optional<double> positive_number(const nlohmann::json& value) {
    if (!value.is_number()) return std::nullopt;
    const double number = value.get<double>();
    return is_valid_rate(number) ? std::optional<double>(number) : std::nullopt;
}

} // namespace provider_detail
} // namespace rateweaver
