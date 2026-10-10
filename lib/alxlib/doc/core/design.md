# alxcore — Design

This document records `alxcore`'s **design decisions, implementation points and tuning notes**.

## Design principles

1. **alxbase is the only dependency**: every submodule of core builds on base's `bytes` / `variant` / `varmap` / `ostream` and introduces no new third-party dependency (zlib / LZ4 / SQLite / libjpeg-turbo are already bundled under `3rdpty/` and exposed through a thin wrapper).
2. **Failure is not an exception**: as in base, failure is expressed uniformly as "an empty object returned / `false` returned / an error code returned". This holds especially for the paths whose input may come from outside -- archiving, compression, decompression.
3. **Long operations are interruptible**: operations that may run long, such as packing or unpacking an archive and running an external command, all offer an **external interrupt flag** (`set_abort_flag`) and a **timeout**, rather than leaving the caller with nothing to do but wait.
4. **State is explicit**: the handle classes (`file` / `sqlite` / `fiber` / `cache`) each have a clear `is_open` / `valid` / `get_state` check and do not rely on "the object exists, so it is usable".

---

## 1. File and stream

### 1.1 The abstraction in base, the implementation in core

`ostream` / `istream` are **pure virtual interfaces** and live in `alxbase` (`astream.h`), because base's serializer (`serer`) has to write to a stream and must not depend on core. core provides the concrete implementations `ostream_file` / `istream_file`.

The gain from that split: serialization, archiving and compression can be written against the interface alone, and the implementation can be a file, memory, a socket, or even "compress as it writes".

### 1.2 `ostream_file`'s layered buffer and `pre_exec`

`ostream_file` carries a user-space buffer of its own (32 KB by default) and reaches the disk only when the buffer fills; `flush()` forces it out. The constructor takes a fourth argument, `pre_exec`: **a transform applied to the data before every write-out** --

```cpp
ostream_file os(info, false, 0x8000, [](const void* d, uint64 n) { return compress(d, n); });
```

This is how "compress or encrypt on the way out" is implemented: the transform is per block (flush granularity), so the whole payload never has to be accumulated in memory.

### 1.3 `file`'s write and its cached size

After a successful `file::write`, the internal `file_info`'s `size` is advanced to `max(FTELL, the size it held)`; `open(WRIT / WRIT|R__W)` sets it to 0 explicitly (truncation semantics). That way `file::info().size()` is usable at once after a write, with no fresh stat.

> Revision log: early on, `write` did not update the cached size, so "read `info().size()` right after a write" gave the old value; `open()` also read `_info` **after** `close()`, and `close()` clears `m_info` -- on a self-aliasing call (`f.open(f.info(), ...)`) `_info` is bound to `m_info` itself and was cleared into "does not exist". `open()` now copies the `file_info` at its entry and works from that.

---

## 2. Compression

### 2.1 The static `sexec` and the instance `exec`

Every codec has two entry points, one per way of using it:

| Form | Semantics | Fits |
|---|---|---|
| `static sexec(...)` | one call does it all, building and destroying the context inside | occasional calls |
| instance `exec(...)` | reuses one context | a loop over many blocks |

The LZ4 context holds a dictionary and a hash table, which are expensive to rebuild, so a loop has to use the instance form. The same goes for gzip's zlib context.

### 2.2 How the output buffer grows

