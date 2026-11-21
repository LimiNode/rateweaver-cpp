#include <rateweaver/runtime/kurlyk_refresh.hpp>
#include <rateweaver/providers/cdn.hpp>
#include <rateweaver/providers/cbr.hpp>
#if RATEWEAVER_TEST_ECB
#include <rateweaver/providers/ecb.hpp>
#endif

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using namespace rateweaver;
using namespace std::chrono_literals;
int checks = 0;
constexpr ts_ms_t stamp = 1704196800000LL;

void check(bool value, const char* expression, int line) {
    ++checks;
    if (!value) throw std::runtime_error(std::string("line ") + std::to_string(line) + ": " + expression);
}
#define CHECK(value) check(static_cast<bool>(value), #value, __LINE__)

void configure(kurlyk::HttpClient& client) {
    client.set_timeout(5);
    client.set_connect_timeout(2);
    client.set_retry_attempts(1, 0);
    // Tests bypass any machine-local HTTP proxy for the loopback fixture.
    client.set_proxy_server("");
}

Provider endpoint(const std::string& base, const std::string& path, Provider provider = cdn_provider()) {
    provider.url = base + path;
    return provider;
}

RefreshResult finish(KurlykRefresh& refresh) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    do {
        if (auto result = refresh.poll()) return *result;
        std::this_thread::sleep_for(1ms);
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("refresh completion timeout");
}

std::string control(kurlyk::HttpClient& client, const std::string& path) {
    auto promise = std::make_shared<std::promise<kurlyk::HttpResponsePtr>>();
    auto future = promise->get_future();
    CHECK(client.get(path, {}, {}, [promise](kurlyk::HttpResponsePtr response) {
        if (response->ready) promise->set_value(std::move(response));
    }));
    CHECK(future.wait_for(5s) == std::future_status::ready);
    auto response = future.get();
    CHECK(!response->error_code);
    CHECK(response->status_code == 200);
    return response->content;
}

void requested(kurlyk::HttpClient& client, const std::string& name) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (control(client, "/state/" + name) != "1") {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("server gate not entered");
        std::this_thread::sleep_for(1ms);
    }
}

void disconnected(kurlyk::HttpClient& client, const std::string& name) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (control(client, "/disconnected/" + name) != "1") {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("native TCP cancellation not observed");
        std::this_thread::sleep_for(1ms);
    }
}

void success_and_freshness(const std::string& base) {
    QuoteBook book;
    std::atomic<ts_ms_t> utc{stamp};
    ts_ms_t tick = 1000;
    kurlyk::HttpClient client;
    configure(client);
    KurlykRefresh refresh(book, client, endpoint(base, "/etag"), {100, 4096, false},
        [&] { return utc.load(); }, [&] { return tick; });
    CHECK(refresh.start() == RefreshStatus::Started);
    CHECK(refresh.start() == RefreshStatus::Busy);
    CHECK(finish(refresh).status == RefreshStatus::Updated);
    const auto published = book.snapshot();
    CHECK(published.revision == 1);
    CHECK(published.quotes.size() == 2);
    CHECK(!published.quotes[0].published_at);
    CHECK(published.quotes[0].effective_at == 1704153600000LL);
    CHECK(published.quotes[0].retrieved_at == stamp);
    CHECK(refresh.start() == RefreshStatus::NotDue);
    // Wall-clock changes affect retrieval metadata, never refresh scheduling.
    utc.store(stamp - 100000);
    CHECK(refresh.start() == RefreshStatus::NotDue);
    tick += 99;
    CHECK(refresh.start() == RefreshStatus::NotDue);
    ++tick;
    CHECK(refresh.start() == RefreshStatus::Started);
    CHECK(finish(refresh).status == RefreshStatus::NotModified);
    CHECK(book.snapshot().revision == published.revision);
    CHECK(book.snapshot().quotes[0].retrieved_at == stamp);
    CHECK(book.snapshot().quotes[0].effective_at == published.quotes[0].effective_at);
    utc.store(stamp + 86400000);
    CHECK(refresh.start() == RefreshStatus::NotDue);
    tick += 100;
    CHECK(refresh.start() == RefreshStatus::Started);
    CHECK(finish(refresh).status == RefreshStatus::NotModified);
    tick += 100;
    CHECK(refresh.start() == RefreshStatus::Started);
    CHECK(finish(refresh).status == RefreshStatus::NotModified);
    CHECK(book.snapshot().revision == published.revision);
    CHECK(!refresh.busy());
    CHECK(!refresh.poll());
}

