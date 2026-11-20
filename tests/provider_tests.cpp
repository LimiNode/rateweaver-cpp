#include <rateweaver/providers/common.hpp>
#include <rateweaver/routing/router.hpp>
#if RATEWEAVER_TEST_JSON
#include <rateweaver/providers.hpp>
#endif
#if RATEWEAVER_TEST_ECB
#include <rateweaver/providers/ecb.hpp>
#endif

#include <atomic>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {
int checks = 0;
constexpr rateweaver::ts_ms_t day = 1704153600000LL;
constexpr rateweaver::ts_ms_t now = day + 43200000;

void check(bool value, const char* expression, int line) {
    ++checks;
    if (!value) throw std::runtime_error(std::string("line ") + std::to_string(line) + ": " + expression);
}
#define CHECK(value) check(static_cast<bool>(value), #value, __LINE__)

#if RATEWEAVER_TEST_JSON || RATEWEAVER_TEST_ECB
std::string fixture(const std::string& name) {
    std::ifstream stream(std::string(RATEWEAVER_FIXTURE_DIR) + "/" + name, std::ios::binary);
    if (!stream) throw std::runtime_error("fixture missing: " + name);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

const rateweaver::Quote& find(const rateweaver::ParseResult& parsed, std::string_view from, std::string_view to) {
    for (const auto& quote : parsed.quotes) if (quote.from == from && quote.to == to) return quote;
    throw std::runtime_error("missing fixture pair " + std::string(from) + "/" + std::string(to));
}
#endif

void timestamps() {
    CHECK(rateweaver::provider_detail::date_ms("1970-01-01") == 0);
    CHECK(rateweaver::provider_detail::date_ms("1969-12-31") == -86400000LL);
    CHECK(rateweaver::provider_detail::date_ms("2024-01-02") == day);
    CHECK(rateweaver::provider_detail::date_ms("2024-02-29") == 1709164800000LL);
    CHECK(!rateweaver::provider_detail::date_ms("2023-02-29"));
    CHECK(!rateweaver::provider_detail::date_ms("1900-02-29"));
    CHECK(rateweaver::provider_detail::date_ms("2000-02-29"));
    CHECK(!rateweaver::provider_detail::date_ms("2024-13-01"));
    CHECK(!rateweaver::provider_detail::date_ms("2024-00-01"));
    CHECK(!rateweaver::provider_detail::date_ms("2024-02-30"));
    CHECK(!rateweaver::provider_detail::date_ms("0000-01-01"));
    CHECK(rateweaver::provider_detail::instant_ms("2024-01-02T11:30:00+03:00") == day + 30600000LL);
    CHECK(rateweaver::provider_detail::instant_ms("2024-01-02T08:30:00.123Z") == day + 30600123LL);
    CHECK(rateweaver::provider_detail::instant_ms("2024-01-02T08:30:00.1Z") == day + 30600100LL);
    CHECK(rateweaver::provider_detail::instant_ms("2024-01-02T08:30:00-03:00") == day + 41400000LL);
    CHECK(!rateweaver::provider_detail::instant_ms("2024-01-02T08:30:00"));
    CHECK(!rateweaver::provider_detail::instant_ms("2024-01-02T24:30:00Z"));
    CHECK(!rateweaver::provider_detail::instant_ms("2024-01-02T08:30:00+14:01"));
    CHECK(!rateweaver::provider_detail::instant_ms("2024-01-02T08:30:00.Z"));
    CHECK(rateweaver::provider_detail::date_ms("0001-01-01") == -62135596800000LL);
    CHECK(rateweaver::provider_detail::date_ms("9999-12-31") == 253402214400000LL);
    CHECK(rateweaver::provider_detail::date_ms("1999-12-31") == 946598400000LL);
    CHECK(rateweaver::provider_detail::date_ms("2000-01-01") == 946684800000LL);
    CHECK(rateweaver::provider_detail::date_ms("2000-02-29") == 951782400000LL);
    CHECK(rateweaver::provider_detail::date_ms("2024-12-31") == 1735603200000LL);
    CHECK(rateweaver::provider_detail::date_ms("2025-01-01") == 1735689600000LL);
    CHECK(!rateweaver::provider_detail::date_ms("2024-04-31"));
    CHECK(!rateweaver::provider_detail::date_ms("2024/01/02"));
    CHECK(!rateweaver::provider_detail::date_ms("2024-1-02"));
    CHECK(!rateweaver::provider_detail::date_ms("2024-01-02 "));
    CHECK(!rateweaver::provider_detail::date_ms("2024-01-0X"));
    CHECK(!rateweaver::provider_detail::date_ms("+123-01-01"));
    CHECK(!rateweaver::provider_detail::date_ms("-123-01-01"));
    CHECK(rateweaver::provider_detail::instant_ms("1969-12-31T23:59:59.999Z") == -1);
    CHECK(rateweaver::provider_detail::instant_ms("1970-01-01T00:00:00+03:00") == -10800000LL);
    CHECK(rateweaver::provider_detail::instant_ms("1969-12-31T23:00:00-01:00") == 0);
    CHECK(rateweaver::provider_detail::instant_ms("1970-01-01T14:00:00+14:00") == 0);
    CHECK(rateweaver::provider_detail::instant_ms("1969-12-31T10:00:00-14:00") == 0);
    CHECK(rateweaver::provider_detail::instant_ms("1970-01-01T00:00:00.12-00:00") == 120);
    CHECK(rateweaver::provider_detail::instant_ms("2000-02-29T00:00:00Z") == 951782400000LL);
    CHECK(!rateweaver::provider_detail::instant_ms("1900-02-29T00:00:00Z"));
    CHECK(!rateweaver::provider_detail::instant_ms("0000-01-01T00:00:00Z"));
    CHECK(!rateweaver::provider_detail::instant_ms("+123-01-01T00:00:00Z"));
    CHECK(!rateweaver::provider_detail::instant_ms("-123-01-01T00:00:00Z"));
    CHECK(!rateweaver::provider_detail::instant_ms("2024-01-02T08:30:60Z"));
    CHECK(!rateweaver::provider_detail::instant_ms("2024-01-02T08:30:00+15:00"));
    CHECK(!rateweaver::provider_detail::instant_ms("2024-01-02T08:30:00-14:01"));
    CHECK(!rateweaver::provider_detail::instant_ms("2024-01-02T08:30:00+03:60"));
    CHECK(!rateweaver::provider_detail::instant_ms("2024-01-02T08:30:00.1234Z"));
    CHECK(!rateweaver::provider_detail::instant_ms("2024-01-02t08:30:00Z"));
    CHECK(!rateweaver::provider_detail::instant_ms("2024-01-02T08:30:00z"));
    CHECK(!rateweaver::provider_detail::instant_ms("2024-01-02T08:30Z"));
    CHECK(!rateweaver::provider_detail::instant_ms("2024-01-02T08:30:00Z "));
    CHECK(!rateweaver::provider_detail::instant_ms(std::string("2024-01-02T08:30:00Z\0", 21)));
}

#if RATEWEAVER_TEST_JSON
void json_providers() {
    const auto cdn = rateweaver::cdn_provider("usd");
    CHECK(cdn.source_id == "cdn:usd");
    CHECK(cdn.url.find("/usd.json") != std::string::npos);
    const auto parsed = cdn.parse(fixture("cdn.json"), now);
    CHECK(parsed.status == rateweaver::ParseStatus::Success);
    CHECK(parsed.quotes.size() == 3);
    const auto& eur = find(parsed, "USD", "EUR");
    CHECK(eur.rate == 0.8);
    CHECK(!eur.published_at && eur.effective_at == day);
    CHECK(eur.retrieved_at == now);
    CHECK(eur.source_id == cdn.source_id);
    CHECK(eur.origin == rateweaver::QuoteOrigin::Observed);
    CHECK(cdn.parse("{", now).status == rateweaver::ParseStatus::Malformed);
    CHECK(!cdn.parse(R"({"date":"2024-01-02","usd":{"eur":1,"eur":2}})", now));
    CHECK(cdn.parse(R"({"date":"2024-01-02","usd":{}})", now).status == rateweaver::ParseStatus::NoData);
    CHECK(!cdn.parse(R"({"date":"2024-02-30","usd":{"eur":1}})", now));
    CHECK(!cdn.parse(R"({"date":"2024-01-02","usd":[]})", now));
    CHECK(!cdn.parse(R"({"date":"2024-01-02","eur":{"usd":1}})", now));
    const auto partial = cdn.parse(R"({"date":"2024-01-02","usd":{"eur":0.8,"jpy":-1}})", now);
    CHECK(partial.status == rateweaver::ParseStatus::Partial);
    CHECK(partial.rejected_entries == 1);
    CHECK(partial.quotes.size() == 1);
    CHECK(cdn.parse(R"({"date":"2024-01-02","usd":{"eur":1,"EUR":2}})", now).rejected_entries > 0);
    CHECK(!cdn.parse(R"({"date":"2024-01-02","usd":{"eur":"1"}})", now));

    const auto cbr = rateweaver::cbr_provider();
    CHECK(cbr.url.find("cbr-xml-daily.ru") != std::string::npos);
    const auto rub = cbr.parse(fixture("cbr.json"), now);
    CHECK(rub.status == rateweaver::ParseStatus::Success);
    CHECK(rub.quotes.size() == 3);
    CHECK(find(rub, "JPY", "RUB").rate == 0.6);
    CHECK(!find(rub, "USD", "RUB").published_at);
    CHECK(find(rub, "USD", "RUB").effective_at == day + 30600000LL);
    CHECK(find(rub, "USD", "RUB").retrieved_at == now);
    CHECK(find(rub, "USD", "RUB").source_id == cbr.source_id);
    CHECK(cbr.parse("[]", now).status == rateweaver::ParseStatus::Malformed);
    CHECK(cbr.parse(R"({"Date":"2024-01-02T11:30:00+03:00","Valute":{}})", now).status == rateweaver::ParseStatus::NoData);
    CHECK(!cbr.parse(R"({"Date":"2024-01-02T11:30:00","Valute":{}})", now));
    const auto bad_nominal = cbr.parse(R"({"Date":"2024-01-02T11:30:00+03:00","Valute":{"a":{"CharCode":"USD","Nominal":0,"Value":90},"b":{"CharCode":"EUR","Nominal":1,"Value":100}}})", now);
    CHECK(bad_nominal.status == rateweaver::ParseStatus::Partial);
    CHECK(bad_nominal.rejected_entries == 1);
    CHECK(bad_nominal.quotes.size() == 1);

    const auto future = cbr.parse(R"({"Date":"2024-01-03T00:00:00+03:00","Valute":{"USD":{"CharCode":"USD","Nominal":1,"Value":90}}})", now);
    CHECK(future && future.quotes[0].effective_at == day + 75600000LL);
    rateweaver::RouterOptions options;
    options.aggregation.reference_time = now;
    options.aggregation.max_future_skew_ms = 86400000;
    const rateweaver::QuoteGraph graph(future.quotes);
    const rateweaver::RateRouter router(graph, options);
    CHECK(!router.convert(1, "USD", "RUB").amount);
    CHECK(!router.convert(1, "RUB", "USD").amount);
    const auto report = router.diagnose_pair("USD", "RUB");
    CHECK(report.aggregation.samples[0].exclusion == rateweaver::QuoteExclusionReason::FutureEffectiveDate);
    options.aggregation.reference_time = future.quotes[0].effective_at;
    CHECK(rateweaver::RateRouter(graph, options).convert(1, "USD", "RUB").amount == 90);

    // Parsing the same daily value again updates retrieval evidence only.
    const auto repeated = cdn.parse(fixture("cdn.json"), now + 86400000);
    CHECK(find(repeated, "USD", "EUR").effective_at == eur.effective_at);
    CHECK(find(repeated, "USD", "EUR").retrieved_at == now + 86400000);
    options.aggregation.reference_time = now + 86400000;
    options.aggregation.max_age_ms = 86400000;
    const rateweaver::QuoteGraph repeated_graph(repeated.quotes);
    const auto stale = rateweaver::RateRouter(repeated_graph, options).diagnose_pair("USD", "EUR");
    CHECK(!stale && stale.aggregation.samples[0].exclusion == rateweaver::QuoteExclusionReason::Stale);
}
#endif

#if RATEWEAVER_TEST_ECB
void ecb_provider() {
    const auto ecb = rateweaver::ecb_provider();
    CHECK(ecb.url.find("ecb.europa.eu") != std::string::npos);
    const auto parsed = ecb.parse(fixture("ecb.xml"), now);
    CHECK(parsed.status == rateweaver::ParseStatus::Success);
    CHECK(parsed.quotes.size() == 3);
    CHECK(find(parsed, "EUR", "USD").rate == 1.25);
    CHECK(!find(parsed, "EUR", "USD").published_at);
    CHECK(find(parsed, "EUR", "USD").effective_at == day);
    CHECK(find(parsed, "EUR", "USD").retrieved_at == now);
    CHECK(find(parsed, "EUR", "USD").source_id == ecb.source_id);
    CHECK(ecb.parse("<broken", now).status == rateweaver::ParseStatus::Malformed);
    CHECK(!ecb.parse("<notfeed/>", now));
    CHECK(!ecb.parse(fixture("ecb.xml") + "<extra/>", now));
    auto no_data = fixture("ecb.xml");
    const auto begin = no_data.find("<Cube currency=");
    no_data.erase(begin, no_data.find("</Cube>", begin) - begin);
    CHECK(ecb.parse(no_data, now).status == rateweaver::ParseStatus::NoData);
    auto partial = fixture("ecb.xml");
    partial.replace(partial.find("rate=\"140\""), 10, "rate=\"bad\"");
    const auto result = ecb.parse(partial, now);
    CHECK(result.status == rateweaver::ParseStatus::Partial);
    CHECK(result.rejected_entries == 1);
    CHECK(result.quotes.size() == 2);
}
#endif

}

int main() {
    try {
        timestamps();
#if RATEWEAVER_TEST_JSON
        json_providers();
#endif
#if RATEWEAVER_TEST_ECB
        ecb_provider();
#endif
        std::cout << "providers: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
