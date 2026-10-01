# alxcomm — Design

This document records the **design decisions, implementation notes and tuning history** of `alxcomm`. For usage, see the corresponding entry in the README's document index.

## Design principles

1. **Layered, each on the one below**: `transmit` (bytes in and out, nothing else) → `comm` (session: worker thread + send queue) → `comm_ex` / `http` / `rpc` (their own message semantics). Each layer depends only on the one below, and the upper layers never touch a socket.
2. **A peer is never trusted**: every length, offset and declared value is bounds-checked when parsed; a parser would rather fail than read out of bounds.
3. **Bounded blocking**: every operation that can block on a peer's behaviour (connect, handshake, send, receive) has a timeout or an interrupt, and no path may "hang forever because the peer does nothing".
4. **No exceptions**: as in base/core, a failure is reported through the return value and `mesg_prit` text.

---

## 1. Transport layer `transmit`

### 1.1 Structure

Each transport is an implementation class of its own, self-registered into the factory through `alx::product` (`transmit::create("tcp_server", ...)` builds one by name). The unified interface is four calls -- `open` / `close` / `connect_num` / `bytes_send` -- plus two signals (`bytes_recv` / `trans_mesg`).

**The address key is the `uint_64` `(ip << 32) | port`**: every callback, directed send and directed close uses it to name a peer, and `0` is the reserved value for "all". The one exception is `local_server`, which names a peer by a connection id instead (`atransmit.h`). That gives TCP, UDP and Unix sockets one addressing scheme (a Unix socket uses the ip half as a hash).

### 1.2 Socket options and robustness

- **`MSG_NOSIGNAL`**: carried on a plain TCP send, so a `SIGPIPE` from a peer that has closed does not kill the process outright.
- **`SO_SNDTIMEO` (5 seconds)**: set both on the connections the server accepts and on the client connections, so "the peer does not read" becomes one bounded failure instead of an endless wait.
- **`SO_RCVTIMEO` (10 seconds)**: set only during the TLS handshake, to give it an upper bound.
- **`EINTR` retry**: `io_interrupted()` tells the platforms apart (`EINTR` / `WSAEINTR`), and the 7 send/recv/sendto/recvfrom sites retry when a signal interrupts them instead of counting it a failure.
- **A send return of 0 counts as a failure**: otherwise `offset += 0` loops forever.

### 1.3 How TLS is integrated

TLS is not a transport of its own but "an SSL layer wrapped over a socket":

- **One thread per connection**: an accepted connection is handed to its thread at once, and that connection's own mutex protects the reads and writes of its SSL object.
- **The handshake moved out of the accept thread**: the accept thread only establishes the connection and registers the table entry, and the handshake happens on the connection's thread. Otherwise a peer that sends nothing would stall the whole accept loop (no further connection could get in). The table entry is registered **before** the handshake, so `close()` can shut it down too.
- **A connection whose handshake is not done takes no part in sending**: `bytes_send` skips an entry with `ready == false`, and "every one of them unready" is not a send failure.
- **`SIGPIPE` handling**: OpenSSL sends alerts and records through a bare `write()`, so a peer that disconnects gets the process a `SIGPIPE`. On the first TLS `open()`, if the process's `SIGPIPE` is still `SIG_DFL` it is set to `SIG_IGN` (a host-installed handler is left alone) -- a **process-wide side effect**, done once.
- **Hostname verification**: configuring `ca` forces a `hostname` as well, and `X509_VERIFY_PARAM_set1_host` / `set1_ip_asc` pin the peer identity down; chain verification alone would be bypassed by "any certificate that CA ever signed".

### 1.4 The close procedure and the concurrency contract

`close(0)` closes the whole transport: it closes the listening/own socket first, then `shutdown`s the connected peers one by one (waking their worker threads out of `recv` / `SSL_read`, with **each worker closing its own fd exactly once**), **joins the receiver thread first** (no new entry or worker can appear after that), wakes them once more (to catch the one the receiver thread let through last), and finally joins every worker thread and finishes up. `close(loc)` closes one peer only: it `shutdown`s that peer's socket, and the entry is erased by that worker when it finishes (so `connect_num()` still counts it until then). `local_server` has no connection table; its accept thread re-checks "has this been closed" after storing the socket, and recycles that connection in place and returns when it has.