- **LZ4 decode**: the destination buffer is allocated once, at the original size the caller claims (LZ4's decode API needs the upper bound up front).
- **gzip decode**: the buffer starts at `min(compressed size * 4, 1 GiB)` (computed in 64 bits, to stay clear of an `int_32` overflow) and grows 2-fold when it fills, failing at 1 GiB -- a small input may not expand without bound.
- The `* 4` in gzip's initial value is a rule of thumb (text compresses at roughly that order) that avoids "even a small input keeps reallocating".

### 2.3 The size trust boundary

The codec interfaces take an "original size" argument that comes from the caller (possibly a field of external data). By design that number is **not trusted**:

- `decoder_lz4::sexec` clamps it to `min(claim, compressed length * 255 + 16)` -- LZ4 decodes at most 255 bytes per input byte, so a claim above that bound can only be forged (or corrupt). If the result size does not add up after clamping, the caller's own size check judges it wrong.
- A negative or zero `_size` / `_source_size` fails outright, without entering the algorithm. **Every codec entry point judges by this rule** (`_size <= 0`, not `0 == _size`): an entry point once let a negative value through, `deflateBound` wrapped into a small buffer and zlib then wrote past the end at the size it had been lied about -- a heap overflow write under ASan and a SIGSEGV in release (measured, pinned by `gt_acompress.negative_size_rejected_before_any_allocation`).

### 2.4 zstd

The third codec pair, **identical in shape to gzip** -- the caller picks the codec by name and the interface does not change with the codec. zstd's private knobs (the negative fast levels, dictionaries, the advanced `ZSTD_c_*` parameters, multithreading) are all left unexposed.

- **`sexec` produces a complete frame** (`ZSTD_compress`): one call, the frame header carries the content size, and the decode side can allocate once at the declared size. Measured 3690 B → 234 B, header declaring 3690.
- **`exec` is one frame as a continuous stream**: every call is `ZSTD_compressStream2(..., ZSTD_e_flush)` and **never `e_end`** -- the same shape as gzip `exec`'s `Z_SYNC_FLUSH` (`source/alxcore/acompress.cpp:288`). So whatever block `exec` produces, its output is an **unterminated frame**: a streaming decoder restores what is there in full, while `sexec` judges it a failure (the same reason gzip requires `Z_STREAM_END`). Measured, after two flushes the streaming decoder restored all 3690 B with `ZSTD_decompressStream` returning ≠ 0 (frame not terminated). **`exec` returns as soon as a frame decodes**, and the bytes after it in the same buffer are not processed -- a concatenation of frames is not the stream this class handles (gzip `exec` has the same shape; `sexec` fails visibly on multi-frame input).
- **`clear()` opens a new frame** (`ZSTD_CCtx_reset(session_only)`, parameters kept), synonymous with lz4's `LZ4_resetStream_fast` / gzip's `deflateReset`: it cuts the cross-block dictionary; **`reset()` frees the context**, and the next `exec` rebuilds it lazily (like gzip's `deflateEnd`). What `clear()` throws away is the **unterminated** frame (no end marker is appended, the same property as `deflateReset` not appending a trailer), so **the read side has to `clear()` at the same position** -- measured, feeding the decoder two pieces spliced across a `clear()` makes both this library and `zstd -d` fail (`Data corruption detected`), not silently return wrong data.
- **Level**: `uint_16`, the same type as gzip's -- no signed type is introduced and the negative level steps stay out of the interface. Valid 1..`ZSTD_maxCLevel()` (22 in 1.5.7), default 3 (zstd's own `ZSTD_CLEVEL_DEFAULT`), anything above the maximum is clamped by zstd itself, and `0` is accepted too (zstd reads it as the default level) -- the library does no validation. **Level values are not comparable across codecs**: gzip's 6 and zstd's 6 are not the same step.
- **The one behavioural difference**: zstd sets `_level` from the argument on every `exec` (zstd allows changing the level part-way), while gzip reads it once, when the context is initialized. The interfaces have the same shape, and this corner follows each library's own capability rather than being flattened away in the interface.
- **The decode buffer**: starts at `min(compressed size * 4, 1 GiB)`, grows 2-fold when full, and failing at the 1 GiB cap (the gzip policy of §2.2); `sexec` allocates once at the declared size when the frame header declares a content size, untouched by the growth, and a streaming frame (declaring unknown) goes through it.
- **No content checksum** (the library default `ZSTD_c_checksumFlag = 0`): structural damage is detectable, a change in the payload is not guaranteed to be -- the same property as lz4, with integrity the outer layer's job (`afpacker`'s CRC / the caller's hash). The boundaries are in `notice.md`.
- **The compress side has no absolute 1 GiB cap**: the 1 GiB in §2.2 is the **decode** bomb baffle. The input on the encode side is the caller's own data, `exec` sizes its buffer at twice that block's `compressBound` (leaving room for the flush) and `sexec` sizes once at `compressBound` -- neither side fails because "the output happened to pass 1 GiB".
- **Source**: `3rdpty/zstd` (1.5.7, BSD-3-Clause), a single-threaded static archive with no pthread dependency.

### 2.5 No virtual base class: the polymorphism sits on the stream

All three codec pairs are **concrete classes** with no common base -- the polymorphism is carried by the stream's transform hooks: `ostream_file`'s `pre_exec` (`include/alxcore/astream_ex.h:42`, `bytes(const void*, uint_64)`, may grow the data) and `istream_file`'s `pre_exec` (`:107`, `void(bytes&)`, rewritten in place). Swapping codecs means swapping a lambda, not swapping a derived class.

The reason is that the signatures are **the same shape with a different meaning**, and folding them into a base class could only unify them by deleting semantics:

- `decoder_lz4` wants the caller's claimed original size (`include/alxcore/acompress.h:118` / `:152`: the decode API needs the upper bound up front), while gzip/zstd grow by themselves. A base class could only take the least common denominator -- and lz4's DoS clamp semantics (§2.3) would be gone.
- One `uint_16` argument: for lz4 it is `_speed` (`include/alxcore/acompress.h:37`: 1 = best ratio, higher is faster), for gzip/zstd it is `_level` (1 = fastest, higher is better), **the opposite direction**. In a base class it could only be called `_param`, with a note that "the direction depends on the codec".
- `clear()` / `reset()` have the same shape with a different meaning: all three **cut the dictionary** (lz4's `LZ4_resetStream_fast` drops the dictionary reference, zstd opens a new frame, gzip's `deflateReset`), and each keeps only the buffers and parameters it has already allocated.

The same judgement has a precedent in the `regex` pair (`doc/core/design.md` §12.1): two concrete classes plus a gtest `static_assert` pinning "same names, same shapes", with **capability differences deliberately kept out of the common contract** -- "writing them into a base class sets a limit that does not take effect, a silent trap, while a host programming against the base class automatically loses" those capabilities. Should a caller that picks by codec id ever turn up (an archive opening a codec slot, a host configuration entry), the cheapest shape is **an enum plus dispatch** (or a `name → function` table on the host side), still not inheritance.

---

## 3. Encryption and digest

**Moved (2026-10-09)**: AES and the digest/checksum family have no dependency on the OS or on any
third-party code, so they were demoted to `alxbase` — see `doc/base/design.md` §10 and §11 (AES,
digest). What stays here is `fpacker`, the archive that builds on them.

## 4. Archive format (fpacker)

### 4.1 Layout

```
[FILE_HEAD:4][cmps_flag:4][aes_flag:4]   fixed header (three magic / flag slots)
[data blocks] * N                        per block: compressed length, original length, offset + data
[fsys index]                             the archive's file tree as a varmap (serialized with varsolid)
[index size:8][stored size:8][FILE_TAIL:4]   trailer (20 bytes: the index's original size, stored size, tail magic)
```

- Magics: `FILE_HEAD = 0x5F415A4C`, `FILE_TAIL = 0x5F455942`; the flag slots take `FILE_CMPS` (compressed) / `FILE_AESC` (encrypted) / `FILE_NONE`.
- **The flags sit in dedicated header slots**, so "is it encrypted / compressed" is decided before the fsys is parsed (`decoder::is_encrypt` / `is_compress` read the first 8 bytes directly), without decoding the whole archive.
- **The encryption parameters**: AES-CTR, with the key derived from the password by a single SHA-256 and the IV the pair (compressed length, offset in the archive). The properties, premises and boundaries are in §4.4.
- **The fsys index uses varsolid**: the file tree is itself a `varmap` and the serialization rules reuse base's framework, so `decoder::fsystem()` hands out a navigable `varmap` directly.

### 4.2 Interruptibility and progress

Every block boundary of packing and unpacking checks the `abort_` flag (an external `bool*`), and raising it returns `forceabort` as early as possible. Progress is reported through the `print_rtmsg_` callback (`path`, the total size of the entry being processed, how much of it is done; on failure the convention is `-1, errcode`).

### 4.3 Defensive parsing

The decode side treats an archive as **untrusted input** and validates its structure: parsing a block index requires at least 3 slots both at the outer and at the inner level (`badblock` otherwise), a data segment's length is checked against the length claimed, and the tail magic is checked. Historically the block index was only tested with `empty()`, which let `block_mesgs[2]` run out of bounds. The security properties and premises are in §4.4.

### 4.4 Security model

**Scope**: this section applies only with **compression and encryption both on**. The other settings (compression only / encryption only / neither) are file-archiving features with no security claim -- with encryption off there is no confidentiality to speak of, and with compression off a blind edit faces the CRC as its only check, the decompression filter being gone.

Threat model: **the adversary has no key but can obtain and modify the archive** (in storage and in transit). Every property below rests on **the key not leaking** -- once it does, confidentiality and integrity fail together: a holder can decrypt everything, and can also pack any self-consistent archive with the same key. The properties below all hold inside this model.

**Premises** (supplied by the caller; fpacker does not guarantee them)

- **The key**: `pswd` is normalized by the layer above and fpacker does one `SHA-256` to get 32 bytes (`source/alxcore/afpacker.cpp:42` / `:184`); salting and a slow hash belong to the shell. CTR's uniqueness requirement is on the **key** rather than on a single archive -- under one key, a (compressed length, offset) pair must not repeat across archives.
- **The entry names**: they come from `file_info` and are always a single-segment base name (`source/alxcore/afile.cpp:43` / `:65`; `get_child` filters out `.` / `..`, `:93` / `:104`), so no separator can enter the index and `save`'s `dir + "/" + key` cannot build an escaping path.

**Properties**

| Property | Basis |
|---|---|
| Confidentiality: without the key nothing readable | the payload and the index are encrypted in full; the IV is unique inside an archive -- the offset strictly increases there (`:154`) and the index follows all the data blocks, so no two slots repeat |
| Integrity: a blind edit is uncontrollable ⇒ forgery is out of reach, the worst case being a DoS | **editing a data block** (measured, 16 positions swept): it is either detected (decoding fails / the CRC disagrees) or decodes to a result **byte-for-byte identical to the original** -- LZ4 is not unique, and an edit landing in that freedom space changes the ciphertext rather than the content; every check happens before the write-out (`:307`–`:308`). **Editing the index** (measured, all 1403 bytes of a 40-file archive swept): 409 positions fail to parse outright and the rest land in `notexist` / `badblock` / `dcmpsfail` -- either way the attacker cannot produce the content it chose |
| Replay / splicing resistance | it takes the same (compressed length, offset) slot **and** a CRC hit, which is equivalent to "the content was the same to begin with"; block boundaries exist only inside the encrypted index, so without the key even the boundaries cannot be located |
| Compression couples the IV tightly to the content | the compressed length is a function of the content. Within one file the blocks continue one stream (the dictionary spans blocks, `source/alxcore/acompress.cpp:85` / `:106`); between files `clear()` cuts the dictionary (`source/alxcore/acompress.cpp:98`), and measured: compressing "a copy of f1's tail" inside one stream gives 25 B while after `clear()` it gives 4114 B, the same as compressing it on its own -- so **a file's compressed length is decided by that file alone, and only the offset is a prefix accumulation** |

**Boundaries** (not counted as defects)

- **Availability is not protected**: the 12-byte header and the 20-byte trailer are in the clear with no check, so 4 changed bytes can ruin the whole archive (setting `aes_flag` to `FILE_NONE`, say, makes `:239` drop the password), and no key is needed for that.
- **Per-block checking ⇒ a failure leaves half a file**: `get_impl` checks one block and writes it (`:307`), so when a later block fails the target file already holds part of it, and that part is not reclaimed.
- **The price of deterministic encryption**: two archives under one key have byte-for-byte identical ciphertext in the same (compressed length, offset) slot -- a byte-by-byte comparison locates "which ranges of the two agree", but yields no content, fields or block boundaries (compression does not change that reasoning: the same content ⇒ the same compressed stream ⇒ the same ciphertext; and conversely an XOR of the ciphertexts tells nothing, the index in ciphertext form being a compressed stream whose edits shift the literal/match split as a whole). The root cause is a key that is not unique, which is the first premise above, not the way the IV is chosen.

---

## 5. Image

- **One abstraction plus a depth template**: `image_base` provides width/height, row bytes, the buffer and the codecs; `image<1|4|8|16|24|32>` provides the pixel access. The pixel representation is specialized by depth: 1/4-bit are **bit fields** (`pixel<1>` uses one bit, `pixel<4>` a nibble), 16-bit is 5-6-5, and 24/32-bit is packed BGRA.
- **Row alignment**: a BMP row is aligned to 4 bytes (`align_line_bytes`); `line_bytes()` is the row width including padding and `usfu_line_bytes()` the useful width without it, the difference being the padding at the end of the row.
- **Size validation on the load path**: `create_image` refuses a size whose header declares more than 1 GiB (the check uses a division, since a multiplication would wrap at extreme widths and heights); the BMP parse checks the offset, the palette size, and how the data segment's length relates to the file's.
- **The codecs**: BMP is implemented here (reading and writing), JPEG goes through the bundled libjpeg-turbo.

---

## 6. Database

- **`sqlite` and `stmt` are both movable, non-copyable handles**: a move transfers the `db` / `stmt` pointer and nulls the source.
- **`stmt` does not own the `sqlite`**, holding a bare pointer instead: that is the convention "a statement derives from the connection and its lifetime is necessarily shorter", traded for zero overhead. The release order across the two objects is the caller's guarantee.
- **`close()` uses `sqlite3_close_v2`**: with statements not yet finalized the connection turns into a zombie and is released once they are destroyed, avoiding "closing the connection while a statement is still alive fails and leaks for good".

---

## 7. Concurrency facilities

### 7.1 Thread pool

`threadpool` = a work queue + N worker threads + an `in_flight` count:

- `enqueue` packs the task into a `std::function`, queues it and wakes one worker;
- `wait_until_empty()` waits for the queue to empty, `wait_until_nothing_in_flight()` for "the queue is empty and no task is still executing" (the latter is for "after submitting a batch, confirm that all of it is done");
- **The constructor arguments are clamped**: `_threads == 0` → 1 (otherwise the pool has no worker and tasks never run), `_queue_limit == 0` → 1.

### 7.2 `safe_queue`'s shutdown semantics

The design points of `destroy()`:

1. **Setting the flag and draining the queue sit in one critical section** (inside `THREAD_SAFE(*this)`), with `notify_all` outside the lock;
2. `b_destroy` is read as an atomic in the predicate, and `push` / `pop` take a quick-path decision **before locking**.

The reason: a condition variable's notification **does not queue** -- the waiter has to hold the lock, check the predicate and only then go to sleep. If "set the flag" and "drain the queue" took the lock twice, a waiter could pass the predicate check after the former and before the latter, go to sleep, and then neither see the queue non-empty nor receive a notification (the flag was already set), hanging for good. Merging the critical sections removes that window.

### 7.3 `safe_map`'s ownership

`safe_map` stores `T*` and **owns** them: destruction, replacement and removal all `delete`. The `call()` family invokes the member function **under the read lock** -- so a member function must not write to that map (it would deadlock against itself).

---

## 8. Logger

### 8.1 The two-level structure

```
log_property   the global format (time / thread / level / head line)
log_appender   one output sink (cout / file / win32), each with its own level threshold
logger         holds a property + a list of appenders
```

`logger::log` first assembles the prefix from the property, then hands the record to every appender whose level it passes.

### 8.2 The async appender

With `is_async` on, the `file` appender's producer only pushes strings into `out_queue` (bounded by `queue_limit` and blocking when full) while a worker thread of its own consumes them and writes them out. At the end (in the destructor) **the queue is swapped out under the lock** and then written in one go -- avoiding a race with a producer that is still pushing.

> Revision log: a `queue_limit` of 0 (or the key absent) blocked every producer for good, and is now clamped to 1024; `is_stop` became an atomic (the worker reads it outside the lock).

### 8.3 The global logger's lazy initialization

The fast path of `global_logger::log` is "read the atomic function pointer / read the atomic logger pointer", and when neither is there it **races to build the default logger with a CAS**: a thread that loses the race frees its own copy and returns the winner's pointer. That way "the first log record, concurrently" creates neither two default loggers nor a leak.

---

## 9. Fiber

- **The platform implementations are separate**: POSIX uses `ucontext` (`getcontext` / `makecontext` / `swapcontext`) and Windows uses `ConvertThreadToFiber` / `CreateFiberEx` / `SwitchToFiber`. The interface is exactly the same.
- **Stack management**: the POSIX side `mmap`s "the stack plus one guard page at the bottom" (`PROT_NONE`), so an overflow as the stack grows downwards faults at once with a `SIGSEGV`; **`malloc` + `mprotect` cannot be used** -- after `free` that page is still unwritable while the allocator has already taken it back as usable memory, and the next allocation to get that chunk crashes outright. The Windows side lets `CreateFiberEx` manage commit/reserve and grow it automatically.
- **The default stack is 1 MB**, matching the order of Windows' default growable amount.
- **An exception inside a fiber does not escape**: the entry function wraps a `try/catch`, logs an uncaught exception, sets the state to `FINISHED` and then yields back to the main fiber.

---

## 10. Cache

`cache` is the abstract interface (`put` / `get` / `remove` / `clear` plus a capacity), the concrete implementations register with `factory` by name, and `cache_pool` is the container of named caches, offering the "fill a miss with the factory" semantics of `get(entity, key, out, factory)`.

---

## 11. Platform layer

### 11.1 Running external commands

The structure of `exec_sync` is the classic `pipe` + `fork` + `dup2` + `execl("/bin/sh", "-c", cmd)`:

- the parent closes the write end and reads the pipe, calling `waitpid(WNOHANG)` as it reads to see whether the child has exited;
- the timeout is fired by a wait thread of its own: if the child has not exited by then, `SIGTERM` goes to its process group and `timeout` is set;
- **every fd close point collapses into the one `RET:` label** (`pipefd` initialized to `{-1, -1}` as the sentinel), so no error path misses one; the Windows branch does the same thing with `hWritePipe = NULL`.
- both stdout and stderr are `dup2`-ed onto one pipe, so `output` is the two of them merged (the order is not guaranteed).

### 11.2 Shared memory

`mmap` wraps "named shared memory + cross-process exclusion": POSIX with `shm_open` + `mmap` + a named semaphore, Windows with `CreateFileMapping` + `MapViewOfFile` + a named mutex. Every read and write interface locks internally, `cas` is a single-byte compare-and-swap, and `take` is "read and zero" (for taking an item out of a queue-shaped shared memory).

**Out of bounds always returns `false`**, judged with the subtraction form (`_offset > size || _size > size - _offset`) rather than an addition (which wraps in `uint_32` and lets a huge range through).

**The lifetime model of the name**: POSIX's `shm_unlink` "removes the name" rather than "destroys the memory" -- an established mapping keeps reading and writing after the name is gone, and the object is only really released when the last mapping or fd closes; and `sem_close` merely lets go while `sem_unlink` removes the name. Hence: `close()` only does munmap / `close(fd)` / `sem_close` (no unlink, callable by any holder at any time), `destroy()` is what removes the two names (idempotent, `ENOENT` counting as success; **the data name first, then the semaphore**, so a latecomer fails at `shm_open` rather than `O_CREAT`-ing a semaphore of a new generation), and destruction branches on `owner()`: the creator `destroy()`, an attacher `close()`. To make a name outlive the creator object, call `close()` explicitly (`close()` clears `owner`, so the destructor naturally unlinks nothing).

**Deciding the creator**: `shm_open(O_CREAT | O_EXCL)` decides it atomically, and the losing side degrades to a plain `O_RDWR` and **does not `ftruncate`** -- the old "probe first, then `O_CREAT`" let both processes believe they had created it, and the later one `ftruncate`-d the existing block, cutting off the other's data. The attaching side checks the size with `fstat` (below the request it fails), otherwise the pages past EOF would SIGBUS; the Windows side already had the same kind of check through `VirtualQuery` + `RegionSize`.

**An attacher's `close()` used to remove the name**: the old `close()` ran `shm_unlink` unconditionally for every instance, so in a "create once, attach repeatedly from many processes" use the first attacher to exit removed the name and every later `open(..., false)` failed (measured on a downstream host: the first request worked, the second onwards failed). The companion semaphore was only ever `sem_close`-d and never `sem_unlink`-d, so every change of name left one `sem.<name>_sem` behind in `/dev/shm` (56 sessions in a day left 56). Both are eliminated by this lifetime model.

### 11.3 Process control (`process_ctrl`)

`process_ctrl` runs one child process over pipes + `fork`/`execv` (Windows using `CreatePipe` + `CreateProcessW`), and a `loop()` thread delivers its stdout/stderr as **raw blocks** into `sig_output`/`sig_errput` -- one `read()` of at most 4096 bytes, **not split into lines**: several lines or half a line may turn up in one callback, the trailing `\n` included as it stands.

Killing and reaping have a single stance:

- **Two kill interfaces**: `terminate()` soft (POSIX `SIGTERM`; Windows can only close its stdin -- irreversible, and useless when the child does not read stdin), `kill()` hard (`SIGKILL` / `TerminateProcess`). `kill()` reaps on the way, joins `loop()` and closes the three pipes, putting the object back into its "not started" state (`pid()` back at 0); it is idempotent, returning `false` with the child already dead yet still completing that release.
- **Destruction = `kill()`**: no zombie is left, and a live child is killed hard.
- **No automatic reaping**: `status()` / `wait()` are explicit questions, the reaping (`waitpid` + `WIFSIGNALED`/`WEXITSTATUS`) happens at that instant, and the cached value comes back afterwards. That gives the path "not the one we killed" (the child crashing or exiting on its own) somewhere to get the outcome too; `alive()` was replaced by `status()` for this reason: `pid() != 0 && !exited` = still running. The state sticks until the next `start()`.
- **The kill path does not preserve the last words**: after the reaping `p_id` goes to 0 first, `loop()` exits on its next round, and the output left in the pipes is not guaranteed to be delivered.
- **"reaped" ≠ "it said everything"**: what `wait()` / `status()` wait for is the child being taken by `waitpid`, while the `loop()` thread is still delivering the bytes left in the pipes block by block -- one stdout read per round plus one 10 ms `select` timeout for stderr, so a tail of ten thousand bytes takes tens of milliseconds. Reading the accumulated output during that sees half of it, and **releasing the handle (`kill()` / destruction) at that moment loses the remaining blocks for good**, the natural-exit path being no different. `output_done()` is the only "no more callbacks" that can be judged: both pipes at EOF (on Windows = `ReadFile` reporting `ERROR_BROKEN_PIPE` / `ERROR_HANDLE_EOF`), or cut by `kill()`, set it; `start()` re-arms it.
- **`input()` writing into a dead child's pipe no longer kills itself**: on POSIX writing to a pipe whose reader is gone raises `SIGPIPE`, whose default action terminates this process outright. The library **does not take over that process-level signal disposition** (it is the host's) and probes liveness before the write instead: when `status()` says the child has ended it returns `false` right away (reaping the body on the way). A very small window remains between the probe and the `write`, so it can still in theory be hit -- traded for "not polluting the host process".
- **The callbacks run on the `loop()` thread**, and `kill()` / destruction join it ⇒ a callback must not wait on a lock the calling thread holds.
- **The single-thread model**: one instance belongs to one calling thread -- the library adds no lock, and concurrent `status()` / `wait()` / `kill()` / `input()` on one instance fight each other (two threads reaping at once). Across instances there is no shared state, each carries its own `loop()` thread, and concurrency is free; locking multi-threaded access to one instance is the caller's job.
- **Windows difference**: the `signaled` / `term_sig` fields do not exist on that platform (there are no signals to report); `kill()` uses `TerminateProcess(…, 1)`, so `exit_code == 1` cannot be told apart from "the child ran `exit(1)` itself"; the reaping goes through `WaitForSingleObject` + `GetExitCodeProcess` and only then `CloseHandle` (once the handle closes, the exit code is out of reach for good).

