#pragma once

#include <rateweaver/core/quote_book.hpp>
#include <rateweaver/providers/common.hpp>
#include <kurlyk.hpp>

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace rateweaver {

struct RefreshOptions {
    ts_ms_t min_interval_ms = 60000;
    /// Maximum accepted body size; the pinned kurlyk still buffers internally.
    std::size_t max_body_bytes = 1024 * 1024;
    bool accept_partial = false;
};

enum class RefreshStatus {
    Started, Updated, NotModified, NotDue, Busy, Cancelled, Stopped,
    InvalidOptions, NetworkError, Timeout, HttpError, ResponseTooLarge,
    ParseError, NoData, PartialData, PublicationRejected
};

struct RefreshResult {
    RefreshStatus status = RefreshStatus::NoData;
    long http_status = 0;
    long attempts = 0;
    ParseStatus parse_status = ParseStatus::NoData;
    std::size_t rejected_entries = 0;
    std::error_code native_error;
    std::string diagnostic;

    explicit operator bool() const noexcept {
        return status == RefreshStatus::Updated || status == RefreshStatus::NotModified;
    }
};

/// Nonblocking refresh and host-thread publication through a dedicated native client.
/// Configure the client's headers, limits, retries and timeouts before construction.
/// Construction assigns the provider URL, streaming and valid 304 status once.
/// Keep the client and book alive through every host call; callbacks own only state.
/// Host calls start/poll serially. cancel/stop may also run concurrently with them.
/// The clock runs on host/start and native/final threads; its captured resources
/// must survive native callback drain, and it must not destroy/wait on the client.
/// Destroy the client on its owning host thread, never from a network callback.
/// See docs/kurlyk-limitations.md for pinned cancellation and buffering constraints.
class KurlykRefresh final {
public:
    KurlykRefresh(QuoteBook& book, kurlyk::HttpClient& client, Provider provider,
                  RefreshOptions options = {},
                  std::function<ts_ms_t()> clock = current_time_ms)
        : m_state(std::make_shared<State>(book, std::move(provider), options, std::move(clock))),
          m_client(client) {
        client.set_host(m_state->provider.url);
        client.set_streaming(true);
        client.add_valid_status(304);
    }

    KurlykRefresh(const KurlykRefresh&) = delete;
    KurlykRefresh& operator=(const KurlykRefresh&) = delete;
    ~KurlykRefresh() { stop(); }

    /// Enqueues a native request without waiting for HTTP completion.
    RefreshStatus start() {
        const auto state = m_state;
        auto& client = m_client;
        auto operation = std::make_shared<Operation>();
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->stopped) return RefreshStatus::Stopped;
            if (state->current) return RefreshStatus::Busy;
            if (state->provider.source_id.empty() || state->provider.url.empty() ||
                !state->provider.parse || !state->clock || state->options.min_interval_ms < 0 ||
                state->options.max_body_bytes == 0) return RefreshStatus::InvalidOptions;
            state->current = operation;
        }
        ExceptionCleanup cleanup{state, operation};
        const auto started_at = state->clock();
        kurlyk::Headers headers;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->stopped || operation->cancelled) {
                state->current.reset();
                return state->stopped ? RefreshStatus::Stopped : RefreshStatus::Cancelled;
            }
            if (state->last_attempt && (started_at < *state->last_attempt ||
                static_cast<std::uint64_t>(started_at) - static_cast<std::uint64_t>(*state->last_attempt) <
                    static_cast<std::uint64_t>(state->options.min_interval_ms))) {
                state->current.reset();
                return RefreshStatus::NotDue;
            }
            state->last_attempt = started_at;
            operation->etag = state->etag;
            if (!operation->etag.empty()) headers.emplace("If-None-Match", operation->etag);
        }
        // Network callbacks never access the book, client or KurlykRefresh object.
        try {
            const bool accepted = client.get("", {}, headers,
                [state, operation](kurlyk::HttpResponsePtr response) {
                    receive(state, operation, std::move(response));
                });
            if (!accepted) {
                std::lock_guard<std::mutex> lock(state->mutex);
                operation->ready = true;
                operation->submission_error = "kurlyk rejected request admission";
            }
        } catch (const std::exception& error) {
            std::lock_guard<std::mutex> lock(state->mutex);
            operation->ready = true;
            operation->submission_error = error.what();
        } catch (...) {
            std::lock_guard<std::mutex> lock(state->mutex);
            operation->ready = true;
            operation->submission_error = "kurlyk request submission failed";
        }
        return RefreshStatus::Started;
    }

    /// Takes one available result and publishes on the caller's thread.
    /// An early ResponseTooLarge asks the host to cancel the dedicated client.
    /// Busy remains true until kurlyk delivers its final callback, even after cancel.
    std::optional<RefreshResult> poll() {
        const auto state = m_state;
        std::shared_ptr<Operation> operation;
        kurlyk::HttpResponsePtr response;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            operation = state->current;
            if (!operation || operation->processing) return std::nullopt;
            if (operation->oversized && !operation->reported && !operation->cancelled && !state->stopped) {
                operation->reported = true;
                if (operation->ready) state->current.reset();
                RefreshResult result;
                result.status = RefreshStatus::ResponseTooLarge;
                result.http_status = operation->http_status;
                result.attempts = operation->attempt;
                return result;
            }
            if (!operation->ready) return std::nullopt;
            if (operation->reported) {
                state->current.reset();
                return std::nullopt;
            }
            operation->processing = true;
            response = std::move(operation->response);
        }
        ExceptionCleanup cleanup{state, operation};
        RefreshResult result;
        result.http_status = operation->http_status;
        result.attempts = operation->attempt;
        if (response) result.native_error = response->error_code;
        ParseResult parsed;
        std::string etag;
        try {
            if (response && !operation->retrieved_at) {
                result.status = RefreshStatus::ParseError;
                result.diagnostic = operation->completion_error;
            } else if (!operation->submission_error.empty()) {
                result.status = RefreshStatus::NetworkError;
                result.diagnostic = operation->submission_error;
            } else if (!response) {
                result.status = RefreshStatus::NetworkError;
            } else if (response->error_code == kurlyk::utils::make_error_code(CURLE_OPERATION_TIMEDOUT)) {
                result.status = RefreshStatus::Timeout;
                result.diagnostic = response->error_message;
            } else if (response->status_code >= 400 && response->status_code <= 599 &&
                       response->error_code == kurlyk::utils::make_http_error(response->status_code)) {
                result.status = RefreshStatus::HttpError;
                result.diagnostic = "HTTP " + std::to_string(response->status_code);
            } else if (response->error_code) {
                result.status = RefreshStatus::NetworkError;
                result.diagnostic = response->error_message;
            } else if (response->status_code == 304) {
                result.status = operation->etag.empty() ? RefreshStatus::HttpError : RefreshStatus::NotModified;
            } else if (response->status_code != 200) {
                result.status = RefreshStatus::HttpError;
            } else {
                // Parsing may re-enter cancel/stop. Publication rechecks the fence.
                parsed = state->provider.parse(response->content, *operation->retrieved_at);
                result.parse_status = parsed.status;
                result.rejected_entries = parsed.rejected_entries;
                result.diagnostic = parsed.diagnostic;
                if (parsed.status == ParseStatus::Malformed) result.status = RefreshStatus::ParseError;
                else if (parsed.status == ParseStatus::NoData || parsed.quotes.empty()) result.status = RefreshStatus::NoData;
                else if (parsed.status == ParseStatus::Partial && !state->options.accept_partial) result.status = RefreshStatus::PartialData;
                else if (parsed.status != ParseStatus::Success && parsed.status != ParseStatus::Partial) result.status = RefreshStatus::ParseError;
                else result.status = RefreshStatus::Updated;
                const auto found = response->headers.find("ETag");
                if (found != response->headers.end()) etag = found->second;
            }
        } catch (const std::exception& error) {
            result.status = RefreshStatus::ParseError;
            result.diagnostic = error.what();
        } catch (...) {
            result.status = RefreshStatus::ParseError;
            result.diagnostic = "Clock or parser threw an unknown exception";
        }
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->stopped) result.status = RefreshStatus::Stopped;
            else if (operation->cancelled) result.status = RefreshStatus::Cancelled;
            else if (result.status == RefreshStatus::Updated) {
                if (state->book.replace_source(state->provider.source_id, std::move(parsed.quotes)) != QuotePublishStatus::Success)
                    result.status = RefreshStatus::PublicationRejected;
                else state->etag = std::move(etag);
            }
            state->current.reset();
        }
        return result;
    }

    /// Immediately fences publication; host cancels native I/O using its client.
    /// The pinned HttpClient only exposes a waiting group-cancellation barrier.
    void cancel() {
        const auto state = m_state;
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->current) state->current->cancelled = true;
    }

    /// Terminal publication fence, safe even while a native callback is pending.
    void stop() {
        const auto state = m_state;
        std::lock_guard<std::mutex> lock(state->mutex);
        state->stopped = true;
        if (state->current) state->current->cancelled = true;
    }

    bool busy() const {
        const auto state = m_state;
        std::lock_guard<std::mutex> lock(state->mutex);
        return static_cast<bool>(state->current);
    }

