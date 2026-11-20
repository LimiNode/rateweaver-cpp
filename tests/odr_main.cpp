#include <rateweaver.hpp>
#include <rateweaver/core/graph.hpp>
#include <rateweaver/aggregation/quote_policy.hpp>

std::string peer_currency();

int main() {
    return peer_currency() == rateweaver::normalize_currency_code(" usd ") ? 0 : 1;
}
