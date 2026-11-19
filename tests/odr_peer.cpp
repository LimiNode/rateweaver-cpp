#include <rateweaver.hpp>

std::string peer_currency() {
    return rateweaver::normalize_currency_code("usd");
}
