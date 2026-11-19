#include <rateweaver.hpp>

#include <iostream>

int main() {
    // Offline example: the host supplies both the observation and valuation time.
    const rateweaver::QuoteGraph graph(std::vector<rateweaver::Quote>{
        {"EUR", "USD", 1.25, "example", rateweaver::QuoteOrigin::Observed,
         1700000000000LL, 1700000000100LL}});
    rateweaver::RouterOptions options;
    options.aggregation.max_age_ms = 1000;
    options.aggregation.reference_time = 1700000000200LL;
    const auto result = rateweaver::RateRouter(graph, options).convert(10.0, "EUR", "USD");
    if (!result) {
        std::cerr << "Conversion unavailable\n";
        return 1;
    }
    std::cout << *result.amount << " USD\n";
    return 0;
}
