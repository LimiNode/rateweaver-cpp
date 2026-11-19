#pragma once

#include <rateweaver/core/types.hpp>

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>

namespace rateweaver {

/// \brief Copyable cancellation state shared with one synchronous request.
class CancellationToken {
public:
    bool is_cancelled() const noexcept { return !m_state || m_state->load(std::memory_order_acquire); }
    void cancel() noexcept { if (m_state) m_state->store(true, std::memory_order_release); }
private:
    std::shared_ptr<std::atomic<bool>> m_state = std::make_shared<std::atomic<bool>>(false);
};

struct HttpRequest {
    std::string url;
    ts_ms_t timeout_ms = 5000;
    std::size_t max_body_bytes = 1024 * 1024;
    std::string if_none_match;
};

enum class TransportError { None, Network, Timeout, Cancelled };

struct HttpResponse {
    int status_code = 0;
    std::string body;
    std::string etag;
    TransportError error = TransportError::None;
    std::string diagnostic;
};

/// \brief Host-owned blocking transport; no global runtime is required.
/// \note Implementations must honor timeout, body cap and cancellation while
///       receiving. The controller cannot preempt a non-cooperative transport.
class HttpTransport {
public:
    virtual ~HttpTransport() = default;
    virtual HttpResponse get(const HttpRequest& request, const CancellationToken& cancellation) = 0;
};

} // namespace rateweaver
