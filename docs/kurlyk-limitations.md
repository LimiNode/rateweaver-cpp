# kurlyk v1.1.1 API audit

Audited release: [kurlyk v1.1.1](https://github.com/LimiNode/kurlyk/releases/tag/v1.1.1),
commit `b62cd53f672c47b74bb14d762ea9dc59a9df19ce`. This audit reads the published
release, rather than assuming older dependency findings still apply. Observed
runtime results belong to [validation](validation.md); source inspection alone
is not a race-freedom proof.

## Version-specific findings

| Classification | Finding | Evidence at v1.1.1 |
| --- | --- | --- |
| Fixed in v1.1.1 | Group counts stay continuous across pending/active/retry transitions; public group queries no longer traverse worker-mutated request containers. | `HttpRequestManager::submit_request()` increments `m_group_request_counts` under `m_mutex`; queries read that map and `complete_group_request()` decrements it after the final user callback. |
| Fixed in v1.1.1 | Callback exceptions do not skip group completion. | `HttpRequestContext::invoke_final_callback()` invokes an exception-isolated callback, then idempotent `complete()`. Its group callback still runs when rate-limit completion throws. Upstream includes corresponding regressions in `http_client_wait_requests_main.cpp`. |
| Fixed in v1.1.1 | A timed-out group waiter can be removed. | Registration returns an ID; `HttpClient::wait_requests_for()` calls `cancel_wait_requests_by_group_id()` on timeout. |
| Still reproducible | Streaming retains the complete native body before notification; an accepted-body limit is not a receive-memory cap. | `HttpRequestHandler::write_response_body()` appends before its void streaming callback and returns the full consumed size. No request option enforces a body cap before append. Loopback rejection tests do not prove a nonexistent memory guarantee. |
| Still reproducible | Client cancellation/destruction can wait. | `HttpClient::cancel_requests()` waits on `future.get()`; its destructor calls that barrier. The worker-thread branch recursively calls `worker.process()` despite its warning. |
| Not tested | General concurrent teardown, arbitrary reentrant callbacks, hostile compressed bodies, redirect chains and total peak-memory bounds. | Local tests cover specific lifecycle paths; they do not establish properties beyond those paths. Calling the cancellation worker branch from a curl callback is unsupported here, not a newly reproduced deadlock. |
| Requires separate kurlyk PR | Pre-append receive cap and nonwaiting targeted cancellation preserving client defaults. | `get()` returns admission success, while `make_request()` and the client's request template/group identity are private. Manager cancellation IDs require independently prepared requests. Both extensions need upstream API changes; RateWeaver does not modify vendor headers or create another transport/scheduler. |

The surviving buffer ordering and waiting APIs are source-confirmed constraints.
The [native loopback checks](validation.md) exercise accepted-body rejection,
actual connection abort and cancellation's final-callback drain. They do not
measure peak memory, prove a hard receive cap or reproduce a callback deadlock.

The former container-scanning group-accounting diagnosis is superseded for this
release. No request remains to reimplement its outstanding-group counter.
Manager processing and shutdown must remain on its designated processing thread.
The host should use `deinit()` to stop/join the worker rather than race direct
manager shutdown against active processing.

## Refresh and ownership contract

The native client retains headers, timeout/retry defaults, its request group and
owned/shared rate-limit handles. Configure it once. Callback metadata separates
stream chunks (`stream_chunk`), retry notifications (`!ready && !stream_chunk`)
and final responses (`ready`). Final content already includes all received
bytes; RateWeaver never appends chunks again. Native retries stop after any
streaming chunk. RateWeaver adds no HTTP retry loop or rate counter.

`KurlykRefresh::cancel()` and `stop()` immediately fence publication without
waiting. Native I/O cancellation remains a host action. Keep the client and
callback captures alive, invoke its barrier on the host thread, and poll until
the final response drains before another refresh. Never destroy the client,
invoke its barrier or stop its runtime from a callback running on that worker.
`wait_requests()` explicitly rejects worker-thread use with `std::logic_error`.

In manual mode the host must pump `process()` while work remains. A blocking
barrier on that same thread cannot progress after pumping stops. The refresh
busy gate follows its actual final-callback state.

`max_body_bytes` bounds accepted data: accounting checks remaining capacity
before addition, oversized content is never parsed/published, and the host
cancels native I/O after an early `ResponseTooLarge`. Native buffering can grow
between a chunk and cancellation. Do not describe this as a memory ceiling.

## Separate upstream work

1. Expose a submission handle/ID from `HttpClient` preserving configured defaults
   and group ownership. Provide nonwaiting cancellation that only schedules the
   request and never recursively processes curl callbacks.
2. Add a native body cap checked before append, with a distinct final diagnostic.
   Define decoded-byte semantics and bound decompression as well.
3. Document/test streaming ownership, retry/reset rules, compressed/chunked
   bodies, redirects, chunk-callback cancellation and final-callback teardown.

These extensions remain separate review work. No vendor code is changed by this
dependency update and no production guarantee relies on them already existing.
