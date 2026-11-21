#include <rateweaver/providers/cdn.hpp>
#include <rateweaver/runtime.hpp>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace {
int run(const std::string& feed_url) {
    rateweaver::QuoteBook book;
    auto provider = rateweaver::cdn_provider("USD");
    // An optional URL permits a local server with the same response schema.
    if (!feed_url.empty()) provider.url = feed_url;

    // This same native client, including its limits and defaults, survives both
    // refreshes. A real embedding host starts its existing runtime just once.
    kurlyk::HttpClient client;
    client.set_timeout(5);
    client.set_connect_timeout(3);
    client.set_retry_attempts(2, 250);
    client.set_rate_limit(2, 1000);
    client.set_headers({{"Accept", "application/json"}});

    rateweaver::RefreshOptions options;
    options.min_interval_ms = 1000; // Short demonstration cadence, not a daily-feed recommendation.
    rateweaver::KurlykRefresh refresh(book, client, provider, options);
    int completed = 0;
    bool pending = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
    while (completed != 2 && std::chrono::steady_clock::now() < deadline) {
        if (!pending) {
            const auto started = refresh.start();
            if (started == rateweaver::RefreshStatus::Started) pending = true;
            else if (started != rateweaver::RefreshStatus::NotDue) {
                std::cerr << "Refresh was not admitted: " << static_cast<int>(started) << '\n';
                return 1;
            }
        }
        if (const auto result = refresh.poll()) {
            if (result->status == rateweaver::RefreshStatus::ResponseTooLarge) {
                refresh.stop();
                client.cancel_requests(); // Host-thread native cancellation barrier.
                std::cerr << "Feed exceeded the accepted-body limit\n";
                return 1;
            }
            if (!*result) {
                std::cerr << "Refresh unavailable: " << result->diagnostic << '\n';
                return 1; // Previous book data remains available with its original age.
            }
            pending = false;
            ++completed;
            const auto snapshot = book.snapshot();
            std::cout << "refresh=" << completed << " HTTP=" << result->http_status
                      << " quotes=" << snapshot.quotes.size()
                      << " revision=" << snapshot.revision << '\n';
            // Updated replaces the complete source batch; NotModified preserves
            // both revision and timestamps. Conversion still needs an age policy.
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    refresh.stop();
    client.cancel_requests();
    return completed == 2 ? 0 : 1;
}
} // namespace

int main(int argc, char** argv) {
    kurlyk::init(true);
    int result = 1;
    try { result = run(argc > 1 ? argv[1] : ""); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    kurlyk::deinit(); // The host owns global teardown after run's clients are gone.
    return result;
}