**Whoever takes the peer socket out of the table fires that `(key, false, "disconnect")`** (`close(0)` fires `(0, false, "close")` instead, which names no peer): a client's receiver thread, a server worker finishing up, and `close(loc)` -- the host kicking a peer out itself -- all follow it; `local_server::close(loc)` fires it in place when it takes the socket out itself, so the receiver thread's tail no longer fires a second one.

**The server owns its worker threads** (`m_workers`, entering and leaving together with the table entries, no longer `detach`ed): `close(0)` and the destructor wait for all of them to finish before returning, so a host that "sees `connect_num()==0` and destructs" no longer steps on a thread still using `this` (in the original implementation a worker erased its entry and then fired `disconnect` -- that path was exactly the UAF entry). Each worker sets its **done flag** as its very last act, and the accept thread reaps the threads whose flag is set after every `accept` -- a long-running server accumulates no joinable thread objects, and the reaping join does not land on a host callback (judging by "the entry is erased" would join a worker still running its callback as well).

**Concurrency contract** -- an interface convention; the library adds no gate for a use that goes outside it:

| Interface | Convention |
|---|---|
| `open` / `close` (the destructor included)| the host calls them **serially** on one instance |
| `is_open` / `connect_num` | state reads, **any thread** may call them concurrently (the library makes them genuinely thread-safe)|
| `bytes_send` | the host keeps it **non-concurrent** (including not overlapping a send from inside a callback), and never overlapping an `open` / `close` |
| `bytes_recv` / `trans_mesg` callbacks | **passive**: the library pushes into them; the host must not `close` / `open` / `delete` / send from inside. Note that the signals `open` / `close` emit themselves run on the **caller's thread**, and only the data and connection signals run on the library's threads |

---

## 2. Session layer `comm`

### 2.1 The worker thread model

`comm` starts **one** worker thread that loops over three things:

1. the transport is not open → try `open()` (a failure lengthens the sleep exponentially, capped at 256 times the base);
2. it is open but there is no peer yet → back off and sleep the same way;
3. there is a peer → pump the send queue (`block_pop` with a timeout, running the queued tasks one by one).

Every "send" and "close one peer" is **handed to the queue** for the worker thread to run, so `bytes_send` calls from several threads never write to a socket concurrently.

### 2.2 `m_opened` rather than `m_thread`

The worker's loop condition is "is the session open". The early implementation read `std::thread* m_thread` directly, which brought two problems: a data race (the main thread writes, the worker reads), and **starting the thread before assigning the pointer** when it is created, so the worker could read `nullptr` and exit at once (it looks like "suicide right after `open()`").

It now uses a `std::atomic<bool> m_opened`, set **before the thread is created**, and `m_thread` is touched only by the main thread.

### 2.3 Closing

`close(0)`: set `m_opened = false` first (the worker leaves its loop and closes the transport itself) and take the thread pointer → `join` → destroy the queue. **No busy-wait** -- polling `connect_num()` down to zero is not needed, because closing the transport is the worker's own job.

### 2.4 The connection-event hook

The session layer turns the transport's `trans_mesg` into a protected virtual function for a derived class to override (empty by default; it does not change the existing meaning of `comm_flag` for a "peer event"):

| Hook | When | Runs on |
|---|---|---|
| `on_trans_event(loc, connect)` | every connection event (open/close/peer event), `loc == 0` is the transport itself | the thread that fired the signal |

The session layer does not fire events on the transport's behalf: a disconnect is reported by the transport itself (all three client transports fire `(0, false, "close")` both in `close()` and in the receive loop), and whoever relies on it relies on that existing convention. It is bound by the callback contract of §1.4 (no `close` / `open` / `delete` / send from inside a callback).

---

## 3. Data frames `comm_ex`

### 3.1 Pack format

```
[8B magic 0x0123456789ABCDEF]
[orig.size + ~size][cmps.size + ~size]     both sizes carry the complement check
[version:4][checksum:4][extra:3*8]         control block (flags live in extra as bits)
[data: a varmap serialized by varsolid, optionally LZ4-compressed]
[8B tail magic 0xFEDCBA9876543210]
```

