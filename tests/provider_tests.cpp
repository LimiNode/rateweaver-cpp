#include <rateweaver/runtime.hpp>
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
    CHECK(eur.published_at == day);
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
    CHECK(find(rub, "USD", "RUB").published_at == day + 30600000LL);
    CHECK(find(rub, "USD", "RUB").retrieved_at == now);
    CHECK(find(rub, "USD", "RUB").source_id == cbr.source_id);
    CHECK(cbr.parse("[]", now).status == rateweaver::ParseStatus::Malformed);
    CHECK(cbr.parse(R"({"Date":"2024-01-02T11:30:00+03:00","Valute":{}})", now).status == rateweaver::ParseStatus::NoData);
    CHECK(!cbr.parse(R"({"Date":"2024-01-02T11:30:00","Valute":{}})", now));
    const auto bad_nominal = cbr.parse(R"({"Date":"2024-01-02T11:30:00+03:00","Valute":{"a":{"CharCode":"USD","Nominal":0,"Value":90},"b":{"CharCode":"EUR","Nominal":1,"Value":100}}})", now);
    CHECK(bad_nominal.status == rateweaver::ParseStatus::Partial);
    CHECK(bad_nominal.rejected_entries == 1);
    CHECK(bad_nominal.quotes.size() == 1);
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
    CHECK(find(parsed, "EUR", "USD").published_at == day);
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

class FakeTransport final : public rateweaver::HttpTransport {
public:
    std::function<rateweaver::HttpResponse(const rateweaver::HttpRequest&, const rateweaver::CancellationToken&)> action;
    std::atomic<int> calls{0};
    rateweaver::HttpResponse get(const rateweaver::HttpRequest& request, const rateweaver::CancellationToken& token) override {
        ++calls;
        return action(request, token);
    }
};

rateweaver::Provider test_provider() {
    return {"test", "https://example.invalid/rates",
        [](std::string_view body, rateweaver::ts_ms_t retrieved) {
            rateweaver::ParseResult parsed;
            if (body == "throw") throw std::runtime_error("parser failure");
            if (body == "empty") { parsed.status = rateweaver::ParseStatus::NoData; return parsed; }
            if (body != "valid" && body != "partial") return parsed;
            parsed.status = body == "partial" ? rateweaver::ParseStatus::Partial : rateweaver::ParseStatus::Success;
            parsed.rejected_entries = body == "partial" ? 1 : 0;
            parsed.quotes.emplace_back("EUR", "USD", 1.25, "test", rateweaver::QuoteOrigin::Observed, day, retrieved);
            return parsed;
        }};
}

rateweaver::HttpResponse response(std::string body = "valid", int code = 200) {
    rateweaver::HttpResponse result;
    result.status_code = code;
    result.body = std::move(body);
    result.etag = "version-1";
    return result;
}

void refresh_workflow() {
    rateweaver::QuoteBook book;
    FakeTransport transport;
    rateweaver::RefreshOptions options;
    options.min_interval_ms = 10;
    options.max_body_bytes = 100;
    rateweaver::ts_ms_t clock = now;
    rateweaver::RefreshController refresh(book, transport, test_provider(), options, [&] { return clock; });
    const auto attempt_at = [&](rateweaver::ts_ms_t time) { clock = time; return refresh.refresh(); };
    transport.action = [&](const auto& request, const auto&) {
        CHECK(request.timeout_ms == options.timeout_ms);
        CHECK(request.max_body_bytes == 100);
        CHECK(request.if_none_match.empty());
        clock = now + 5;
        return response();
    };
    CHECK(attempt_at(now).status == rateweaver::RefreshStatus::Updated);
    const auto snapshot = book.snapshot();
    CHECK(snapshot.revision == 1);
    CHECK(snapshot.quotes.front().retrieved_at == now + 5);
    CHECK(attempt_at(now + 1).status == rateweaver::RefreshStatus::NotDue);
    CHECK(attempt_at(now - 1).status == rateweaver::RefreshStatus::NotDue);
    CHECK(transport.calls == 1);
    transport.action = [](const auto& request, const auto&) {
        CHECK(request.if_none_match == "version-1");
        return response("", 304);
    };
    CHECK(attempt_at(now + 10).status == rateweaver::RefreshStatus::NotModified);
    CHECK(book.snapshot().revision == 1);
    CHECK(book.snapshot().quotes.front().retrieved_at == now + 5);
    int attempt = 20;
    for (const auto& item : std::vector<std::pair<std::string, rateweaver::RefreshStatus>>{
            {"broken", rateweaver::RefreshStatus::ParseError},
            {"empty", rateweaver::RefreshStatus::NoData},
            {"partial", rateweaver::RefreshStatus::PartialData},
            {"throw", rateweaver::RefreshStatus::ParseError}}) {
        transport.action = [&](const auto&, const auto&) { return response(item.first); };
        CHECK(attempt_at(now + attempt).status == item.second);
        CHECK(book.snapshot().revision == 1);
        CHECK(attempt_at(now + attempt + 1).status == rateweaver::RefreshStatus::NotDue);
        attempt += 10;
    }
    transport.action = [](const auto&, const auto&) { auto r = response(); r.error = rateweaver::TransportError::Timeout; return r; };
    CHECK(attempt_at(now + attempt).status == rateweaver::RefreshStatus::Timeout);
    attempt += 10;
    transport.action = [](const auto&, const auto&) -> rateweaver::HttpResponse { throw std::runtime_error("network"); };
    CHECK(attempt_at(now + attempt).status == rateweaver::RefreshStatus::NetworkError);
    attempt += 10;
    transport.action = [](const auto&, const auto&) { return response("", 503); };
    CHECK(attempt_at(now + attempt).status == rateweaver::RefreshStatus::HttpError);
    attempt += 10;
    transport.action = [](const auto&, const auto&) { return response(std::string(101, 'x')); };
    CHECK(attempt_at(now + attempt).status == rateweaver::RefreshStatus::ResponseTooLarge);
    attempt += 10;
    transport.action = [&](const auto&, const auto& token) { refresh.cancel(); CHECK(token.is_cancelled()); return response(); };
    CHECK(attempt_at(now + attempt).status == rateweaver::RefreshStatus::Cancelled);
    CHECK(book.snapshot().revision == 1);
    attempt += 10;
    transport.action = [](const auto&, const auto&) { return response(); };
    CHECK(attempt_at(now + attempt).status == rateweaver::RefreshStatus::Updated);
    CHECK(book.snapshot().revision == 2);
    refresh.stop();
    CHECK(attempt_at(now + attempt + 10).status == rateweaver::RefreshStatus::Stopped);
}

