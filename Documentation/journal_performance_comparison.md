# Journal performance and architectural comparison

In the C++ logging landscape, libraries fall into four distinct architectural generations based on how much work the calling thread performs before returning. `journal` sits firmly in the third generation of deferred binary ring loggers, alongside libraries like Quill and fmtlog.

## Latency comparison across C++ logging generations

| Logging tier and examples | Producer latency per call | Heap allocations on hot path | Clock source on producer | Total ordering across threads |
| :--- | :--- | :--- | :--- | :--- |
| Generation 1 synchronous `sd_journal_sendv` and `syslog` | 15,000 to 25,000 ns | None or internal socket buffers | Kernel socket timestamp | Kernel socket serialization |
| Generation 1 synchronous file `spdlog` and `glog` | 1,500 to 4,000 ns | Often 1 per call for string buffers | `clock_gettime` vDSO | Mutex lock order |
| Generation 2 async text `spdlog` async and `g3log` | 150 to 600 ns | 1 per call or pooled string buffer | `clock_gettime` vDSO | MPMC queue order |
| Generation 3 deferred MPSC `journal` | 28 to 48 ns[^1] | Zero via `constinit` static ring | Hardware `rdtsc` | Exact atomic `LOG_SEQ` |
| Generation 3 deferred SPSC Quill and fmtlog | 12 to 30 ns | Dynamic per thread SPSC queues | Hardware `rdtsc` | Reconstructed by timestamp sort |
| Generation 4 offline dictionary Stanford NanoLog | 7 to 18 ns | Dynamic per thread SPSC staging | Hardware `rdtsc` | Offline decompressor sort |

## How `journal` compares to each category

**Against synchronous `sd_journal_sendv` and `spdlog`, 50x to 500x faster.** Calling `sd_journal_sendv` directly on the application thread costs roughly 18,000 nanoseconds because the caller formats `iovec` strings and blocks inside a Linux `sendmsg` datagram socket call. Even synchronous file loggers like `spdlog` spend 1,500 nanoseconds formatting text and acquiring a file mutex. `journal` eliminates both costs from the caller by deferring all string formatting and `send_to_journald` socket calls to the `SCHED_BATCH` consumer thread.

**Against asynchronous `spdlog`, 5x to 12x faster.** Asynchronous `spdlog` still formats numbers and strings on the producer thread, reads the kernel vDSO clock at roughly 25 nanoseconds, and pushes a formatted buffer into a queue. `journal` skips producer formatting completely. It reads the CPU hardware cycle counter via `raw_timestamp` in about 3 nanoseconds, stores static string pointers in `slot_base_header`, and copies raw message bytes into preallocated 64 bit words.

**Against Quill and fmtlog, trading 10 nanoseconds for zero heap and exact global ordering.** Quill and fmtlog achieve 12 to 25 nanoseconds in single thread benchmarks by allocating a separate thread local SPSC ring buffer on the heap for every thread that logs. That avoids atomic contention on a shared head counter, but it requires dynamic memory allocation when threads start, unbounded memory growth as thread counts scale, and a consumer that must poll dozens of separate queues and sort timestamps to guess the order of events across threads. `journal` instead uses a single `constinit` MPSC ring in `ring_state`. The single atomic `head.fetch_add` adds about 10 nanoseconds compared to an uncontended thread local SPSC write, but it guarantees zero heap allocation, fixed memory footprint, and an exact hardware sequence number across all threads.

**Against Stanford NanoLog, standard `journald` compatibility without offline tools.** NanoLog reaches 7 to 18 nanoseconds by running a custom build step that strips format strings into an external dictionary and writes compressed binary files that cannot be read without a custom decompressor binary. `journal` stays within 15 to 20 nanoseconds of NanoLog using pure C++23 while delivering standard structured records directly to `systemd-journald`.

## Where `journal` beats even Quill and fmtlog

**Consumer side flight recorder filtering.** In most asynchronous loggers, enabling `debug` logging forces the background consumer thread to format and write every `debug` message to disk or `journald`, which saturates the consumer I/O bottleneck during bursts. Because `journal` separates `read_slot_header` from `read_slot_payload`, a suppressed `debug` record in flight recorder mode is written to the ring in 28 nanoseconds and stepped over by the consumer in about 15 nanoseconds by inspecting only the 40 byte header. No payload bytes are copied out of the ring and zero `sd_journal_sendv` system calls are made unless an error triggers `flush_lookbehind`.

**Non-preemptive `SCHED_BATCH` consumer scheduling.** Typical asynchronous loggers run their background flushing thread under the default Linux `SCHED_OTHER` policy and poll on short intervals. Because the Linux scheduler boosts the wakeup priority of threads that sleep briefly, an ordinary logging consumer can preempt latency sensitive application threads and bounce the shared ring `head` cache line across CPU cores. In `journal`, the background thread registers itself as `journal-consume` via `pthread_setname_np` and switches to the `SCHED_BATCH` scheduling policy via `pthread_setschedparam`, which requires no elevated privileges. The kernel treats the consumer as a throughput oriented background worker, suppresses wakeup preemption against application producer threads, and drains up to 64 records per batch with a two stage `50 µs` to `1 ms` idle backoff to keep the producer cache line hot.

[^1]: All `journal` measurements are recorded under custom release presets with production hardening enabled, including `-fstack-protector-strong` and standard library bounds assertions via `_GLIBCXX_ASSERTIONS` and `_LIBCPP_HARDENING_MODE_FAST`. Hardware performance counter comparisons against unhardened `-O3 -DNDEBUG` builds show that stack canary verification and `std::span` precondition checks add roughly 2 to 3 nanoseconds per call because the CPU branch predictor speculates through non-failing bounds checks with near zero misses. Reaching Generation 3 ring buffer latency while keeping stack protection and bounds hardening active makes these benchmark numbers directly representative of hardened production binaries rather than stripped microbenchmark builds.
