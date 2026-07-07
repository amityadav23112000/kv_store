# Multithreaded TCP Key-Value Store (mini-Redis) in C++17

An in-memory key-value server built from raw POSIX sockets up: thread pool,
sharded cache-line-aligned locking, newline-framed text protocol, RAII resource
management, and signal-driven graceful shutdown. Built as a systems-programming
deep dive across OS, networking, and computer-architecture concepts — every
design decision benchmarked, including one optimization that measurably lost.

## Features
- `SET key value` / `GET key` / `DEL key` over TCP (port 6379), values may contain spaces
- Many concurrent clients: fixed thread pool (sized to CPU cores) + task queue
  (mutex + condition_variable, producer-consumer)
- Correct TCP byte-stream handling: inbox buffering + newline framing
  (split and glued commands both handled), partial-send-safe writer
- Storage: 8-way sharded `unordered_map`, one mutex per shard,
  `alignas(128)` so shards never share a cache line (Apple Silicon lines are 128 B)
- RAII `Socket` wrapper (non-copyable, movable) — fd leaks impossible by construction
- Graceful shutdown: SIGINT handler + EINTR-aware accept loop; SIGPIPE ignored
  so dead clients degrade into ordinary send errors

## Architecture
clients ──TCP──▶ accept loop (main thread ONLY accepts)
│  task = [client_fd, &store] lambda
▼
task queue  ◀─ mutex + condition_variable ─▶  N pool workers
│ per client:
▼
recv → inbox buffer → '\n' framing
→ protocol parser (SET/GET/DEL)
→ hash(key) % 8 → shard lock → map
→ reply via send_all


Layers are independent files: `server.cpp` (network+threads), `protocol.h`
(parsing), `kvstore.h` (storage), `thread_pool.h`, `socket_raii.h`.
Storage and protocol were unit-tested before any socket existed.

## Build & run
```bash
g++ -std=c++17 -O2 -Wall -Wextra -o server server.cpp
./server                      # Ctrl+C for graceful shutdown
# talk to it:
nc 127.0.0.1 6379             # SET name amit / GET name / DEL name
```

## Benchmarks (MacBook, Apple M5, 8 threads, 90% GET / 10% SET, medians of 3)

Direct store benchmark (`bench_store.cpp`, no network — isolates lock design):

| Store variant                         | Throughput      | vs V1 |
|--------------------------------------|-----------------|-------|
| V1 — single global mutex             |  7.18M ops/sec  | 1.0×  |
| V2 — 8 sharded mutexes + alignas(128)| 15.17M ops/sec  | 2.1×  |
| V3 — sharded + `shared_mutex`        |  5.63M ops/sec  | 0.78× |

End-to-end network benchmark (`bench.cpp`, 8 TCP clients): **170K ops/sec** —
~89× below the store's capacity, i.e. the bottleneck is the request round trip
(≥4 syscalls + 2 thread wakeups per op), not the data structure.

False-sharing demo (2 threads, adjacent vs 128B-aligned counters): ____ ms vs ____ ms.

### Finding: the reader-writer lock LOST
V3 replaced each shard's mutex with `std::shared_mutex` expecting read-heavy
traffic to benefit — throughput instead fell 2.7× below plain sharded mutexes.
Mechanism: the critical section is one hash lookup (~100 ns) while a shared
lock must maintain a reader count — an atomic read-modify-write on a shared
cache line per entry AND exit — so admission cost exceeded the work protected.
Reader parallelism pays for long hold times, not microsecond ones.
**The shipped store is V2; the V3 experiment and its numbers are kept in-repo.**

## Design decisions (each one a measured or reasoned trade-off)
- **TCP over UDP** — a dropped `SET` silently acknowledged is data loss;
  kernel-level reliability/ordering beats app-level retransmission.
- **Text protocol, newline-framed** — endian-neutral (digits travel as ASCII),
  debuggable with netcat; byte order still handled where it's real: `htons`
  into kernel socket structs. Length-prefixed binary upgrade path understood
  (`htonl` length, then exact-count reads).
- **Thread pool over thread-per-connection** — bounded memory, no per-client
  creation cost, no thread-explosion DoS; over epoll/kqueue — my workload is
  few busy clients, and the goal was engineering shared-state concurrency,
  not solving C10K (can explain what an event loop would buy).
- **Sharded locks over one mutex** — measured 2.1×; hash routing keeps per-key
  operations serialized (correctness), no operation holds two shard locks
  (deadlock-free by construction).
- **alignas(128) not 64** — Apple Silicon P-cores use 128-byte cache lines;
  64-byte alignment would leave two shards per line on this machine.

## Known limitations (deliberate scope)
- Shutdown drains but doesn't evict: a silent connected client delays exit
  (eviction design known: fd registry + `shutdown(fd, SHUT_RDWR)`).
- No inbox size cap (a '\n'-less flood grows memory); no idle timeouts.
- Single node, in-memory, no persistence — extension path: consistent-hash
  client sharding, then primary-replica replication.

## Repo map
`server.cpp` `client.cpp` `kvstore.h` (+v1/v2/v3 variants) `protocol.h`
`thread_pool.h` `socket_raii.h` `bench.cpp` `bench_store.cpp`
`race_demo.cpp` / `race_fixed.cpp` (M4 evidence) `false_sharing_demo.cpp`
`endian_demo.cpp` `run_bench.sh`