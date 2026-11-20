#include <rateweaver.hpp>
#include <rateweaver/providers.hpp>
#include <rateweaver/providers/ecb.hpp>
#include <rateweaver/runtime.hpp>

#include <chrono>
#include <algorithm>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
const char* status_name(rateweaver::RefreshStatus status) {
    using S = rateweaver::RefreshStatus;
    switch (status) {
    case S::Updated: return "Updated";
    case S::NotModified: return "NotModified";
    case S::NetworkError: return "NetworkError";
    case S::Timeout: return "Timeout";
    case S::HttpError: return "HttpError";
    case S::ResponseTooLarge: return "ResponseTooLarge";
    case S::ParseError: return "ParseError";
    case S::NoData: return "NoData";
    case S::PartialData: return "PartialData";
    case S::PublicationRejected: return "PublicationRejected";
    default: return "Other";
    }
}

bool convert(const rateweaver::QuoteBook& book, const std::string& from, const std::string& to,
             rateweaver::ts_ms_t reference_time) {
    rateweaver::QuoteGraph graph(book.snapshot());
    rateweaver::RouterOptions options;
    options.aggregation.reference_time = reference_time;
    // Daily reference feeds can retain Friday's publication over a long weekend.
    options.aggregation.max_age_ms = 7 * 86400000LL;
    const auto result = rateweaver::RateRouter(graph, options).convert(1, from, to);
    if (!result) return false;
    std::cout << "  conversion: 1 " << from << " = " << *result.amount << ' ' << to << '\n';
    for (const auto& step : result.route) {
        for (const auto& contributor : step.contributors) {
            std::cout << "  contributor=" << contributor.quote.source_id
                      << " origin=" << (contributor.quote.origin == rateweaver::QuoteOrigin::Observed ? "Observed" : "Synthetic")
                      << " usage=" << (contributor.usage == rateweaver::QuoteUsage::Normal ? "Normal" : "LastKnownFallback")
                      << " reversed=" << contributor.reversed << '\n';
        }
    }
    return true;
}

int run(const std::string& ca_file) {
    int skipped = 0;
    bool failed = false;
    const std::vector<rateweaver::Provider> providers = {
        rateweaver::cdn_provider("USD"), rateweaver::cbr_provider(), rateweaver::ecb_provider()};
    for (const auto& provider : providers) {
        rateweaver::QuoteBook book;
        // The host owns this persistent native client and the global runtime.
        kurlyk::HttpClient client;
        client.set_timeout(15);
        client.set_connect_timeout(5);
        client.set_retry_attempts(2, 250);
        client.set_rate_limit(2, 1000);
        client.set_user_agent("rateweaver-live-smoke/0.1");
        if (!ca_file.empty()) client.set_ca_file(ca_file);
        rateweaver::RefreshOptions options;
        options.min_interval_ms = 0;
        rateweaver::KurlykRefresh refresh(book, client, provider, options);
        if (refresh.start() != rateweaver::RefreshStatus::Started) return 1;
        std::optional<rateweaver::RefreshResult> result;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(40);
        while (!(result = refresh.poll()) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (!result) {
            refresh.stop();
            client.cancel_requests();
            std::cout << provider.source_id << " SKIP: external request deadline\n";
            ++skipped;
            continue;
        }
        std::cout << provider.source_id << " HTTP=" << result->http_status
                  << " refresh=" << status_name(result->status) << " attempts=" << result->attempts;
        if (result->native_error == kurlyk::utils::make_error_code(CURLE_SSL_CACERT_BADFILE) ||
            result->native_error == kurlyk::utils::make_error_code(CURLE_PEER_FAILED_VERIFICATION)) {
            std::cout << " FAIL: TLS trust configuration/verification: " << result->diagnostic << '\n';
            failed = true;
            continue;
        }
        if (result->status == rateweaver::RefreshStatus::NetworkError ||
            result->status == rateweaver::RefreshStatus::Timeout ||
            result->http_status >= 500 || result->http_status == 403 || result->http_status == 429) {
            std::cout << " SKIP: endpoint/network unavailable: " << result->diagnostic << '\n';
            ++skipped;
            continue;
        }
        const auto snapshot = book.snapshot();
        std::cout << " quotes=" << snapshot.quotes.size() << " revision=" << snapshot.revision << '\n';
        if (result->status != rateweaver::RefreshStatus::Updated || snapshot.quotes.empty()) {
            std::cerr << "  FAIL: " << result->diagnostic << '\n';
            failed = true;
            if (result->status == rateweaver::RefreshStatus::ResponseTooLarge) client.cancel_requests();
            continue;
        }
        const auto preferred = std::find_if(snapshot.quotes.begin(), snapshot.quotes.end(), [](const auto& quote) {
            return (quote.from == "USD" && quote.to == "EUR") ||
                   (quote.from == "USD" && quote.to == "RUB") ||
                   (quote.from == "EUR" && quote.to == "USD");
        });
        const auto& quote = preferred == snapshot.quotes.end() ? snapshot.quotes.front() : *preferred;
        std::cout << "  source_timestamp=" << quote.published_at.value_or(0)
                  << " retrieval_timestamp=" << quote.retrieved_at.value_or(0) << '\n';
        auto valuation_time = rateweaver::current_time_ms();
        if (quote.published_at && *quote.published_at > valuation_time) {
            rateweaver::QuoteGraph graph(snapshot);
            rateweaver::RouterOptions options;
            options.aggregation.reference_time = valuation_time;
            options.aggregation.max_age_ms = 7 * 86400000LL;
            const auto current = rateweaver::RateRouter(graph, options).convert(1, quote.from, quote.to);
            if (current) { failed = true; continue; }
            std::cout << "  current valuation unavailable: future source timestamp (expected)\n";
            valuation_time = *quote.published_at;
            std::cout << "  feed-date routing check only; valuation_timestamp=" << valuation_time << '\n';
        }
        failed = !convert(book, quote.from, quote.to, valuation_time) ||
                 !convert(book, quote.to, quote.from, valuation_time) || failed;
    }
    if (failed) return 1;
    return skipped ? 77 : 0;
}
} // namespace

int main(int argc, char** argv) {
    kurlyk::init(true);
    int result = 1;
    try { result = run(argc > 1 ? argv[1] : ""); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    kurlyk::deinit();
    return result;
}