### 11.4 Process info (`process_info`)

Only five fields are exposed: `pid` / `ppid` / `thread_cnt` / `base_priority` / `path`. **The process name is no longer stored** -- the caller splits it with `file_info(path).name()` (`file_info::name_` is a plain string split, computed outside the `exist_` test, so a path that does not exist still gives a value).

- **`path` has exactly one source**: `readlink("/proc/<pid>/exe")` / `QueryFullProcessImageNameA`. An unreadable one is an empty string (a POSIX kernel thread has no exe, a cross-user one may be held back by ptrace permission, and a restricted or system process on Windows fails `OpenProcess`), and the library offers no fallback and no `argv[0]` or `comm` standing in -- those are strings the caller can write at will, not an identity.
- **`find_process(name)` matches on `basename(path)`**: steadier than matching on `comm` (a process cannot disguise itself with `PR_SET_NAME`), at the cost of taking the path once per entry while scanning.
- **`current()` and `find_process(pid)` share one single-process parse** (`read_process(pid)`) and do not sweep `/proc` just to "get myself". On Windows `current()` goes the same way.
- **`/proc/<pid>/stat` is read whole and then parsed**, anchoring on the **last** `')'` to take the fields after it: `comm` may hold spaces, right brackets and even newlines (writing `/proc/self/comm` does not strip a trailing newline, so a bare `\n` lands in the stat line and reading line by line truncates it to `pid (comm`).
- **Narrow strings + the Windows A-version APIs**, the same stance as `file` / `file_info` (see `notice.md`: A = the system ANSI code page).