void cancellation_and_overlap() {
    rateweaver::QuoteBook book;
    FakeTransport transport;
    std::promise<void> entered, released;
    auto release = released.get_future().share();
    auto entered_future = entered.get_future();
    transport.action = [&](const auto&, const auto&) { entered.set_value(); release.wait(); return response(); };
    rateweaver::RefreshController controller(book, transport, test_provider(), {}, [] { return now; });
    rateweaver::RefreshResult result;
    std::thread worker([&] { result = controller.refresh(); });
    entered_future.wait();
    const auto overlapping = controller.refresh();
    controller.stop();
    released.set_value();
    worker.join();
    CHECK(overlapping.status == rateweaver::RefreshStatus::Busy);
    CHECK(result.status == rateweaver::RefreshStatus::Cancelled || result.status == rateweaver::RefreshStatus::Stopped);
    CHECK(book.snapshot().quotes.empty());
    CHECK(controller.refresh().status == rateweaver::RefreshStatus::Stopped);
}

#if RATEWEAVER_TEST_JSON || RATEWEAVER_TEST_ECB
void feed_publication(const rateweaver::Provider& provider, const std::string& body) {
    rateweaver::QuoteBook book;
    FakeTransport transport;
    rateweaver::RefreshOptions options;
    options.min_interval_ms = 0;
    rateweaver::RefreshController controller(book, transport, provider, options, [] { return now; });
    transport.action = [&](const auto&, const auto&) { return response(body); };
    CHECK(controller.refresh().status == rateweaver::RefreshStatus::Updated);
    CHECK(book.snapshot_source(provider.source_id).revision == 1);
    CHECK(!book.snapshot_source(provider.source_id).quotes.empty());
    transport.action = [](const auto&, const auto&) { return response("<malformed"); };
    CHECK(controller.refresh().status == rateweaver::RefreshStatus::ParseError);
    CHECK(book.snapshot().revision == 1);
    transport.action = [](const auto&, const auto&) { auto r = response(); r.error = rateweaver::TransportError::Timeout; return r; };
    CHECK(controller.refresh().status == rateweaver::RefreshStatus::Timeout);
    CHECK(book.snapshot().revision == 1);
    transport.action = [](const auto&, const auto&) { auto r = response(); r.error = rateweaver::TransportError::Network; return r; };
    CHECK(controller.refresh().status == rateweaver::RefreshStatus::NetworkError);
    CHECK(book.snapshot().revision == 1);
}
#endif
}

int main() {
    try {
        timestamps();
#if RATEWEAVER_TEST_JSON
        json_providers();
        feed_publication(rateweaver::cdn_provider(), fixture("cdn.json"));
        feed_publication(rateweaver::cbr_provider(), fixture("cbr.json"));
#endif
#if RATEWEAVER_TEST_ECB
        ecb_provider();
        feed_publication(rateweaver::ecb_provider(), fixture("ecb.xml"));
#endif
        refresh_workflow();
        cancellation_and_overlap();
        std::cout << "providers/runtime: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
