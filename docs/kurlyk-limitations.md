# Pinned kurlyk audit and required upstream changes

Audited dependency: `LimiNode/kurlyk`
`fe2698dec259a7e417883e1f41985ed21ec28b55`, the MGC gitlink. This is an API
constraint report, not a claim that all receive-limit requirements are met.

## Available native behavior

`HttpClient` preserves its host, headers, timeout/retry defaults, request group,
owned rate limits and externally assigned shared handles. Its callback API
distinguishes streaming chunks (`stream_chunk`), intermediate retry responses
(`!ready && !stream_chunk`) and final responses (`ready`). Final content contains
the entire body, including bytes already delivered through streaming callbacks.
RateWeaver never appends those chunks to final content.

The native handler disables retries once any streaming chunk has been emitted.
An empty-body HTTP failure or connection failure can still retry. RateWeaver
uses that behavior directly and does not add another retry loop. The retry
counter identifies attempts; intermediate callbacks cannot publish quotes.

The audit also read OptionX's `BaseHttpClientComponent` and the IntradeBar and
TradeUp HTTP components: they retain clients and configure limits once, poll
only ready futures, and use native client cancellation during host teardown.
RateWeaver borrows the ownership pattern without their component/task hierarchy.

## Receive size is not a hard memory limit

In `include/kurlyk/http/HttpRequestManager/HttpRequestHandler.hpp`,
`write_response_body()` appends bytes to the internal complete response **before**
calling the void streaming callback. It always returns the consumed byte count.
There is no public abort decision or per-request receive-size limit.

RateWeaver counts streamed bytes using subtraction before addition, reports
`ResponseTooLarge` as soon as the host polls an exceeded limit, and never parses
or publishes an oversized body. It retains no second assembled body. The native
handler can continue buffering between a chunk and the host's cancellation.
Thus `max_body_bytes` is an acceptance limit, not a guaranteed memory ceiling.
The host should call its dedicated client's native `cancel_requests()` after
an early `ResponseTooLarge` result. A test checks actual socket cancellation;
it cannot establish a hard cap that this pinned API does not provide.

## Publication cancellation and native I/O cancellation

`HttpClient::get()` returns only admission success. Its request factory and
group identifier are private. The public nonwaiting manager cancellation API
requires a request/group identifier that this client API does not expose.
`HttpClient::cancel_requests()` is a cancellation barrier: it waits for native
cancellation callbacks. The client destructor invokes the same barrier.

`KurlykRefresh::cancel()` and `stop()` immediately fence publication without
waiting, accessing a destroyed client or capturing the client in a callback.
They do not claim to abort native I/O. Keep the dedicated client alive and use
its native cancellation barrier on the owning host thread when I/O must stop.
The refresh overlap gate remains closed until the final native callback drains;
polling then permits a later attempt after cancel. Stop is terminal. The host
can create a new refresh object with the same still-live client after draining.

Never call the native barrier or destroy the client from its network callback.
The pinned implementation has a worker-thread branch that recursively processes
the manager; invoking it inside a curl callback can re-enter manager processing.
RateWeaver does not use that branch, invent an executor to hide waiting, intercept
authentication to extract private identities, or bypass client defaults by
constructing unrelated requests directly in the manager.

## Native group accounting and drain

The delayed-poll regression exposed an additional pinned problem:
`HttpClient::wait_requests()` can return before its final callback. In
`HttpRequestManager.hpp`, `process_pending_requests()` removes contexts from
`m_pending_requests`, releases `m_mutex`, constructs a batch, and only then adds
that batch to `m_active_request_batches`. During the handoff, group-count queries
can observe zero because they count only pending, failed and active containers.
The same query is used by the native group cancellation barrier and admission
guard. RateWeaver's overlap fence therefore uses its own final-callback state,
not native group-count/wait completion.

The active/failed container mutations in worker processing also occur outside
`m_mutex`, while public group-count queries traverse them under that mutex.
That is a supported synchronization defect in the pinned dependency, not a
RateWeaver lock that can repair it. Native barrier behavior in the exercised
loopback cases is recorded in validation; it is not proof that arbitrary
concurrent client teardown is safe. The completion timestamp regression waits
for the clock sample inside the actual final callback instead of relying on
`wait_requests()`.

## Proposed separate kurlyk change

Before claiming full receive-limit and nonwaiting I/O cancellation support:

1. Expose a native request submission handle/ID from `HttpClient`, preserving
   its default configuration and group ownership. Supply a nonwaiting cancel
   operation that only schedules cancellation and never recursively processes
   the current curl callback.
2. Add a per-request received-body cap. In the native write handler, check
   multiplication and remaining capacity **before** append and return a curl
   write error immediately on overflow. Preserve a distinct limit-exceeded
   diagnostic in the final response.
3. Optionally allow streaming without retaining the complete body. Document
   retry/reset semantics and the final-response content contract explicitly.
4. Test large/chunked/compressed bodies, retries, cancel inside a chunk callback,
   teardown, redirects and final callback delivery in kurlyk itself.
5. Make group accounting continuous across pending/active/retry transitions and
   synchronize public group queries with worker mutations. Wait/cancel/admission
   decisions must use that continuous accounting and deliver callbacks without
   holding the manager lock.

No vendor copy or MGC gitlink is modified here. A separate kurlyk review and pin
update are prerequisites for the stronger guarantees. Until then the native
integration is useful for evaluation, but MGC production acceptance remains
gated on these constraints and the consumer review.
