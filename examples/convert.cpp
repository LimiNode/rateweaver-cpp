#include <rateweaver.hpp>

#include <iostream>
#include <iomanip>

int main() {
    // This is a fictional offline observation, not a current market rate.
    const rateweaver::ts_ms_t publication_time = 1700000000000LL;
    const rateweaver::ts_ms_t effective_time = publication_time;
    const rateweaver::ts_ms_t retrieval_time = publication_time + 100;
    const rateweaver::ts_ms_t valuation_time = publication_time + 200;

    rateweaver::Quote observation;
    observation.from = "EUR";                       // One unit of the base currency.
    observation.to = "USD";                         // Currency in which its value is quoted.
    observation.rate = 1.25;                        // Fictional 1 EUR = 1.25 USD.
    observation.source_id = "offline-demonstration";
    observation.origin = rateweaver::QuoteOrigin::Observed;
    observation.published_at = publication_time;     // Known source release instant.
    observation.effective_at = effective_time;       // Rate applies from this instant.
    observation.retrieved_at = retrieval_time;       // Successful retrieval completion.
    // Observed describes provenance, not freshness or accuracy. An assumed parity
    // would instead be Synthetic and would need explicit fallback permission.
    const rateweaver::QuoteGraph graph(std::vector<rateweaver::Quote>{observation});
    rateweaver::RouterOptions options;
    // One second is only an illustrative window for this deterministic fixture;
    // a daily provider needs a separately chosen financial freshness policy.
    options.aggregation.max_age_ms = 1000;
    options.aggregation.reference_time = valuation_time;
    const double amount_eur = 10.0;
    const auto result = rateweaver::RateRouter(graph, options).convert(amount_eur, "EUR", "USD");
    if (!result) {
        std::cerr << "Conversion unavailable\n";
        return 1;
    }
    // Display formatting is not a decimal-money or accounting rounding policy.
    std::cout << std::fixed << std::setprecision(2) << *result.amount
              << " USD (fixture expectation: 12.50 USD)\n";
    for (const auto& step : result.route) {
        for (const auto& contributor : step.contributors) {
            std::cout << "source=" << contributor.quote.source_id
                      << " origin=" << (contributor.quote.origin == rateweaver::QuoteOrigin::Observed ? "Observed" : "Synthetic")
                      << " usage=" << (contributor.usage == rateweaver::QuoteUsage::Normal ? "Normal" : "LastKnownFallback")
                      << " reversed=" << contributor.reversed << '\n';
        }
    }
    return 0;
}
