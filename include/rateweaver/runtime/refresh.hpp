#pragma once

#include <rateweaver/core/quote_book.hpp>
#include <rateweaver/providers/common.hpp>
#include <rateweaver/runtime/transport.hpp>

#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace rateweaver {

struct RefreshOptions {
    ts_ms_t min_interval_ms = 60000;
    ts_ms_t timeout_ms = 5000;
    std::size_t max_body_bytes = 1024 * 1024;
    bool accept_partial = false;
};

enum class RefreshStatus {
    Updated, NotModified, NotDue, Busy, Cancelled, Stopped, InvalidOptions,
    NetworkError, Timeout, HttpError, ResponseTooLarge, ParseError, NoData,
    PartialData, PublicationRejected
};

struct RefreshResult {
    RefreshStatus status = RefreshStatus::NoData;
    ParseStatus parse_status = ParseStatus::NoData;
    std::size_t rejected_entries = 0;
    std::string diagnostic;
    RefreshResult(RefreshStatus status_ = RefreshStatus::NoData,
                  ParseStatus parse_status_ = ParseStatus::NoData,
                  std::size_t rejected_entries_ = 0, std::string diagnostic_ = {})
        : status(status_), parse_status(parse_status_), rejected_entries(rejected_entries_),
          diagnostic(std::move(diagnostic_)) {}
    explicit operator bool() const noexcept {
        return status == RefreshStatus::Updated || status == RefreshStatus::NotModified;
    }
};

/// \brief Synchronous refresh driven manually or by the host's timer.
/// \note Use one controller per source; the overlap gate is per instance.
/// \note The host must keep controller, book and transport alive until every
///       refresh call returns. stop() signals cancellation and suppresses late
///       publication; it does not wait. Drain the host executor before destruction.
class RefreshController final {
public:
    RefreshController(QuoteBook& book, HttpTransport& transport, Provider provider,
                      RefreshOptions options = {},
                      std::function<ts_ms_t()> clock = current_time_ms)
        : m_book(book), m_transport(transport), m_provider(std::move(provider)),
          m_options(options), m_clock(std::move(clock)) {}

    RefreshController(const RefreshController&) = delete;
    RefreshController& operator=(const RefreshController&) = delete;

    /// \brief Starts at most one bounded attempt; failures also consume the interval.
    /// \note Clock callback exceptions propagate; the overlap reservation is released.
    RefreshResult refresh() {
        CancellationToken cancellation;
        HttpRequest request;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopped) return {RefreshStatus::Stopped};
            if (m_busy) return {RefreshStatus::Busy};
            if (m_provider.source_id.empty() || m_provider.url.empty() || !m_provider.parse || !m_clock ||
                m_options.min_interval_ms < 0 || m_options.timeout_ms <= 0 || m_options.max_body_bytes == 0)
                return {RefreshStatus::InvalidOptions};
            request = {m_provider.url, m_options.timeout_ms, m_options.max_body_bytes, m_etag};
            m_cancellation = cancellation;
            m_busy = true;
        }
        BusyGuard busy{*this};
        const ts_ms_t started_at = m_clock();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopped) return {RefreshStatus::Stopped};
            if (cancellation.is_cancelled()) return {RefreshStatus::Cancelled};
            if (m_last_attempt && (started_at < *m_last_attempt ||
                static_cast<std::uint64_t>(started_at) - static_cast<std::uint64_t>(*m_last_attempt) <
                    static_cast<std::uint64_t>(m_options.min_interval_ms))) return {RefreshStatus::NotDue};
            m_last_attempt = started_at;
        }
        HttpResponse response;
        try { response = m_transport.get(request, cancellation); }
        catch (const std::exception& error) { return failure(RefreshStatus::NetworkError, error.what()); }
        catch (...) { return failure(RefreshStatus::NetworkError, "Transport threw an unknown exception"); }
        if (cancellation.is_cancelled() || response.error == TransportError::Cancelled)
            return failure(RefreshStatus::Cancelled);
        if (response.error == TransportError::Timeout) return failure(RefreshStatus::Timeout, response.diagnostic);
        if (response.error != TransportError::None) return failure(RefreshStatus::NetworkError, response.diagnostic);
        if (response.body.size() > request.max_body_bytes) return failure(RefreshStatus::ResponseTooLarge);
        if (response.status_code == 304) {
            if (request.if_none_match.empty()) return failure(RefreshStatus::HttpError, "Unsolicited HTTP 304");
            return failure(RefreshStatus::NotModified);
        }
        if (response.status_code != 200) return failure(RefreshStatus::HttpError, "HTTP " + std::to_string(response.status_code));
        const ts_ms_t retrieved_at = m_clock();
        if (cancellation.is_cancelled()) return failure(RefreshStatus::Cancelled);
        ParseResult parsed;
        try { parsed = m_provider.parse(response.body, retrieved_at); }
        catch (const std::exception& error) { return failure(RefreshStatus::ParseError, error.what()); }
        catch (...) { return failure(RefreshStatus::ParseError, "Parser threw an unknown exception"); }
        if (parsed.status == ParseStatus::Malformed) return failure(RefreshStatus::ParseError, parsed.diagnostic, &parsed);
        if (parsed.status == ParseStatus::NoData || parsed.quotes.empty()) return failure(RefreshStatus::NoData, parsed.diagnostic, &parsed);
        if (parsed.status == ParseStatus::Partial && !m_options.accept_partial)
            return failure(RefreshStatus::PartialData, parsed.diagnostic, &parsed);
        if (parsed.status != ParseStatus::Success && parsed.status != ParseStatus::Partial)
            return failure(RefreshStatus::ParseError, "Invalid parser status", &parsed);
        // Serialize the final publication with stop/cancel, after all external
        // calls have returned. QuoteBook's commit is atomic and invokes no user code.
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopped) return {RefreshStatus::Stopped};
        if (cancellation.is_cancelled()) return {RefreshStatus::Cancelled};
        if (m_book.replace_source(m_provider.source_id, std::move(parsed.quotes)) != QuotePublishStatus::Success)
            return {RefreshStatus::PublicationRejected, parsed.status, parsed.rejected_entries, "QuoteBook rejected provider batch"};
        m_etag = std::move(response.etag);
        return {RefreshStatus::Updated, parsed.status, parsed.rejected_entries, std::move(parsed.diagnostic)};
    }

    /// \brief Cancels the current attempt; a later due refresh remains possible.
    void cancel() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_busy) m_cancellation.cancel();
    }

    /// \brief Permanently stops this controller without waiting for the caller.
    void stop() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopped = true;
        m_cancellation.cancel();
    }

private:
    struct BusyGuard {
        RefreshController& owner;
        ~BusyGuard() {
            std::lock_guard<std::mutex> lock(owner.m_mutex);
            owner.m_busy = false;
        }
    };

    RefreshResult failure(RefreshStatus status, std::string diagnostic = {},
                          const ParseResult* parsed = nullptr) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopped) status = RefreshStatus::Stopped;
        else if (m_cancellation.is_cancelled()) status = RefreshStatus::Cancelled;
        return {status, parsed ? parsed->status : ParseStatus::NoData,
                parsed ? parsed->rejected_entries : 0, std::move(diagnostic)};
    }

    QuoteBook& m_book;
    HttpTransport& m_transport;
    Provider m_provider;
    RefreshOptions m_options;
    std::function<ts_ms_t()> m_clock;
    std::mutex m_mutex;
    CancellationToken m_cancellation;
    std::optional<ts_ms_t> m_last_attempt;
    std::string m_etag;
    bool m_busy = false;
    bool m_stopped = false;
};

} // namespace rateweaver