### 11.5 Process limits (`process_limits`)

The limits of `start(args, limits)` are **all or nothing**: if any one guarantee asked for cannot be made, the child is not started and `start()` returns `false`.

- **POSIX**: set between `fork` and `exec`. The memory wall = `RLIMIT_AS`, with `rlim_cur` and `rlim_max` set **together** -- once lowered, a hard limit cannot be raised again, so the child cannot lift it; parent death = `prctl(PR_SET_PDEATHSIG, SIGKILL)`, followed by one more `getppid()` check that the parent is still the original (the classic race). A platform without PDEATHSIG **makes no** false promise of "ignore that field" and simply fails.
- **Windows**: a job object. `CreateJobObjectA` → `SetInformationJobObject` (`JOB_OBJECT_LIMIT_PROCESS_MEMORY` caps **commit**, which is not the same thing as POSIX's address space; `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` is parent death) → `CreateProcessA(..., CREATE_SUSPENDED, ...)` → `AssignProcessToJobObject` → `ResumeThread`. **Cover it first, then let it run**, so there is no "allocate before the limit" window; closing the job handle = killing everything left in the tree.
- **The status pipe (POSIX only)**: a pipe whose write end carries `FD_CLOEXEC` is created before the `fork`. A successful `exec` in the child makes the kernel close the write end automatically ⇒ the parent reads **EOF** = success; a child that dies before `exec` (a limit that could not be set / an `exec` that failed) writes errno back and `_exit(126/127)` ⇒ the parent reads **data** = failure. `start()` therefore has synchronous semantics: returning `true` ⇔ the child is already running the requested image, which also does away with the fork/exec window. Windows needs none of it -- `CreateProcessA` is synchronous to begin with.
- **The measured stance** (POSIX): `ulimit -v` inside the child reads back the value that was set; under a 64 MiB wall a 128 MiB allocation fails (the no-wall control group succeeds, exit code 0); with `kill_on_parent_exit` the child has been taken by the kernel 0.8 s after the parent exited; a nonexistent executable ⇒ `start()` returns `false` and leaves no zombie.
- **`RLIMIT_AS` does not stop `exec`**: measured, `RLIMIT_AS=1` still `exec`s successfully (the program that comes up then dies of a failed allocation of its own). The memory wall governs "the allocations once it runs", not "whether it can be started".

---

## 12. Regex (PCRE2)

### 12.1 Why two engines rather than one abstract base class

`regex_ex` (base) is nailed to `std::regex`: standard library only, which is how base stays dependency-free; the price is that its **cost has no bound** and no interrupt can be inserted (`<regex>` has neither a step knob nor a callback point). To fix "no bound" the engine has to change, and base's "zero dependencies" is a documented property that must not be broken for it.

So it splits into two concrete classes with no virtual base: `regex_ex` stays in base untouched (it serves trustworthy input) and `regex_pcre2` lands in core (which already has the 3rdpty convention of zlib / lz4 / sqlite3 / jpeg). Their common contract is only "same names, same shapes" -- 7 matching methods plus `is_valid` / `get_pattern`, pinned one by one by a `static_assert` in the gtest (`std::is_same_v` compares the member pointer types on the two sides, so either side drifting fails to compile). **Boundedness and interruption are deliberately kept out of any public contract**: `std::regex` cannot deliver them, so writing them into a base class would be the silent trap of "a limit is set and does not take effect", and a host programming against the base class automatically loses both.

### 12.2 AUTO_CALLOUT is mandatory

The constructor always passes `PCRE2_AUTO_CALLOUT`: the callout items are inserted into the pattern at **compile time**, and `set_callout()` only hangs a callback on the match context. Without them compiled in, a later `set_callout()` reports no error and is never called -- the host holds an interrupt hook that has silently stopped working, which is worse than having no hook at all.

### 12.3 What a callout's return value means

`pcre2callout.3:379-389`: returning `0` continues; **`> 0` fails only the current position** (equivalent to a failed lookahead assertion), leaving the other possibilities to be tried; **`< 0` abandons the whole match** and returns that negative value as it stands. `PCRE2_ERROR_CALLOUT` is left for the callout itself to return (PCRE2 never uses it internally). So when the host says "stop", the bridge returns `-37`, not `1`.

A measured comparison (taken this time with a one-off probe calling the PCRE2 C API directly): returning `1` makes the engine keep trying up to 1029 times and finally report `NOMATCH` -- disguising "it was stopped" as "nothing matched"; returning `-1` abandons the whole match but reports `NOMATCH` (a disguise as well); only `-37` travels all the way to the host and maps to `interrupted`. That chain is permanently pinned by `callout_interrupts_a_running_match` in `gtest/alxcore/src/gt_aregex_pcre2.cpp`.

### 12.4 The caps and their defaults

The step cap is written into the match context (`pcre2_set_match_limit`). `0` means "back to the engine default", implemented by querying `pcre2_config(PCRE2_CONFIG_MATCHLIMIT)` right there and writing it back, **not by hard-coding 10⁷** (so a library upgrade cannot quietly start lying). The depth cap works the same way (its factory default is the step cap's value). PCRE2's JIT is not in the library -- the build leaves `--enable-jit` at its default (off) -- and `pcre2_jit_compile` is never called: the present behaviour is the interpreter's and is predictable. (A JIT run would start with rebuilding PCRE2 with `--enable-jit` and calling that entry.)

PCRE2's own "required bytes" pre-scan incidentally took care of that original 165 s repro: `(a+)+b` against 30 `a`s backtracks not one step (not even a 1000-step budget is touched). What the step cap really has to hold back is what the pre-scan cannot cut, such as `^(a+)+$` against 30 `a`s plus `!`.

### 12.5 Concurrency: one apiece, the hook to the instance

Only the read-only part can be shared across threads: the compiled result (`pcre2api.3:576-580`) and a match context that is not modified (`:654-657`). **The match block is a hard one-per-thread constraint** (`:663-667`, "Each thread must provide its own copy of this memory"), and `status_` / `last_err_` are written during a match -- so one instance can only belong to one execution agent: methods on one instance must not run concurrently, and neither may a setter run concurrently with a match (`set_callout` being a use-after-free: it frees the old binding, and that is what the bridge of the running match reads).

**Copying was rejected.** Both semantics were tried and neither was clean: **inheriting the callout** → two matchers share one stop signal (A's "stop" aborts B's running match as well, and the callback cannot tell whose it is, which stays unsolved even with the match block and the status independent); **not inheriting the callout** → one more invisible difference between the original and the copy. The judgement was "rather than leave that obscurity, forbid copying", and the cost of the decision is small: parallel work only needs one instance built per thread with its own hook, the extra being one pattern compile (µs).

One measured number along the way: were it changed to "build a fresh match block per call" to open up same-object concurrency, the cost would be **+30 ns per call** (+52~63% on a small pattern and a short subject), and it would **still not settle the hook's ownership** -- that needs the hook turned into a per-call argument, which is an interface-generation change. So this is not done.

**The library adds no lock and does no detection** (the same stance as `process_ctrl`: one instance does not run concurrently, across instances concurrency is free): the consequences of a concurrent call are the caller's to bear. All of this is written down in the header, in `api.md` §14 and in `notice.md`.

## 13. Revision log (behaviour changes that touch the design)

| Topic | Change | Reason |
|---|---|---|
| `file::open` | the entry copies the `file_info` first | on a self-aliasing call `close()` clears `m_info`, leaving "the file was never opened" |
| `file::read_all` | reads to EOF instead: `st_size`/`ftell` only hint at the capacity to preallocate, and a final `fitsize()` settles it | procfs reports an `st_size` of 0 yet has content, and a FIFO cannot seek; the old implementation went by "the file length" and read both silently as empty |
| `file::read` | a positional read requires a seekable source, otherwise it returns empty and reports failure; reading past EOF still counts as success | the old implementation's positional read on a FIFO/socket/tty returned empty in silence, and the caller could not tell it from "an empty file" |
| `file::read_all` / `file::read` | a new `bool* _ok` out-parameter, written on every path (the same convention as `ajson`'s `_ok`). Default arguments would change the exported symbols, making this an interface-generation change | under the old signature "the read failed" and "the file was empty to begin with" were the same answer: `bytes()` is `null()`, and `resize(0)` is a no-op on null |
| `file::flush` | `void` → `bool`, returning what `fflush` decided | the old implementation dropped `fflush`'s return value, so the caller could not know when the buffer had not reached the disk |
| `ostream_file`'s write-out callback | `(file_.flush(), true)` becomes a `&&` chain | the lambda declared `bool` yet reported success unconditionally, swallowing a failed flush after a write |
| `file::write` | goes through `write_stream`, shared with `write_all` (retrying a short write) | the original judged on a single `fwrite`, so a short write counted as a total failure and the first half stayed in the file |
| `crc_32_c` | the hardware path's gate changes from `is_sha_ni_supported_v` to `is_crc32_supported_v` (in both `update` and `hexdigest`); `bytedigest()` gains the software branch | `_mm_crc32_u64` wants SSE4.2's CRC32 (leaf 1 / ECX[20]) while that flag tested for SHA-NI (leaf 7 / EBX[29]), and `hardcal()` used the correct flag, so the three disagreed. On a CPU with SSE4.2 but no SHA-NI, `update` silently fell back to software while `bytedigest()` computed unconditionally in the hardware form ⇒ **a wrong digest value** (`acomm_ex`'s message checksum goes through `bytedigest`). The software path's byte order differs from the hardware path's, and `hexdigest()` had split the two branches long before while `bytedigest()` was the only one that had not; both now branch on `is_crc32_supported_v`, and on a CPU without CRC32 the two exits give the same value (measured: both `e3069283` with the feature bit pinned)|
| `averify`'s software transform | the alignment responsibility moves to the boundary: `update_soft`'s **whole-block loop** `memcpy`s the caller's 64 bytes into `m_cache` first, so `transform_soft` receives only aligned buffers and keeps reading straight by `uint_32` (the other two paths already landed on `m_cache` and are untouched)| `verify::update(const uint_8*, ...)` is a public virtual function and `_data` may be any address; reading straight by `uint_32` is an unaligned access (measured under UBSAN: `load of misaligned address ... requires 4 byte alignment`). The aligned bytes are read directly on the internal buffer, and only that one boundary `memcpy` is unrelated to alignment |
| `file::write` | advances the cached size after a write; `open(WRIT)` sets it to 0 | makes `info().size()` usable at once |
| `aes_base::ghash_hard` / `ghash_mul_hard` | `public` → `private` (matching the `cipher_impl_hard` family), reachable only through the self-probing `ghash()` | their semantics require a CPU with PCLMULQDQ and the library exposes no public PCLMULQDQ probe (`hardcal()` reports AES-NI alone), so a caller had no way to check whether the contract held; in the same batch these two actually execute PCLMULQDQ (before that they merely forwarded to the software implementation), so the precondition went from "on paper" to "a real condition" |
| `sqlite::close` | switched to `sqlite3_close_v2` | with statements not yet finalized the connection is no longer leaked for good |
| `sqlite::get_text` | a NULL column returns an empty string; the length comes from `column_bytes` | the original constructed a `std::string` from `(const char*)nullptr` (UB) |
| `logger`'s async queue | `queue_limit` 0 clamped to 1024; `is_stop` turned atomic; the finishing swap done under the lock | 0 blocks every producer for good; taking the lock twice for the flag and the queue loses a wakeup |
| `safe_queue::destroy/reset` | the flag and the queue drain merged into one critical section | notifications do not queue, so taking the lock twice leaves a lost-wakeup window |
| `threadpool` | `_threads == 0` clamped to 1 | the original ran zero workers and tasks never executed |
| `fiber` (POSIX) | 1 MB default stack, `mmap` + a guard page | the original defaulted to 8 KB; a guard page cannot be built on a malloc'ed block |
| `fpacker` block index (inner) | requires at least 3 slots | the original only tested `empty()`, so `[2]` ran out of bounds |
| `fpacker` block index (outer) | requires at least 3 slots, consistent with the inner one | the original only tested the parity: an entry with 1 slot left the read loop spinning, `total` took its default 0, and the final `0 != 0` passed → a silent success with empty content |
| `image::load_image_bmp` | checks the offset, the data segment's length and the row-copy bounds | the original read and wrote out of bounds in several places, driven by header fields |
| the size check at a codec entry point | unified to `_size <= 0` (it was `0 == _size`) | a negative size reaches `deflateBound` and wraps the result into a small buffer, after which zlib writes out of bounds at the size it was lied about: a heap overflow write under ASan and a SIGSEGV in release (reproduced). The lz4 encode side was caught by `LZ4_COMPRESSBOUND <= 0`, safe behaviour but an inconsistent check |
| `decoder_gzip`'s `_size * 4` | widened to `uint_64` before clamping to the 1 GiB cap, and the growth path is capped too | with a claimed compressed length ≥ 512 MiB the multiplication wraps into a negative number inside `int`, and once widened to `uint_64` an allocation that wraps in `bytes` leaves "a small buffer with a large size claimed", so zlib writes out of bounds (measured under ASan as `heap-buffer-overflow`; in release it silently allocates 4 GiB) |
| the content-size check in `decoder_zstd::sexec` | `CONTENTSIZE_ERROR` is tested first, and the 1 GiB cap is only compared against a frame that **declares** a size | `CONTENTSIZE_UNKNOWN` is `2^64-1`, was taken as the claimed size and went straight over the cap ⇒ every frame without a content size (the streaming output of `zstd -3`, hand-made frames) was rejected by one-shot decompression and the doubling branch was dead code |
| the claimed size in `decoder_lz4::exec` | clamped exactly as `sexec` does: `min(claim, compressed length * 255 + 16)` | two paths of the same family gave different answers to one forged claim: `sexec` clamped to ≈6.5 KB (a 26-byte compressed stream ⇒ `6646 B`) while `exec` allocated by the claim as it stood (measured: a 2 GiB virtual reservation, VmPeak +2097156 KB). The bound is LZ4's worst-case expansion, so a legitimate stream is unaffected |
| `mmap::set` / `MMAP_PROTECT` | the bounds check becomes the subtraction form | the addition wraps in `uint_32` and lets a huge range through |
| `mmap` zero length | a `_size == 0` in `write` / `read` / `set` / `take` returns `true` before the lock and the bounds check | a no-op was being taken as a failure: the stopping rule at `ostream`'s exit is "stop as soon as `append()` returns false", while `ajson`'s escape writer pushes "the text since the last escape point", which is 0 bytes for an empty string, when the first character needs escaping, and between two adjacent escapes ⇒ **a host using `mmap` as its sink judged "it does not fit" at the very first escape in the body** and dropped the whole reply (measured on a downstream host: a 33 MB and a 128 KB return value died at the same point, with the symptom pointing at "the value is too large"). The contract is written into `ostream::append`'s comment as well |
| `mmap` lifetime | `close()` only detaches this object (no more `shm_unlink`); a new `destroy()` (idempotent, removing the data name plus the companion semaphore's, the data first) and `owner()`; destruction branches on `owner()` (creator destroy, attacher close); segment creation takes the name with `O_CREAT\|O_EXCL` (the losing side does not `ftruncate`), and an attacher checks the size with `fstat` | any attacher's `close()` removed the name, failing every later attach; the companion semaphore was never `sem_unlink`-d, leaving one file per session in `/dev/shm`; with `O_CREAT` and no `O_EXCL` two processes both "created" it and `ftruncate`-d each other's block away; an attacher that did not check the size faulted on the pages past the end |
| `exec_sync` | every fd close point unified at `RET:` | error paths missed descriptors |
| `process_ctrl::output_done` | new: set once both pipes are at EOF or `kill()` cut them, re-armed by `start()` | output may still be arriving when `wait()` / `status()` return, and a caller that releases the handle before that loses the tail for good (measured: 4 rounds out of 300) |
| Windows `process_ctrl::loop` | gains EOF detection (`ERROR_BROKEN_PIPE` / `ERROR_HANDLE_EOF`), the same stance as POSIX | otherwise `output_done()` holds on that platform only under `kill()`, and "wait for the drain" is meaningless |
| `process_ctrl` | `close()`/`alive()` deleted, replaced by `terminate()`/`kill()`/`status()`/`wait()`; destruction hard-kills and reaps | `close()` sent `SIGTERM` (the child may ignore it) and reaped with no timeout, hanging for good; `alive()` reaped but dropped the exit code, and signalled an already-reaped pid again |
| `process_info` | the `name` field (a wide string, from `comm`/`szExeFile`) becomes `path` (a narrow string, the real exe); `find_process(name)` matches on `basename(path)`; `current()` added | `comm` is truncated to 15 characters and can be forged with `PR_SET_NAME`, and `/proc/<pid>/stat` cannot be read line by line because comm may contain newlines; the path is the stable identity |
| platform-layer strings | `process_info` / `process_ctrl` / `dll_version` all move to narrow strings + the Windows A-version APIs | unifies the existing stance of `file` / `file_info`, and POSIX no longer does pointless wide/narrow conversions |
| `process_ctrl::start` | gains a `limits` argument (memory wall / parent death) and does not start unless every item holds; `start()` becomes synchronous (returning means `exec` has happened) and reports an `exec` failure as well | a cap has to be imposed by the **parent** and "not capped" has to mean "not started"; the status pipe does away with the fork/exec window along the way |
| regex | new `regex_pcre2` (core, PCRE2 10.48: step cap + callout interrupt); `regex_ex` stays in base untouched | see §12: `std::regex`'s cost has no bound and cannot be interrupted, and base's zero-dependency stance cannot be given up |
