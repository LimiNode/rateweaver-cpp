#include <rateweaver.hpp>
#include <rateweaver/core/graph.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

using namespace rateweaver;
constexpr ts_ms_t now = 1700000000000LL;
int checks = 0;

void check(bool condition, const char* expression, int line) {
    ++checks;
    if (!condition) throw std::runtime_error(std::string("line ") +
        std::to_string(line) + ": " + expression);
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

bool near(double left, double right) {
    return std::abs(left - right) <= 1e-12 * std::max(1.0, std::abs(right));
}

Quote quote(std::string from, std::string to, double rate,
            std::string source = "feed", ts_ms_t published = now) {
    return {std::move(from), std::move(to), rate, std::move(source),
            QuoteOrigin::Observed, published, now};
}

AggregationOptions fresh_options() {
    AggregationOptions result;
    result.reference_time = now;
    result.max_age_ms = 1000;
    return result;
}

RouterOptions router_options() {
    RouterOptions result;
    result.aggregation = fresh_options();
    return result;
}

void currency_contract() {
    static_assert(std::is_same<ts_ms_t, std::int64_t>::value, "exact millisecond type");
    CHECK(normalize_currency_code(" \tusd\r\n") == "USD");
    CHECK(normalize_currency_code("usdt") == "USDT");
    CHECK(normalize_currency_code("a b") == "A B");
    CHECK(!is_valid_currency_code("A B"));
    CHECK(!is_valid_currency_code("usd"));
    CHECK(!is_valid_currency_code(""));
    CHECK(!is_valid_currency_code(std::string("A\0B", 3)));
    CHECK(is_valid_currency_code("X-CUSTOM.1"));
    CHECK(is_valid_currency_code(std::string(1, static_cast<char>(0xC0))));
    CHECK(make_currency_pair("usd", " eur ") == (CurrencyPair{"USD", "EUR"}));
    CHECK(!is_valid_currency_pair({"USD", "USD"}));
    CHECK(is_valid_amount(0));
    CHECK(is_valid_amount(-100));
    CHECK(!is_valid_amount(std::numeric_limits<double>::infinity()));
    CHECK(!is_valid_rate(0));
    CHECK(!is_valid_rate(-1));
    CHECK(!is_valid_rate(std::numeric_limits<double>::quiet_NaN()));
}

void publication_contract() {
    QuoteBook book;
    CHECK(book.snapshot().revision == 0);
    CHECK(book.replace_source("feed", {}) == QuotePublishStatus::Success);
    CHECK(book.snapshot().revision == 0);
    CHECK(book.replace_source("feed", {quote("USD", "EUR", 2), quote("USD", "RUB", 90)}) ==
        QuotePublishStatus::Success);
    const auto old = book.snapshot();
    CHECK(old.revision == 1 && old.quotes.size() == 2);
    auto invalid = quote("USD", "EUR", 3);
    invalid.from = "usd";
    CHECK(book.replace_source("feed", {invalid}) == QuotePublishStatus::InvalidQuote);
    CHECK(book.replace_source("feed", {quote("USD", "USD", 1)}) == QuotePublishStatus::InvalidQuote);
    CHECK(book.replace_source("feed", {quote("USD", "EUR", 0)}) == QuotePublishStatus::InvalidQuote);
    CHECK(book.replace_source("feed", {quote("USD", "EUR", -1)}) == QuotePublishStatus::InvalidQuote);
    CHECK(book.replace_source("feed", {quote("USD", "EUR", std::numeric_limits<double>::infinity())}) ==
        QuotePublishStatus::InvalidQuote);
    CHECK(book.replace_source("feed", {quote("USD", "EUR", 2), quote("USD", "EUR", 3)}) ==
        QuotePublishStatus::DuplicatePair);
    CHECK(book.replace_source("other", {quote("USD", "EUR", 3)}) == QuotePublishStatus::InvalidSource);
    CHECK(book.replace_source("", {}) == QuotePublishStatus::InvalidSource);
    CHECK(book.snapshot().revision == old.revision);
    CHECK(book.snapshot().quotes[0].rate == old.quotes[0].rate);
    CHECK(book.replace_source("other", {quote("USD", "EUR", 4, "other")}) == QuotePublishStatus::Success);
    CHECK(book.find_quotes({"USD", "EUR"}).size() == 2);
    CHECK(book.find_quotes({"EUR", "USD"}).empty());
    CHECK(book.snapshot_source("absent").quotes.empty());
    CHECK(book.replace_source("feed", {quote("USD", "EUR", 5)}) == QuotePublishStatus::Success);
    CHECK(book.snapshot().revision == 3);
    CHECK(old.quotes.size() == 2 && old.quotes[0].rate == 2);
    CHECK(book.replace_source("feed", {}) == QuotePublishStatus::Success);
    CHECK(book.snapshot_source("feed").quotes.empty());
    CHECK(book.snapshot_source("other").quotes.size() == 1);
    CHECK(!book.erase_source("absent"));
    CHECK(book.erase_source("other"));
    const auto revision = book.snapshot().revision;
    book.clear();
    CHECK(book.snapshot().revision == revision);
    CHECK(book.replace_source("feed", {quote("USD", "EUR", 1)}) == QuotePublishStatus::Success);
    book.clear();
    CHECK(book.snapshot().revision == revision + 2);
}

void graph_contract() {
    const std::vector<Quote> inputs{quote("USD", "EUR", 2, "b"),
        quote("EUR", "USD", 0.4, "b"), quote("USD", "EUR", 3, "a")};
    QuoteGraph graph(inputs);
    CHECK(graph.quote_count() == 3 && graph.edge_count() == 4);
    CHECK(graph.currency_count() == 2 && graph.has_currency("USD"));
    CHECK(graph.outgoing("absent").empty());
    const auto reverse = graph.edges_between("EUR", "USD");
    CHECK(reverse.size() == 2);
    CHECK(reverse[0].quote.source_id == "a" && reverse[0].reversed);
    CHECK(reverse[1].quote.source_id == "b" && !reverse[1].reversed);
    QuoteGraph direct(inputs, GraphBuildOptions{false});
    CHECK(direct.edge_count() == 3);
    auto reversed_inputs = inputs;
    std::reverse(reversed_inputs.begin(), reversed_inputs.end());
    QuoteGraph permuted(reversed_inputs);
    const auto reordered = permuted.edges_between("EUR", "USD");
    for (std::size_t i = 0; i < reverse.size(); ++i) {
        CHECK(reverse[i].quote.source_id == reordered[i].quote.source_id);
        CHECK(reverse[i].quote.rate == reordered[i].quote.rate);
        CHECK(reverse[i].reversed == reordered[i].reversed);
    }
    bool duplicate_rejected = false;
    try { QuoteGraph bad(std::vector<Quote>{inputs[0], inputs[0]}); }
    catch (const std::invalid_argument&) { duplicate_rejected = true; }
    CHECK(duplicate_rejected);
    bool invalid_rejected = false;
    try { QuoteGraph bad(std::vector<Quote>{quote("usd", "EUR", 2)}); }
    catch (const std::invalid_argument&) { invalid_rejected = true; }
    CHECK(invalid_rejected);
    QuoteBook book;
    CHECK(book.replace_source("feed", {quote("USD", "EUR", 2)}) == QuotePublishStatus::Success);
    QuoteGraph frozen(book.snapshot());
    CHECK(book.replace_source("feed", {quote("USD", "EUR", 3)}) == QuotePublishStatus::Success);
    CHECK(frozen.revision() == 1);
    CHECK(frozen.outgoing("USD")[0].quote.rate == 2);
}

void aggregation_contract() {
    QuoteGraph graph(std::vector<Quote>{quote("USD", "EUR", 1, "a"),
        quote("USD", "EUR", 2, "b"), quote("USD", "EUR", 9, "c")});
    auto options = fresh_options();
    auto result = RateAggregator::aggregate(graph, "usd", "eur", options);
    CHECK(result && *result.rate == 2 && result.eligible_sources == 3);
    CHECK(result.samples[0].contributed && result.samples[1].contributed && result.samples[2].contributed);
    options.strategy = AggregationStrategy::Mean;
    result = RateAggregator::aggregate(graph, "USD", "EUR", options);
    CHECK(result && near(*result.rate, 4));
    options.strategy = AggregationStrategy::Priority;
    result = RateAggregator::aggregate(graph, "USD", "EUR", options);
    CHECK(result && *result.rate == 1);
    CHECK(result.samples[0].contributed && !result.samples[1].contributed);
    options.source_priority = {"c", "b"};
    result = RateAggregator::aggregate(graph, "USD", "EUR", options);
    CHECK(result && *result.rate == 9);
    options.disabled_sources.insert("c");
    result = RateAggregator::aggregate(graph, "USD", "EUR", options);
    CHECK(result && *result.rate == 2);
    CHECK(result.samples[2].exclusion == QuoteExclusionReason::SourceDisabled);
    options.source_priority = {"b", "b"};
    CHECK(RateAggregator::aggregate(graph, "USD", "EUR", options).status == AggregationStatus::InvalidOptions);
    options = fresh_options();
    options.min_sources = 4;
    CHECK(RateAggregator::aggregate(graph, "USD", "EUR", options).status == AggregationStatus::InsufficientSources);
    options.min_sources = 0;
    CHECK(RateAggregator::aggregate(graph, "USD", "EUR", options).status == AggregationStatus::InvalidOptions);
    options = fresh_options();
    options.max_relative_deviation = 0.1;
    result = RateAggregator::aggregate(graph, "USD", "EUR", options);
    CHECK(result && *result.rate == 2 && result.eligible_sources == 1);
    CHECK(result.samples[0].exclusion == QuoteExclusionReason::Outlier);
    CHECK(result.samples[2].exclusion == QuoteExclusionReason::Outlier);
    options.min_sources = 2;
    CHECK(RateAggregator::aggregate(graph, "USD", "EUR", options).status == AggregationStatus::InsufficientSources);
    QuoteGraph divergent(std::vector<Quote>{quote("USD", "EUR", 1, "a"), quote("USD", "EUR", 10, "b")});
    CHECK(RateAggregator::aggregate(divergent, "USD", "EUR", options).status == AggregationStatus::NoConsensus);
    options.max_relative_deviation = -1;
    CHECK(RateAggregator::aggregate(graph, "USD", "EUR", options).status == AggregationStatus::InvalidOptions);
    CHECK(RateAggregator::aggregate(graph, "USD", "ZZZ").status == AggregationStatus::NoQuotes);
    CHECK(RateAggregator::aggregate(graph, "USD", "USD").status == AggregationStatus::InvalidPair);

    const auto large = std::numeric_limits<double>::max();
    QuoteGraph extremes(std::vector<Quote>{quote("USD", "EUR", large, "a"), quote("USD", "EUR", large, "b")});
    options = fresh_options();
    CHECK(*RateAggregator::aggregate(extremes, "USD", "EUR", options).rate == large);
    options.strategy = AggregationStrategy::Mean;
    CHECK(*RateAggregator::aggregate(extremes, "USD", "EUR", options).rate == large);
}

void freshness_contract() {
    auto options = fresh_options();
    QuoteGraph boundary(std::vector<Quote>{quote("USD", "EUR", 2, "feed", now - 1000)});
    CHECK(RateAggregator::aggregate(boundary, "USD", "EUR", options));
    QuoteGraph stale(std::vector<Quote>{quote("USD", "EUR", 2, "feed", now - 1001)});
    auto result = RateAggregator::aggregate(stale, "USD", "EUR", options);
    CHECK(!result && result.samples[0].exclusion == QuoteExclusionReason::Stale);
    auto missing = quote("USD", "EUR", 2);
    missing.published_at.reset();
    missing.retrieved_at.reset();
    QuoteGraph no_time(std::vector<Quote>{missing});
    result = RateAggregator::aggregate(no_time, "USD", "EUR", options);
    CHECK(!result && result.samples[0].exclusion == QuoteExclusionReason::MissingTimestamp);
    missing.retrieved_at = now - 1000;
    QuoteGraph retrieved(std::vector<Quote>{missing});
    CHECK(RateAggregator::aggregate(retrieved, "USD", "EUR", options));
    QuoteGraph future(std::vector<Quote>{quote("USD", "EUR", 2, "feed", now + 1)});
    result = RateAggregator::aggregate(future, "USD", "EUR", options);
    CHECK(!result && result.samples[0].exclusion == QuoteExclusionReason::FutureTimestamp);
    options.max_future_skew_ms = 1;
    CHECK(RateAggregator::aggregate(future, "USD", "EUR", options));
    options.max_age_ms.reset();
    options.max_future_skew_ms = 0;
    CHECK(!RateAggregator::aggregate(future, "USD", "EUR", options));
    CHECK(RateAggregator::aggregate(no_time, "USD", "EUR", options));
    options.max_age_ms = std::numeric_limits<ts_ms_t>::max();
    options.reference_time = std::numeric_limits<ts_ms_t>::max();
    QuoteGraph ancient(std::vector<Quote>{quote("USD", "EUR", 2, "feed", std::numeric_limits<ts_ms_t>::min())});
    result = RateAggregator::aggregate(ancient, "USD", "EUR", options);
    CHECK(!result && result.samples[0].exclusion == QuoteExclusionReason::Stale);
    options.reference_time = std::numeric_limits<ts_ms_t>::min();
    options.max_future_skew_ms = std::numeric_limits<ts_ms_t>::max();
    QuoteGraph distant(std::vector<Quote>{quote("USD", "EUR", 2, "feed", std::numeric_limits<ts_ms_t>::max())});
    result = RateAggregator::aggregate(distant, "USD", "EUR", options);
    CHECK(!result && result.samples[0].exclusion == QuoteExclusionReason::FutureTimestamp);
}

void policy_contract() {
    auto options = fresh_options();
    QuoteGraph stale(std::vector<Quote>{quote("USD", "EUR", 2, "old", now - 2000)});
    CHECK(!QuotePolicy::select(stale, "USD", "EUR", options));
    QuotePolicyOptions policy;
    policy.max_last_known_age_ms = 2000;
    auto selected = QuotePolicy::select(stale, "USD", "EUR", options, policy);
    CHECK(selected && selected.usage[0] == QuoteUsage::LastKnownFallback);
    CHECK(selected.aggregation.samples[0].edge.quote.origin == QuoteOrigin::Observed);
    CHECK(selected.aggregation.samples[0].edge.quote.published_at == now - 2000);
    policy.max_last_known_age_ms = 1999;
    CHECK(!QuotePolicy::select(stale, "USD", "EUR", options, policy));
    policy.max_last_known_age_ms = 999;
    CHECK(QuotePolicy::select(stale, "USD", "EUR", options, policy).aggregation.status == AggregationStatus::InvalidOptions);
    policy.max_last_known_age_ms = 2000;
    options.max_age_ms.reset();
    CHECK(QuotePolicy::select(stale, "USD", "EUR", options, policy).aggregation.status == AggregationStatus::InvalidOptions);
    options = fresh_options();
    options.min_sources = 2;
    QuoteGraph mixed(std::vector<Quote>{quote("USD", "EUR", 2, "a"), quote("USD", "EUR", 2, "b", now - 2000)});
    selected = QuotePolicy::select(mixed, "USD", "EUR", options, policy);
    CHECK(selected && selected.usage[0] == QuoteUsage::Normal && selected.usage[1] == QuoteUsage::LastKnownFallback);
    options.disabled_sources.insert("b");
    CHECK(!QuotePolicy::select(mixed, "USD", "EUR", options, policy));
    options = fresh_options();
    auto synthetic = quote("USD", "EUR", 1, "assumption");
    synthetic.origin = QuoteOrigin::Synthetic;
    synthetic.published_at.reset();
    synthetic.retrieved_at.reset();
    QuoteGraph assumed(std::vector<Quote>{synthetic});
    CHECK(!QuotePolicy::select(assumed, "USD", "EUR", options, policy));
    options.allow_synthetic_fallback = true;
    selected = QuotePolicy::select(assumed, "USD", "EUR", options, policy);
    CHECK(selected && selected.usage[0] == QuoteUsage::Normal);
    CHECK(selected.aggregation.samples[0].edge.quote.origin == QuoteOrigin::Synthetic);
    QuoteGraph preferred(std::vector<Quote>{synthetic, quote("USD", "EUR", 2, "observed")});
    selected = QuotePolicy::select(preferred, "USD", "EUR", options, policy);
    CHECK(selected && *selected.aggregation.rate == 2);
    CHECK(selected.aggregation.samples[0].exclusion == QuoteExclusionReason::ObservedPreferred);
    QuoteGraph outliers(std::vector<Quote>{quote("USD", "EUR", 1, "a", now - 2000), quote("USD", "EUR", 10, "b", now - 2000)});
    options.max_relative_deviation = 0.1;
    CHECK(!QuotePolicy::select(outliers, "USD", "EUR", options, policy));
}

void routing_contract() {
    QuoteGraph graph(std::vector<Quote>{quote("USD", "EUR", 2), quote("EUR", "GBP", 3),
        quote("USD", "AUD", 4), quote("AUD", "GBP", 5)});
    auto options = router_options();
    RateRouter router(graph, options);
    auto result = router.convert(10, " usd ", "gbp");
    CHECK(result && near(*result.amount, 200));
    CHECK(result.route.size() == 2 && result.route[0].to() == "AUD");
    CHECK(result.route[0].contributors[0].quote.source_id == "feed");
    CHECK(result.route[0].contributors[0].quote.published_at == now);
    result = router.convert(12, "GBP", "EUR");
    CHECK(result && near(*result.amount, 4));
    CHECK(result.route[0].contributors[0].reversed);
    CHECK(result.route[0].contributors[0].effective_rate == 1.0 / 3.0);
    CHECK(result.route[0].contributors[0].quote.from == "EUR");
    result = router.convert(0, "USD", "GBP");
    CHECK(result && result.amount == 0);
    result = router.convert(-10, "USD", "GBP");
    CHECK(result && near(*result.amount, -200));
    result = router.convert(0, "USD", "ZZZ");
    CHECK(!result && result.status == ConversionStatus::NoRoute && !result.amount);
    result = router.convert(10, "usd", "USD");
    CHECK(result && result.amount == 10 && result.route.empty());
    CHECK(router.convert(10, "", "USD").status == ConversionStatus::InvalidCurrency);
    CHECK(router.convert(std::numeric_limits<double>::quiet_NaN(), "USD", "EUR").status == ConversionStatus::InvalidAmount);
    result = router.convert(10, std::vector<std::string>{"USD", "EUR", "GBP"});
    CHECK(result && near(*result.amount, 60));
    CHECK(router.convert(10, std::vector<std::string>{"USD", "EUR", "usd"}).status == ConversionStatus::InvalidRoute);
    CHECK(router.convert(10, std::vector<std::string>{}).status == ConversionStatus::InvalidRoute);
    CHECK(router.convert(10, std::vector<std::string>{"USD", "ZZZ"}).status == ConversionStatus::NoRoute);
    options.max_hops = 1;
    CHECK(RateRouter(graph, options).convert(10, "USD", "GBP").status == ConversionStatus::NoRoute);
    CHECK(RateRouter(graph, options).convert(10, std::vector<std::string>{"USD", "EUR", "GBP"}).status == ConversionStatus::InvalidRoute);
    options = router_options();
    options.max_search_states = 1;
    CHECK(RateRouter(graph, options).convert(10, "USD", "GBP").status == ConversionStatus::PolicyRejected);
    options.max_search_states = 2;
    CHECK(RateRouter(graph, options).convert(10, "USD", "GBP").status == ConversionStatus::PolicyRejected);
    options.max_search_states = 0;
    CHECK(RateRouter(graph, options).convert(10, "USD", "GBP").status == ConversionStatus::PolicyRejected);
    options = router_options();
    options.aggregation.disabled_sources.insert("feed");
    CHECK(RateRouter(graph, options).convert(10, "USD", "GBP").status == ConversionStatus::PolicyRejected);
    QuoteGraph old(std::vector<Quote>{quote("USD", "EUR", 2, "feed", now - 2000)});
    options = router_options();
    CHECK(!RateRouter(old, options).convert(0, "USD", "EUR"));
    options.quote_policy.max_last_known_age_ms = 2000;
    result = RateRouter(old, options).convert(10, "USD", "EUR");
    CHECK(result && result.route[0].contributors[0].usage == QuoteUsage::LastKnownFallback);

    QuoteGraph huge(std::vector<Quote>{quote("USD", "EUR", std::numeric_limits<double>::max())});
    CHECK(RateRouter(huge, router_options()).convert(2, "USD", "EUR").status == ConversionStatus::ArithmeticError);
    QuoteGraph tiny(std::vector<Quote>{quote("USD", "EUR", std::numeric_limits<double>::denorm_min())});
    CHECK(RateRouter(tiny, router_options()).convert(0.5, "USD", "EUR").status == ConversionStatus::ArithmeticError);
    CHECK(!RateRouter(tiny, router_options()).convert(1, "EUR", "USD"));
    CHECK(RateRouter(tiny, router_options()).convert(0, "USD", "EUR"));
}

void concurrent_publication() {
    QuoteBook book;
    CHECK(book.replace_source("feed", {quote("USD", "EUR", 1), quote("USD", "RUB", 2)}) == QuotePublishStatus::Success);
    const auto frozen = book.snapshot();
    std::atomic<bool> start{false};
    std::atomic<bool> failed{false};
    std::vector<std::thread> threads;
    for (int writer = 0; writer < 2; ++writer) {
        threads.emplace_back([&, writer] {
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            for (int i = 0; i < 200; ++i) {
                const double rate = 1 + writer * 200 + i;
                if (book.replace_source("feed", {quote("USD", "EUR", rate), quote("USD", "RUB", rate * 2)}) != QuotePublishStatus::Success) failed = true;
            }
        });
    }
    for (int reader = 0; reader < 3; ++reader) {
        threads.emplace_back([&] {
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            std::uint64_t previous_revision = 0;
            for (int i = 0; i < 400; ++i) {
                const auto snapshot = book.snapshot();
                if (snapshot.quotes.size() != 2 || snapshot.revision < previous_revision ||
                    snapshot.quotes[1].rate != snapshot.quotes[0].rate * 2) failed = true;
                previous_revision = snapshot.revision;
            }
        });
    }
    start.store(true, std::memory_order_release);
    for (auto& thread : threads) thread.join();
    CHECK(!failed.load());
    CHECK(book.snapshot().revision == 401);
    CHECK(frozen.revision == 1 && frozen.quotes[0].rate == 1);
}

} // namespace

int main() {
    try {
        currency_contract();
        publication_contract();
        graph_contract();
        aggregation_contract();
        freshness_contract();
        policy_contract();
        routing_contract();
        concurrent_publication();
        std::cout << "core: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "core failure: " << error.what() << '\n';
        return 1;
    }
}