void chunked_and_completion_time(const std::string& base) {
    QuoteBook book;
    kurlyk::HttpClient client;
    configure(client);
    struct ClockProbe {
        std::atomic<ts_ms_t> now{stamp};
        std::atomic<int> calls{0};
        std::promise<void> sampled;
    };
    const auto clock = std::make_shared<ClockProbe>();
    auto sampled = clock->sampled.get_future();
    auto provider = endpoint(base, "/chunked");
    const auto parse = provider.parse;
    int parse_calls = 0;
    provider.parse = [&](std::string_view body, ts_ms_t at) {
        ++parse_calls;
        return parse(body, at);
    };
    KurlykRefresh refresh(book, client, provider, {0, 4096, false}, [clock] {
        const auto value = clock->now.load();
        if (clock->calls.fetch_add(1) == 0) clock->sampled.set_value();
        return value;
    });
    CHECK(refresh.start() == RefreshStatus::Started);
    // Observe the completion sample directly; a native queue/idle observation
    // must not substitute for this timestamp boundary.
    CHECK(sampled.wait_for(5s) == std::future_status::ready);
    CHECK(parse_calls == 0);
    clock->now.store(stamp + 50000);
    CHECK(finish(refresh).status == RefreshStatus::Updated);
    CHECK(parse_calls == 1);
    CHECK(book.snapshot().quotes.size() == 2);
    CHECK(book.snapshot().quotes[0].retrieved_at == stamp);
}