- **Sizes carry their complement**: `size` and `~size` are stored as a pair and the unpacker checks that they complement, which rejects "random bytes taken for a pack head".
- **The control block uses bit flags**: `compress` (row 0, bit 0), `verify` (row 0, bit 1), with 24 bytes reserved for later flags.
- **The checksum is CRC32C**: enabled only when the CPU has the hardware CRC32 instructions (`hardcal()`), so a software implementation does not slow down the frequent small packs.
- **Self-delimiting**: frames are cut on the head and tail magic and the size fields, and the unpacker keeps a half-frame cache across calls, so TCP coalescing and splitting are transparent to the layers above.

### 3.2 Codec reuse

The encoding and decoding of `packer` are stateless and functional, but a decoder instance carries a `thread_local` half-frame cache -- each receive thread caches its own, which avoids a lock.

---

## 4. HTTP

### 4.1 Messages

A text protocol of its own: start line + `key: value` headers + a blank line + body. `Content-Type` / `Content-Length` are filled in automatically by `set_content`. A request declaring a body past the server's hard cap (64 MiB) is dropped, neither handled nor answered.

### 4.2 Server-side parsing

- **Cross-call cache**: a receive callback may get half a request or several of them, so the parser accumulates them in a `thread_local bytes cache` and loops out whole requests.
- **Locating the request head**: it looks for `"HTTP/1."` first, walks back to the method name and the start of the line, and only then looks for `\r\n\r\n` **from the line start onwards** -- searching from the beginning of the buffer would hit the previous request's terminator and cut out a reversed range.
- **Bounded `Content-Length` parsing**: the cache is not NUL-terminated, so `std::stoi` cannot be used (it would read out of bounds). It accumulates digit by digit within the head range instead, dropping the receive cache as soon as the total passes the hard cap; the same constraint (64 MiB, no API knob) applies to one client-side chunked block.
- **`chunked` decoding**: a loop of block-length line (hex, at most 16 characters) + data + CRLF; the length is accumulated in hex, and 16 digits land exactly inside a `uint_64` without wrapping. Chunk extensions are not supported (judged illegal outright), and trailers are ignored.

### 4.3 Routing

`http::api` is a tree dispatching by URL word: each node holds an `api_func`, `insert(child, word)` mounts a child and `add_alias` adds an alias. An incoming request drills down the tree by its `urlwords`, and the node it lands on runs its function; a node with no handler answers 403 and a word that matches no child answers 404.

### 4.4 Client

One `std::promise<reply>` per request: `exec()` sends the request and returns a future, and the reply is assembled whole in `on_bytes_recv` by `Content-Length` / `chunked` and then set into the promise. The client keeps exactly one pending-reply state (`m_promise` / `m_reply`), so **one instance allows one request in flight at a time** -- that is the API contract (one future `get` before the next send), a second `exec()` returns false outright, and the client **takes no lock for concurrent use**.

