#include <rateweaver/runtime.hpp>

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace rateweaver;
constexpr ts_ms_t now = 1700000000000LL;
int checks = 0;
void check(bool passed, const char* expression, int line) {
    ++checks;
    if (!passed) throw std::runtime_error(std::to_string(line) + ": " + expression);
}
#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

ParseResult good_parse(std::string_view, ts_ms_t retrieved) {
    return {ParseStatus::Success,
        {{"USD", "EUR", 2, "feed", QuoteOrigin::Observed, now, retrieved}}, 0, {}};
}
Provider feed() { return {"feed", "offline://fixture", good_parse}; }
HttpResponse response() { return {200, "fixture", "v1", TransportError::None, {}}; }
RefreshOptions immediate() { RefreshOptions result; result.min_interval_ms = 0; return result; }
struct Transport final : HttpTransport {
    std::function<HttpResponse(const HttpRequest&, const CancellationToken&)> action =
        [](const HttpRequest&, const CancellationToken&) { return response(); };
    HttpResponse get(const HttpRequest& request, const CancellationToken& token) override {
        return action(request, token);
    }
};
void seed(QuoteBook& book) {
    CHECK(book.replace_source("feed", {{"USD", "EUR", 3, "feed", QuoteOrigin::Observed, now, now},
        {"USD", "GBP", 4, "feed", QuoteOrigin::Observed, now, now}}) == QuotePublishStatus::Success);
}

void transport_reentrancy() {
    QuoteBook book; seed(book);
    Transport transport;
    RefreshController* controller = nullptr;
    RefreshStatus reentered = RefreshStatus::NoData;
    bool parser_called = false;
    auto provider = feed();
    provider.parse = [&](std::string_view body, ts_ms_t at) { parser_called = true; return good_parse(body, at); };
    transport.action = [&](const HttpRequest&, const CancellationToken&) {
        reentered = controller->refresh().status;
        controller->stop();
        return response();
    };
    RefreshController refresh(book, transport, provider, immediate(), [] { return now; });
    controller = &refresh;
    CHECK(refresh.refresh().status == RefreshStatus::Stopped);
    CHECK(reentered == RefreshStatus::Busy && !parser_called);
    CHECK(book.snapshot().revision == 1 && book.snapshot().quotes[0].rate == 3);
    CHECK(refresh.refresh().status == RefreshStatus::Stopped);
}

void parser_reentrancy() {
    QuoteBook book;
    Transport transport;
    RefreshController* controller = nullptr;
    auto provider = feed();
    provider.parse = [&](std::string_view body, ts_ms_t at) {
        controller->stop();
        return good_parse(body, at);
    };
    RefreshController refresh(book, transport, provider, immediate(), [] { return now; });
    controller = &refresh;
    CHECK(refresh.refresh().status == RefreshStatus::Stopped);
    CHECK(book.snapshot().revision == 0 && book.snapshot().quotes.empty());
}

void publication_rejection() {
    QuoteBook book; seed(book);
    Transport transport;
    auto provider = feed();
    provider.parse = [](std::string_view body, ts_ms_t at) {
        auto parsed = good_parse(body, at);
        parsed.quotes[0].source_id = "wrong-source";
        return parsed;
    };
    RefreshController refresh(book, transport, provider, immediate(), [] { return now; });
    CHECK(refresh.refresh().status == RefreshStatus::PublicationRejected);
    const auto snapshot = book.snapshot();
    CHECK(snapshot.revision == 1 && snapshot.quotes.size() == 2 && snapshot.quotes[0].rate == 3);
}

void clock_failure_releases_operation() {
    QuoteBook book;
    Transport transport;
    int calls = 0;
    auto clock = [&]() -> ts_ms_t {
        if (++calls == 2) throw std::runtime_error("completion clock unavailable");
        return now;
    };
    RefreshController refresh(book, transport, feed(), immediate(), clock);
    bool threw = false;
    try { refresh.refresh(); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw && book.snapshot().revision == 0);
    CHECK(refresh.refresh().status == RefreshStatus::Updated);
    CHECK(calls == 4 && book.snapshot().revision == 1);
}

void unsolicited_not_modified() {
    QuoteBook book;
    Transport transport;
    transport.action = [](const HttpRequest&, const CancellationToken&) {
        auto result = response(); result.status_code = 304; result.body.clear(); return result;
    };
    RefreshController refresh(book, transport, feed(), immediate(), [] { return now; });
    CHECK(refresh.refresh().status == RefreshStatus::HttpError);
    CHECK(book.snapshot().revision == 0 && book.snapshot().quotes.empty());
}

void partial_publication_is_explicit() {
    QuoteBook book; seed(book);
    Transport transport;
    auto provider = feed();
    provider.parse = [](std::string_view body, ts_ms_t at) {
        auto parsed = good_parse(body, at); parsed.status = ParseStatus::Partial;
        parsed.rejected_entries = 1; parsed.diagnostic = "One entry was invalid"; return parsed;
    };
    RefreshController strict(book, transport, provider, immediate(), [] { return now; });
    const auto rejected = strict.refresh();
    CHECK(rejected.status == RefreshStatus::PartialData && rejected.rejected_entries == 1);
    CHECK(book.snapshot().revision == 1 && book.snapshot().quotes.size() == 2);
    auto options = immediate(); options.accept_partial = true;
    RefreshController permissive(book, transport, provider, options, [] { return now; });
    const auto accepted = permissive.refresh();
    CHECK(accepted.status == RefreshStatus::Updated && accepted.parse_status == ParseStatus::Partial);
    const auto snapshot = book.snapshot();
    CHECK(snapshot.revision == 2 && snapshot.quotes.size() == 1 && snapshot.quotes[0].rate == 2);
}

void completion_timestamp() {
    QuoteBook book;
    Transport transport;
    ts_ms_t clock = now;
    transport.action = [&](const HttpRequest&, const CancellationToken&) { clock += 250; return response(); };
    RefreshController refresh(book, transport, feed(), immediate(), [&] { return clock; });
    CHECK(refresh.refresh().status == RefreshStatus::Updated);
    const auto published = book.snapshot().quotes[0];
    CHECK(published.published_at == now);
    CHECK(published.retrieved_at == now + 250);
}
} // namespace

int main() {
    try {
        transport_reentrancy(); parser_reentrancy(); publication_rejection();
        clock_failure_releases_operation(); unsolicited_not_modified();
        partial_publication_is_explicit(); completion_timestamp();
        std::cout << "runtime edges: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "runtime edge failure: " << error.what() << '\n';
        return 1;
    }
}