void refresh_clock_failures(const std::string& base) {
    QuoteBook book;
    std::atomic<ts_ms_t> utc{stamp};
    ts_ms_t tick = 2000;
    bool throw_tick = true;
    const auto host_thread = std::this_thread::get_id();
    kurlyk::HttpClient client;
    configure(client);
    KurlykRefresh refresh(book, client, endpoint(base, "/error"), {100, 4096, false},
        [&] { return utc.load(); }, [&] {
            CHECK(std::this_thread::get_id() == host_thread);
            if (throw_tick) throw std::runtime_error("monotonic clock failure");
            return tick;
        });
    bool threw = false;
    try { refresh.start(); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
    CHECK(!refresh.busy());
    throw_tick = false;
    CHECK(refresh.start() == RefreshStatus::Started);
    CHECK(finish(refresh).status == RefreshStatus::HttpError);
    CHECK(refresh.start() == RefreshStatus::NotDue);
    utc.store(stamp - 86400000);
    tick += 99;
    CHECK(refresh.start() == RefreshStatus::NotDue);
    utc.store(stamp + 86400000);
    ++tick;
    CHECK(refresh.start() == RefreshStatus::Started);
    CHECK(finish(refresh).status == RefreshStatus::HttpError);
    CHECK(book.snapshot().revision == 0);

    for (int mode = 0; mode != 2; ++mode) {
        kurlyk::HttpClient reentrant_client;
        configure(reentrant_client);
        std::unique_ptr<KurlykRefresh> reentrant;
        reentrant = std::make_unique<KurlykRefresh>(book, reentrant_client,
            endpoint(base, "/cdn"), RefreshOptions{0, 4096, false}, [] { return stamp; }, [&] {
                if (mode == 0) reentrant->cancel();
                else reentrant->stop();
                return tick;
            });
        CHECK(reentrant->start() == (mode == 0 ? RefreshStatus::Cancelled : RefreshStatus::Stopped));
        CHECK(!reentrant->busy());
    }

    // The client drains before this owner is destroyed, even if a check throws.
    std::unique_ptr<KurlykRefresh> completion;
    kurlyk::HttpClient completion_client;
    configure(completion_client);
    completion = std::make_unique<KurlykRefresh>(book, completion_client,
        endpoint(base, "/cdn"), RefreshOptions{0, 4096, false}, [&] {
            completion->cancel();
            return stamp;
        });
    CHECK(completion->start() == RefreshStatus::Started);
    CHECK(finish(*completion).status == RefreshStatus::Cancelled);
    CHECK(book.snapshot().revision == 0);

    kurlyk::HttpClient invalid_client;
    configure(invalid_client);
    KurlykRefresh invalid_utc(book, invalid_client, endpoint(base, "/cdn"), {}, {});
    CHECK(invalid_utc.start() == RefreshStatus::InvalidOptions);
    KurlykRefresh invalid_tick(book, invalid_client, endpoint(base, "/cdn"), {}, [] { return stamp; }, {});
    CHECK(invalid_tick.start() == RefreshStatus::InvalidOptions);
}

void errors_preserve_book(const std::string& base) {
    QuoteBook book;
    CHECK(book.replace_source("cdn:usd", {{"USD", "EUR", 0.7, "cdn:usd", QuoteOrigin::Observed, stamp, stamp}})
          == QuotePublishStatus::Success);
    const auto revision = book.snapshot().revision;
    const std::pair<const char*, RefreshStatus> cases[] = {
        {"/malformed", RefreshStatus::ParseError}, {"/empty", RefreshStatus::NoData},
        {"/partial", RefreshStatus::PartialData}, {"/error", RefreshStatus::HttpError},
        {"/error500", RefreshStatus::HttpError}, {"/missing", RefreshStatus::HttpError},
        {"/rawempty", RefreshStatus::ParseError},
        {"/unsolicited304", RefreshStatus::HttpError}
    };
    for (const auto& item : cases) {
        kurlyk::HttpClient client;
        configure(client);
        KurlykRefresh refresh(book, client, endpoint(base, item.first), {0, 4096, false}, [] { return stamp; });
        CHECK(refresh.start() == RefreshStatus::Started);
        const auto result = finish(refresh);
        CHECK(result.status == item.second);
        CHECK(book.snapshot().revision == revision);
        CHECK(book.snapshot().quotes[0].rate == 0.7);
    }
    kurlyk::HttpClient partial_client;
    configure(partial_client);
    KurlykRefresh partial(book, partial_client, endpoint(base, "/partial"), {0, 4096, true}, [] { return stamp; });
    CHECK(partial.start() == RefreshStatus::Started);
    const auto result = finish(partial);
    CHECK(result.status == RefreshStatus::Updated);
    CHECK(result.rejected_entries == 1);
    CHECK(book.snapshot().quotes.size() == 1);
    CHECK(book.snapshot().quotes[0].rate == 0.8);
#if RATEWEAVER_TEST_ECB
    kurlyk::HttpClient xml_client;
    configure(xml_client);
    const auto xml_revision = book.snapshot().revision;
    KurlykRefresh xml(book, xml_client, endpoint(base, "/malformedxml", ecb_provider()), {0, 4096, false}, [] { return stamp; });
    CHECK(xml.start() == RefreshStatus::Started);
    CHECK(finish(xml).status == RefreshStatus::ParseError);
    CHECK(book.snapshot().revision == xml_revision);
#endif
}

void native_retry_and_headers(const std::string& base) {
    QuoteBook book;
    kurlyk::HttpClient client;
    configure(client);
    client.set_retry_attempts(2, 1);
    KurlykRefresh refresh(book, client, endpoint(base, "/retry"), {0, 4096, false}, [] { return stamp; });
    CHECK(refresh.start() == RefreshStatus::Started);
    const auto result = finish(refresh);
    CHECK(result.status == RefreshStatus::Updated);
    CHECK(result.attempts == 2);
    CHECK(book.snapshot().revision == 1);

    kurlyk::HttpClient host_client;
    configure(host_client);
    host_client.set_headers({{"X-Host-Setting", "preserved"}});
    KurlykRefresh configured(book, host_client, endpoint(base, "/headers"), {0, 4096, false}, [] { return stamp; });
    CHECK(configured.start() == RefreshStatus::Started);
    CHECK(finish(configured).status == RefreshStatus::Updated);
}

void timeout_cancel_and_overflow(const std::string& base, kurlyk::HttpClient& commands) {
    QuoteBook book;
    {
        kurlyk::HttpClient client;
        configure(client);
        KurlykRefresh refresh(book, client, endpoint(base, "/gate/fence-only"), {0, 4096, false}, [] { return stamp; });
        CHECK(refresh.start() == RefreshStatus::Started);
        requested(commands, "fence-only");
        refresh.cancel();
        CHECK(refresh.busy());
        CHECK(!refresh.poll());
        control(commands, "/release/fence-only");
        CHECK(finish(refresh).status == RefreshStatus::Cancelled);
        CHECK(book.snapshot().revision == 0);
    }
    {
        kurlyk::HttpClient client;
        configure(client);
        client.set_timeout(1);
        KurlykRefresh refresh(book, client, endpoint(base, "/gate/timeout"), {0, 4096, false}, [] { return stamp; });
        CHECK(refresh.start() == RefreshStatus::Started);
        requested(commands, "timeout");
        CHECK(finish(refresh).status == RefreshStatus::Timeout);
        disconnected(commands, "timeout");
        CHECK(book.snapshot().revision == 0);
        control(commands, "/release/timeout");
    }
    {
        kurlyk::HttpClient client;
        configure(client);
        KurlykRefresh refresh(book, client, endpoint(base, "/gate/cancel"), {0, 4096, false}, [] { return stamp; });
        CHECK(refresh.start() == RefreshStatus::Started);
        requested(commands, "cancel");
        refresh.cancel();
        CHECK(refresh.busy());
        CHECK(refresh.start() == RefreshStatus::Busy);
        client.cancel_requests();
        disconnected(commands, "cancel");
        CHECK(finish(refresh).status == RefreshStatus::Cancelled);
        CHECK(!refresh.busy());
        CHECK(book.snapshot().revision == 0);
        control(commands, "/release/cancel");
        CHECK(refresh.start() == RefreshStatus::Started);
        CHECK(finish(refresh).status == RefreshStatus::Updated);
        CHECK(book.snapshot().revision == 1);
    }
    {
        kurlyk::HttpClient client;
        configure(client);
        KurlykRefresh refresh(book, client, endpoint(base, "/stream/overflow"), {0, 128, false}, [] { return stamp; });
        CHECK(refresh.start() == RefreshStatus::Started);
        requested(commands, "overflow");
        CHECK(finish(refresh).status == RefreshStatus::ResponseTooLarge);
        CHECK(refresh.busy());
        CHECK(refresh.start() == RefreshStatus::Busy);
        client.cancel_requests();
        disconnected(commands, "overflow");
        CHECK(!refresh.poll());
        CHECK(!refresh.busy());
        CHECK(book.snapshot().revision == 1);
        control(commands, "/release/overflow");
    }
}

void concurrent_providers(const std::string& base) {
    QuoteBook book;
    kurlyk::HttpClient cdn_client, cbr_client;
    configure(cdn_client);
    configure(cbr_client);
    KurlykRefresh cdn(book, cdn_client, endpoint(base, "/cdn"), {0, 4096, false}, [] { return stamp; });
    KurlykRefresh cbr(book, cbr_client, endpoint(base, "/cbr", cbr_provider()), {0, 4096, false}, [] { return stamp; });
    CHECK(cdn.start() == RefreshStatus::Started);
    CHECK(cbr.start() == RefreshStatus::Started);
    CHECK(finish(cdn).status == RefreshStatus::Updated);
    CHECK(finish(cbr).status == RefreshStatus::Updated);
    CHECK(book.snapshot().revision == 2);
    CHECK(book.snapshot_source("cdn:usd").quotes.size() == 2);
    CHECK(book.snapshot_source(cbr_provider().source_id).quotes.size() == 1);
#if RATEWEAVER_TEST_ECB
    kurlyk::HttpClient ecb_client;
    configure(ecb_client);
    KurlykRefresh ecb(book, ecb_client, endpoint(base, "/ecb", ecb_provider()), {0, 4096, false}, [] { return stamp; });
    CHECK(ecb.start() == RefreshStatus::Started);
    CHECK(finish(ecb).status == RefreshStatus::Updated);
    CHECK(book.snapshot_source(ecb_provider().source_id).quotes.size() == 1);
#endif
}

void parser_boundary(const std::string& base) {
    for (int mode = 0; mode != 5; ++mode) {
        QuoteBook book;
        kurlyk::HttpClient client;
        configure(client);
        std::unique_ptr<KurlykRefresh> refresh;
        auto provider = endpoint(base, "/cdn");
        const auto parse = provider.parse;
        provider.parse = [&, parse](std::string_view body, ts_ms_t now) {
            if (mode == 0) refresh->cancel();
            if (mode == 1) refresh->stop();
            if (mode == 2) { CHECK(refresh->start() == RefreshStatus::Busy); refresh.reset(); }
            if (mode == 3) throw std::runtime_error("test parser failure");
            auto parsed = parse(body, now);
            if (mode == 4) parsed.quotes[0].source_id = "wrong-source";
            return parsed;
        };
        refresh = std::make_unique<KurlykRefresh>(book, client, provider, RefreshOptions{0, 4096, false}, [] { return stamp; });
        auto* pending = refresh.get();
        CHECK(pending->start() == RefreshStatus::Started);
        const auto result = finish(*pending);
        const RefreshStatus expected[] = {RefreshStatus::Cancelled, RefreshStatus::Stopped,
            RefreshStatus::Stopped, RefreshStatus::ParseError, RefreshStatus::PublicationRejected};
        CHECK(result.status == expected[mode]);
        CHECK(book.snapshot().revision == 0);
    }
    for (const char* message : {"test completion clock failure", ""}) {
        QuoteBook book;
        CHECK(book.replace_source("cdn:usd", {{"USD", "EUR", 0.7, "cdn:usd"}}) == QuotePublishStatus::Success);
        kurlyk::HttpClient client;
        configure(client);
        KurlykRefresh refresh(book, client, endpoint(base, "/cdn"), {0, 4096, false}, [message]() -> ts_ms_t {
            throw std::runtime_error(message);
        });
        CHECK(refresh.start() == RefreshStatus::Started);
        CHECK(finish(refresh).status == RefreshStatus::ParseError);
        CHECK(book.snapshot().revision == 1);
        CHECK(book.snapshot().quotes[0].rate == 0.7);
    }
}

void destruction_while_pending(const std::string& base, kurlyk::HttpClient& commands) {
    auto book = std::make_unique<QuoteBook>();
    kurlyk::HttpClient client;
    configure(client);
    auto refresh = std::make_unique<KurlykRefresh>(*book, client, endpoint(base, "/gate/destruction"),
        RefreshOptions{0, 4096, false}, [] { return stamp; });
    CHECK(refresh->start() == RefreshStatus::Started);
    requested(commands, "destruction");
    refresh.reset();
    book.reset();
    control(commands, "/release/destruction");
    // Native callbacks retain only fenced operation state, not the destroyed book.
    client.wait_requests();

    QuoteBook surviving_book;
    auto owned_client = std::make_unique<kurlyk::HttpClient>();
    configure(*owned_client);
    auto owned_refresh = std::make_unique<KurlykRefresh>(surviving_book, *owned_client,
        endpoint(base, "/gate/client-destruction"), RefreshOptions{0, 4096, false}, [] { return stamp; });
    CHECK(owned_refresh->start() == RefreshStatus::Started);
    requested(commands, "client-destruction");
    owned_refresh.reset();
    // The native client's host-thread destructor actively cancels the pending
    // operation; callback-owned fenced state survives both public objects.
    owned_client.reset();
    disconnected(commands, "client-destruction");
    CHECK(surviving_book.snapshot().revision == 0);
}

void native_rate_limits(const std::string& base, kurlyk::HttpClient& commands) {
    QuoteBook book;
    kurlyk::HttpClient first, second;
    configure(first);
    configure(second);
    auto limit = kurlyk::HttpRequestManager::get_instance().create_rate_limit(1, 1, true);
    CHECK(first.assign_rate_limit_handle(limit));
    CHECK(second.assign_rate_limit_handle(limit));
    KurlykRefresh one(book, first, endpoint(base, "/gate/shared-limit"), {0, 4096, false}, [] { return stamp; });
    KurlykRefresh two(book, second, endpoint(base, "/shared-second"), {0, 4096, false}, [] { return stamp; });
    first.set_headers({{"X-Host-Setting", "preserved"}});
    second.set_headers({{"X-Host-Setting", "preserved"}});
    CHECK(one.start() == RefreshStatus::Started);
    requested(commands, "shared-limit");
    CHECK(two.start() == RefreshStatus::Started);
    // Give the native worker time to process both submissions. The server must
    // not receive the second request while the first sequential lease is held.
    std::this_thread::sleep_for(100ms);
    CHECK(control(commands, "/count/shared-second") == "0");
    CHECK(!two.poll());
    CHECK(two.busy());
    CHECK(second.in_flight_requests() == 1);
    control(commands, "/release/shared-limit");
    CHECK(finish(one).status == RefreshStatus::Updated);
    CHECK(finish(two).status == RefreshStatus::Updated);
    CHECK(control(commands, "/count/shared-second") == "1");
    CHECK(two.start() == RefreshStatus::Started);
    CHECK(finish(two).status == RefreshStatus::Updated);
    CHECK(kurlyk::HttpRequestManager::get_instance().get_rate_limit(limit->id()) == limit);
    kurlyk::HttpRequestManager::get_instance().remove_limit(limit);
}

void owned_rate_limit_persists(const std::string& base) {
    QuoteBook book;
    kurlyk::HttpClient client;
    configure(client);
    // Configure once: each refresh must inherit this same native limiter.
    client.set_rate_limit(1, 500);
    KurlykRefresh refresh(book, client, endpoint(base, "/ownedlimit"), {0, 4096, false}, [] { return stamp; });
    const auto started = std::chrono::steady_clock::now();
    for (int attempt = 0; attempt != 3; ++attempt) {
        CHECK(refresh.start() == RefreshStatus::Started);
        CHECK(finish(refresh).status == RefreshStatus::Updated);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    CHECK(book.snapshot().revision == 3);
    CHECK(elapsed >= 900);
    std::cout << "Owned native limit: three refreshes, period 500 ms, elapsed " << elapsed << " ms\n";
}

void native_group_accounting(const std::string& base, kurlyk::HttpClient& commands) {
    std::atomic<int> completed{0};
    std::atomic<bool> failed{false};
    kurlyk::HttpClient client(base);
    configure(client);
    const auto observe = [&](kurlyk::HttpResponsePtr response) {
        if (!response->ready) return;
        if (response->error_code || response->status_code != 200) failed.store(true);
        completed.fetch_add(1);
    };
    // A wait immediately after admission must include pending-to-active handoff.
    for (int attempt = 0; attempt != 32; ++attempt) {
        CHECK(client.get("/cdn", {}, {}, observe));
        CHECK(client.wait_requests_for(5s));
        CHECK(completed.load() == attempt + 1);
        CHECK(client.in_flight_requests() == 0);
    }
    CHECK(!failed.load());
    completed.store(0);
    constexpr int batch = 8;
    for (int attempt = 0; attempt != batch; ++attempt) {
        CHECK(client.get("/gate/group-accounting", {}, {}, observe));
        CHECK(client.in_flight_requests() == static_cast<std::size_t>(attempt + 1));
    }
    requested(commands, "group-accounting");
    for (int sample = 0; sample != 1000; ++sample) {
        CHECK(client.in_flight_requests() == batch);
        std::this_thread::yield();
    }
    CHECK(!client.wait_requests_for(1ms));
    control(commands, "/release/group-accounting");
    CHECK(client.wait_requests_for(5s));
    CHECK(completed.load() == batch);
    CHECK(client.in_flight_requests() == 0);
    CHECK(!failed.load());

    // Cancellation must also deliver every final callback before its barrier.
    completed.store(0);
    for (int attempt = 0; attempt != 32; ++attempt) {
        CHECK(client.get("/cdn", {}, {}, [&](kurlyk::HttpResponsePtr response) {
            if (response->ready) completed.fetch_add(1);
        }));
        client.cancel_requests();
        CHECK(completed.load() == attempt + 1);
        CHECK(client.in_flight_requests() == 0);
    }
}

void shutdown_with_inflight(const std::string& base, kurlyk::HttpClient& commands) {
    QuoteBook book;
    kurlyk::HttpClient client;
    configure(client);
    KurlykRefresh refresh(book, client, endpoint(base, "/gate/shutdown"), {0, 4096, false}, [] { return stamp; });
    CHECK(refresh.start() == RefreshStatus::Started);
    requested(commands, "shutdown");
    refresh.stop();
    // Stop joins the worker; its own final pass shuts down native managers.
    kurlyk::deinit();
    CHECK(finish(refresh).status == RefreshStatus::Stopped);
    CHECK(!refresh.busy());
    CHECK(refresh.start() == RefreshStatus::Stopped);
    CHECK(book.snapshot().revision == 0);
}

void manual_processing(const std::string& base) {
    kurlyk::init(false);
    {
        QuoteBook book;
        const auto host_thread = std::this_thread::get_id();
        bool completion_on_host = false;
        kurlyk::HttpClient client;
        configure(client);
        KurlykRefresh refresh(book, client, endpoint(base, "/cdn"), {0, 4096, false}, [&] {
            completion_on_host = std::this_thread::get_id() == host_thread;
            return stamp;
        });
        CHECK(refresh.start() == RefreshStatus::Started);
        std::optional<RefreshResult> result;
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!result && std::chrono::steady_clock::now() < deadline) {
            kurlyk::process();
            result = refresh.poll();
            if (!result) std::this_thread::sleep_for(1ms);
        }
        CHECK(result);
        CHECK(result->status == RefreshStatus::Updated);
        CHECK(completion_on_host);
        CHECK(book.snapshot().revision == 1);
        CHECK(client.in_flight_requests() == 0);
        // In manual mode drain via process() before native client destruction:
        // its cancellation barrier cannot rely on a background worker.
    }
    kurlyk::deinit();
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2 && argc != 3) throw std::runtime_error("expected loopback server URL [manual]");
        const std::string base = argv[1];
        if (argc == 3) {
            CHECK(std::string(argv[2]) == "manual");
            manual_processing(base);
            std::cout << checks << " native manual HTTP checks passed\n";
            return 0;
        }
        kurlyk::init(true);
        {
            kurlyk::HttpClient commands(base);
            configure(commands);
            success_and_freshness(base);
            chunked_and_completion_time(base);
            refresh_clock_failures(base);
            errors_preserve_book(base);
            native_retry_and_headers(base);
            timeout_cancel_and_overflow(base, commands);
            concurrent_providers(base);
            parser_boundary(base);
            destruction_while_pending(base, commands);
            native_rate_limits(base, commands);
            owned_rate_limit_persists(base);
            native_group_accounting(base, commands);
            shutdown_with_inflight(base, commands);
        }
        kurlyk::deinit();
        std::cout << checks << " native HTTP checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        kurlyk::deinit();
        return 1;
    }
}