**A request in flight ends asynchronously in two ways**: the reply arriving (normal) and the connection dropping (`on_trans_event`'s `connect == false`, which covers the peer closing and a host `close()`) -- besides those, `exec()`'s own enqueue failure and the destructor also land the future; a send failure is **not** among them (`exec()` returning true only means the request was queued). The disconnect one goes through `abort_pending()`: drop `m_reply` / `m_buffer` first, then land the future with an **empty `reply`** (`state == NONE`) -- the order cannot be swapped, since a host that is woken may re-send at once and the state must be clean by then. A disconnect also clears the send queue along the way: a request that has not gone out yet would otherwise be sent after the reconnect, and its reply would land on the next request. Without that reset, a disconnect leaves the future blocked forever, and `m_promise` never resets so `exec()` is false forever.

The destructor does `close()` and then `abort_pending()`: `close()` joins the receive thread, which might otherwise be parsing on that same state. For the narrow window where `exec()` interleaves with a disconnect event see the "Send and receive model" section of `notice.md`.

### 4.5 The server processing pipeline

```
worker thread: receive bytes → parse_bytes (cut requests) → threadpool::enqueue(process_request)
pool thread:   run the api function → reply_request (send the reply back)
```

Parsing is separated from the work: parsing runs on the worker thread (order must be kept), the work on the threadpool (it may run concurrently).

---

## 5. RPC

### 5.1 Protocol

An RPC message is itself a `varmap` pack (reusing the send, receive and compression of `comm_ex`); its fields:

| Field | Meaning |
|---|---|
| `rpcid` | the call id (generated by the caller, carried back unchanged in the reply)|
| `state` | request / reply and the other states |
| `service` | the service id (what `install_service` returns)|
| `expcmps` | the compression policy expected |
| `error` | error text (an empty string means success)|
| `data` | the payload (request arguments or the return value)|

### 5.2 The two tables

- `service_map`: service id → `service_pack` (handler + description + compression option + whether it runs inline), with `install_service` allocating the id;
- `consume_map`: `rpcid` → `consume_pack` (callback + arguments + compression expectation); `consume_service` registers and then sends.

Both tables are `thread_safe_readwrite`: the receive thread reads them while the business threads install and uninstall, so a read like `list_service()` takes the read lock as well.

**The reserved service with id `0`** is the built-in "list the services" and is marked **inline_exec** -- it runs at once on the receive thread, so that even "ask for the service list" need not queue for the threadpool.

### 5.3 Running a service

It runs on the threadpool `rpc` owns by default (the pool size and queue cap are given at construction); `service_pack`'s `inline_exec = true` runs it in place instead. **A copy of the `service_pack` is taken before the lock is released**, so a service function that calls `install_service` / `remove_service` in turn cannot self-lock.

### 5.4 Argument binding

`service_pack::create<Args...>` wraps "take the arguments out of a `varvec` + convert their types + call the handler" into one `std::function`. A count that does not match returns `invalid params` outright, with no partial binding. The in-order expansion of the arguments uses the `index++/index--` comma-expression trick, and since GCC and Clang evaluate in different orders each of the two spellings carries a pragma to suppress the warning.

---

## 6. Revision log (behaviour changes that touch the design)

| Topic | Change | Reason |
|---|---|---|
| Pack head parsing | `pack_head` is taken by value (`m_interpret`), and the neighbouring scalar read sites follow | `r_interpret` hands back a reference into the buffer, and a pack can sit at any offset ⇒ every member access and member call lands on an unaligned address (40 of the 41 UBSAN reports)|
| `comm::is_open` | now reads the atomic `m_opened`, set before the thread is created | the original read a bare `m_thread`: both a data race and the "thread started before the pointer is assigned" that let the worker read `nullptr` and exit at once |
| `comm::close(0)` | the busy-wait polling `connect_num()` is gone | the original busy-waited for the peer count to reach zero when closing a session |
| TLS server | the handshake moved off the accept thread; the entry is registered first and the handshake happens on the connection's thread | the original handshook on the accept thread, so a silent peer blocked the whole listen loop |
| TLS, one `SSL*` | reads and writes are serialized by the connection's own mutex | the original let the sending thread and the receiving thread work on the same `SSL*` at once |
| TLS | ignore `SIGPIPE` on the first open, when the disposition is still the default | OpenSSL uses a bare `write()`, so a peer that disconnects gets the process a `SIGPIPE` and kills it |
| TLS client | with a `ca`, the hostname is verified too, and a missing `hostname` refuses the connection | chain verification alone is bypassed by "any certificate that CA ever signed" |
| socket send/recv | retry on `EINTR`; a send return of 0 counts as a failure | the original failed outright on a call a signal interrupted, and a return of 0 loops forever |
| socket send | `SO_SNDTIMEO` is set, with the no-progress bound on top on the TLS side | the original blocked forever on a peer that does not read its data |
| HTTP parsing | the request head is located by searching for the terminator from the start of the line; `Content-Length` accumulates within bounds and a peer-declared size has a hard cap | the original cut out a reversed range by searching `\r\n\r\n` from the beginning of the buffer, `std::stoi` read out of bounds, and a peer-declared size cannot be unbounded |
| HTTP chunked | rewritten to parse by block-length line | the original used `split("\r\n")` plus a string length comparison, which is wrong semantics |
| HTTP URL decoding | invalid hex no longer throws (the `%` is kept as it is)| `std::stoi(hex)` throws on a character that is not hex |
| `rpc::list_service` | takes the read lock | the receive thread reads the service table while the business threads install and uninstall, and the original walked it unlocked |
| `transmit::from_strip` | a hand-written bounded accumulation replaces `std::stoi` | an out-of-bounds value (segment > 255, port > 65535) or an empty segment would throw; and `isdigit` was given its `unsigned char` cast |
| the `decoder_lz4` entry (through `comm_ex`)| the decompressed size the peer claims is clamped | the original allocated memory straight from the claimed value |
| Server worker threads | owned by the server (`m_workers`) and no longer `detach`ed; `close(0)` and the destructor join every worker; the accept thread reaps the ones whose **done flag** is set after each `accept` (the worker sets it in its last step); the `close(0)` busy-wait is gone | the original detached them and tracked nothing: the destructor did not wait for them, while a worker erased its entry and then fired `disconnect` and took a lock ⇒ a host that "sees `connect_num()==0` and destructs" (what this repo's tests do) steps into UAF; and a `close(0)` that busy-waits for an empty table relies on "someone else erasing it". Reaping by "the entry is erased" would join a worker still running a host callback (measured: it held the accept thread for 360 ms, and waiting for the next connection inside a callback becomes a cycle)|
| Server socket closing | `wake_workers` and `close(loc)` only `shutdown`; each worker closes the fd once itself | the original `delete_socket`ed in both places and the worker's tail deleted the same fd again ⇒ the second `close` could hit a descriptor another thread had just reused |
| `local_server::close(loc)` | fires `(vid, false, "disconnect")` in place when it takes the socket out | it races the receive loop for the same `m_client`: when `close(loc)` wins the exchange the receive loop's tail sees no socket and fires no event at all -- on the tcp side the worker's finish-up fires it, and this was the one place where "whoever takes it out fires it" was missed |
| Connection-table state reads | `connect_num()` takes the read lock; `m_server` / `m_socket` became atomic | `m_clients.size()` races the accept and worker threads adding and removing entries, and the socket slot is written concurrently with the closing thread; the contract lets any thread read those two states |
| The `close()` of the three clients | the thread object and the TLS finish-up moved out of the "the slot is still there" condition | when the peer closes first the receive thread `exchange`s the slot away itself, and the whole of `close()` was skipped ⇒ the thread object and its resources (plus `SSL` / `SSL_CTX` on tls) stayed on the heap forever: LSan measured 93146 B in 548 places in `alxcomm_test`, and the transport side went to zero after the fix (the 530 B in 7 places left on the HTTP client path are the next row)|
| The `open()` of the three clients | the previous round's receive thread is released before a new connection is made, along with `SSL` / `SSL_CTX` on tls | a peer that closes first with the host reconnecting without a `close()` (which is exactly `comm`'s automatic reconnect) overwrote the old thread object and the old SSL object, and neither was ever reclaimed: LSan measured 8 B per round (tcp/local) and about 92 KB (tls)|
| The finish-up of `tls_client::close` | shares `m_ssl_mtx` with `bytes_send`; the sending side gained a null check inside the lock | the finish-up called `SSL_free` / `SSL_CTX_free` unlocked, and the sending side could be switched away between "the slot is fine" and "take the lock" ⇒ a null `SSL*` handed to `SSL_write` |
| `local_server::close(0)` | the accept thread re-checks "has this been closed" after storing the socket, and recycles and returns when it has | a close landing between the `accept` returning and the slot being stored: the `exchange` takes an empty slot and nobody deletes that connection ⇒ the accept thread blocks on its `recv` and the `close(0)` join never returns |
| The `http::client` destructor | releases the half-parsed `m_reply` | destructing the client while a reply is only half in (`Content-Length` / `chunked` not complete, a malformed head) left `m_reply` unreclaimed -- `m_promise` and `m_buffer` each have an owner, and `m_reply` was the one bare pointer missed. LSan measured 530 B in 7 places left in `alxcomm_test` (all indirect, the stack in the `new reply()` of `on_bytes_recv` and the two places filling its header map), and `alxcomm_test` went to zero overall after the fix |
| Connection events in `comm` | one protected virtual `on_trans_event(loc, connect)` added (empty by default), which `http::client` uses to void the in-flight request; the session layer fires no events of its own | the disconnect path only fired `mesg_prit`: the in-flight future blocked forever and `m_promise` never reset, so after a reconnect `exec()` was false forever and the client stayed unusable until it was destructed |
| The request state of `http::client` | a disconnect and the destructor go through `abort_pending()` (an empty `reply` lands and the state is cleared), and the disconnect one clears the send queue as well; serial use is the contract, so no lock | the original was race-free only under the assumption "the host uses it serially", and without the reset after a disconnect the future blocked forever and the client stayed unusable |