private:
    struct Operation {
        kurlyk::HttpResponsePtr response;
        std::string etag;
        std::string submission_error;
        std::string completion_error;
        std::optional<ts_ms_t> retrieved_at;
        std::size_t received = 0;
        long attempt = 0;
        long http_status = 0;
        bool ready = false;
        bool oversized = false;
        bool reported = false;
        bool cancelled = false;
        bool processing = false;
    };
    struct State {
        State(QuoteBook& book_, Provider provider_, RefreshOptions options_, std::function<ts_ms_t()> clock_)
            : book(book_), provider(std::move(provider_)), options(options_), clock(std::move(clock_)) {}
        QuoteBook& book;
        const Provider provider;
        const RefreshOptions options;
        const std::function<ts_ms_t()> clock;
        std::mutex mutex;
        std::shared_ptr<Operation> current;
        std::optional<ts_ms_t> last_attempt;
        std::string etag;
        bool stopped = false;
    };

    // Header preparation and atomic book publication can allocate outside the
    // parser error policy. Propagated failures must still release this operation.
    struct ExceptionCleanup {
        std::shared_ptr<State> state;
        std::shared_ptr<Operation> operation;
        int exceptions = std::uncaught_exceptions();

        ~ExceptionCleanup() {
            if (std::uncaught_exceptions() <= exceptions) return;
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->current == operation) state->current.reset();
        }
    };

    static void receive(const std::shared_ptr<State>& state, const std::shared_ptr<Operation>& operation,
                        kurlyk::HttpResponsePtr response) {
        if (!response) return;
        std::optional<ts_ms_t> retrieved_at;
        std::string completion_error;
        // Capture actual completion time, even if the host polls much later.
        // The injected clock is external code and must never run under our lock.
        if (response->ready) {
            try { retrieved_at = state->clock(); }
            catch (const std::exception& error) { completion_error = error.what(); }
            catch (...) { completion_error = "Completion clock threw an unknown exception"; }
        }
        std::lock_guard<std::mutex> lock(state->mutex);
        if (operation->ready) return;
        operation->http_status = response->status_code;
        if (response->retry_attempt != operation->attempt) {
            operation->received = 0;
            operation->attempt = response->retry_attempt;
        }
        if (response->stream_chunk) {
            const auto size = response->content.size();
            const auto cap = state->options.max_body_bytes;
            if (size > cap - operation->received) operation->oversized = true;
            else operation->received += size;
            return;
        }
        // Non-final non-chunks announce a retry. Final content already contains
        // the entire body, so streamed chunks must never be appended to it.
        if (!response->ready) { operation->received = 0; return; }
        operation->ready = true;
        operation->retrieved_at = retrieved_at;
        operation->completion_error = std::move(completion_error);
        if (response->content.size() > state->options.max_body_bytes) operation->oversized = true;
        if (!operation->oversized && !operation->cancelled && !state->stopped)
            operation->response = std::move(response);
    }

    std::shared_ptr<State> m_state;
    kurlyk::HttpClient& m_client;
};

} // namespace rateweaver
