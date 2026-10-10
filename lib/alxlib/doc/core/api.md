# alxcore — API Manual

`alxcore` is the **middle layer** of AlxLib: file and stream, compression, archive, image, database, thread pool and thread-safe containers, logger, fiber, cache, platform adaptation layer and regex. It depends on `alxbase` and is depended on by `alxcomm` / `alxscpt`.

- Header directory: `include/alxcore/`
- Library: `libalxcore.so` (or the merged static library `alxlib.a`)
- Namespace: `alx` (compression in `alx::compress`, archiving in `alx::fpacker`)

```bash
g++ -std=c++17 -I include -I include/alxbase -I include/alxcore main.cpp \
    -L bin -lalxcore -lalxbase -Wl,-rpath,$PWD/bin
```

This manual covers **how to use it**; the limits that touch each area are explained where they come up.

---

## 1. File and path (`afile.h`)

### 1.1 `file_info` — metadata snapshot

Collected at construction: path, name, suffix, size, three timestamps (creation / access / modification), and whether it is a directory / exists / is a link.

```cpp
alx::file_info info("/tmp/a.txt");
info.is_exist();  info.is_dir();  info.is_link();  info.is_valid();
info.path();  info.name();  info.suffix();  info.size();
info.time_ct();  info.time_la();  info.time_lw();
alx::file_info::conver_file_time(info.time_lw());     // → datetime (now in alxbase, see doc/base/api.md §13)

info.get_parent();                                     // the containing directory's file_info
info.get_child("*.txt");                               // entry list (wildcards accepted)
info.mkdir();

alx::file_info::mkdir("/tmp/x");      alx::file_info::rmdir("/tmp/x");
alx::file_info::rmfile("/tmp/a.txt"); alx::file_info::mvfile("/a", "/b");
alx::file_info::calsize(info, true);  // recursive size, true = skip links
alx::file_info::is_absolute(path);    // cross-platform absolute-path test
alx::file_info::conver_std_path(p);   // separators normalized
```

### 1.2 `file` — file handle

```cpp
// one-shot read and write
alx::bytes all = alx::file::read_all("/tmp/a.txt", &ok);   // reads to EOF; _ok reports what happened (an empty file is true too)
alx::file::write_all("/tmp/a.txt", data, size, /*append=*/false);   // there is also a T version (needs .data()/.size())

// handle style
alx::file f;
f.open(alx::file_info("/tmp/a.txt"), alx::file::READ | alx::file::R__W);
```

`IO_TYPE` is a bit mask, but only six combinations are accepted: `READ` ("rb"), `WRIT` ("wb"), `APED` ("ab"), or any one of the three OR-ed with `R__W` ("rb+"/"wb+"/"ab+"). Anything else -- `NONE`, `READ | WRIT`, `R__W` alone -- makes `open()` fail.

```cpp
f.is_open();  f.io_type();  f.info();  f.is_read_able();  f.is_write_able();
alx::bytes part = f.read(pos, size, &ok);  // position + length; ok=false when the source cannot seek
f.write(data, size, pos);                  // with pos omitted, writes at the current position
f.write(bytes_view / std::string / std::wstring);
f.flush();  f.close();                     // flush returns whether the data reached the disk
```

`read_all` goes by EOF rather than `st_size`, so sources like procfs and FIFOs that "report 0 bytes yet have content" come back complete;
`read()` is a positional read and holds only on a seekable source. `file` is `noncopyable`, and its destructor runs `close()`.

---

## 2. Streams (`astream_ex.h`)

`alxbase` provides the abstract `ostream` / `istream`; `alxcore` provides the concrete implementations: file streams.

```cpp
// output to a file (buffered)
alx::ostream_file os(alx::file_info("/tmp/out.bin"), /*append=*/false, /*buf_size=*/0x8000);
os << std::string("hello") << some_pod;
os.append(ptr, size);         // appending directly works too
os.flush();  os.total();  os.reset();   // reset reopens with the mode the constructor was given

// read from a file (zero-copy view)
alx::istream_file is(alx::file_info("/tmp/out.bin"));
alx::bytes_view chunk = is.get(0, 1024);
is.total();

// in-memory streams (alxbase)
alx::bytes buff;  alx::ostream_buff om(buff);
alx::istream_buff im(alx::bytes_view(buff));
```

`ostream_file` takes a transform callback as its fourth argument, `std::function<bytes(const void*, uint64)>` (compressing or encrypting on the way out, say).

---

## 3. Compression (`acompress.h`, namespace `alx::compress`)

Six classes (one pair each for lz4 / gzip / zstd), each with a static one-shot `sexec` (returning `bytes` or filling a `bytes&`) and an instance `exec` (reusing the stream context). **The decode side has a hard 1 GiB output cap** (gzip / zstd): the buffer starts at `min(compressed size * 4, 1 GiB)` and reaching the cap fails the whole call (an empty result), rather than succeeding truncated.

```cpp
// LZ4
alx::bytes packed = alx::compress::encoder_lz4::sexec(src, /*speed=*/1);
alx::bytes orig   = alx::compress::decoder_lz4::sexec(packed, /*original size*/ (int_32) src.size());

// gzip
alx::bytes gz   = alx::compress::encoder_gzip::sexec(src, /*level=*/6);
alx::bytes back = alx::compress::decoder_gzip::sexec(gz);

// zstd (a different level scale from gzip: 1..22, default 3, the two numbers are not comparable)
alx::bytes zst  = alx::compress::encoder_zstd::sexec(src, /*level=*/3);
alx::bytes raw  = alx::compress::decoder_zstd::sexec(zst);

// use the output-parameter form when a `true/false` answer is wanted
alx::bytes out;
if (!alx::compress::decoder_gzip::sexec(gz, out)) { /* failed */ }

// reuse the context (no rebuild per call)
alx::compress::encoder_lz4 enc;
alx::bytes a = enc.exec(src1);  alx::bytes b = enc.exec(src2);
enc.reset();
```

`sexec` produces a **complete frame/stream**; `exec` on gzip / zstd produces the **unterminated** piece (continuous across calls, `clear()` opens the next frame), so that piece reads back through `decoder::exec` only -- handed to `decoder::sexec` it fails the check, which `zstd -d` confirmed on zstd (reporting `premature end`). lz4 differs: it is a **block** codec, each `exec` block carries a complete block structure, and only the blocks that reference the previous block as a dictionary cannot be read back (`decoder::sexec` succeeds on the first block and fails on the later ones).

Failure always shows up as an **empty `bytes`** (`null()` true) or `false`; nothing throws.

---

## 4. Encryption and digest

**Moved (2026-10-09)**: AES and the digest/checksum family now live in `alxbase` — see
`doc/base/api.md` §11 and §12.

## 5. Archive packing (`afpacker.h`, namespace `alx::fpacker`)

Packs a directory tree into one self-describing archive: an fsys index plus the data in blocks, each block optionally LZ4-compressed and AES-encrypted.

```cpp
// pack
alx::bytes out;
alx::ostream_buff ostm(out);
alx::fpacker::encoder enc(/*cmps=*/true, /*password=*/"pwd", /*block=*/0x04000000U);
enc.start(&ostm);
enc.append(alx::file_info("/data/dir"), "root");       // a directory or a file; the second argument is the root name inside the archive
enc.finish();

// unpack
alx::istream_buff istm(alx::bytes_view(out));
alx::fpacker::decoder dec(&istm, "pwd");               // takes ownership of istm and deletes it in the destructor
dec.valid();                                           // whether the fsys parsed
dec.fsystem();                                         // the file tree inside the archive (varmap)

alx::bytes content;
dec.get("root", {"a.txt"}, content);                   // pull one file into memory
dec.get("root", {"a.txt"}, ostm);                      // or write it into an output stream
dec.save("root", {}, alx::file_info("/tmp/dst"));      // onto disk; with _root empty or "*" the whole archive is extracted
```

- The result codes are `uint_8`: `success` / `notexist` / `notfile` / `badfsys` / `badblock` / `dcmpsfail` / `forceabort` …, and `error_message(code)` gives their text.
- Progress and error callback: `set_print_rtmsg(MesgFunc)`, signature `(path, v1, v2)`: when `v1 == -1`, `v2` is the error code; otherwise `v1` is the total size of the entry being processed and `v2` how much of it is done.
- Interrupt: `set_abort_flag(&flag)`; raising the flag from outside returns `forceabort` as soon as possible.
- Silent ignore: `set_igerr_notexist` / `set_igerr_linkfile` control whether "the file does not exist / is a symbolic link" counts as an error.
- **Keeping the key unique is the caller's job**: do not pack two archives with one `password` (CTR's uniqueness requirement is on the key, not on a single archive), and salting and key stretching are the caller's job too. The full security model and its boundaries are in `design.md` §4.4.

---

## 6. Image (`aimage.h`)

Unified behind `image_base` (abstract), instantiated by depth as `image<1|4|8|16|24|32>`.

```cpp
// create / load
alx::image_base* img = alx::image_base::create_image(w, h, /*depth=*/24);
alx::image_base* bmp = alx::image_base::load_image_bmp(bytes_view);
alx::image_base* jpg = alx::image_base::load_image_jpg(bytes_view);   // libjpeg-turbo

// attributes
img->null();  img->valid();  img->width();  img->height();  img->depth();
img->line_bytes();        // bytes per row (alignment padding included)
img->usfu_line_bytes();   // the "useful" bytes per row (padding excluded)
img->byte_count();        // total bytes in the buffer
img->data();

// pixels (the representation follows the depth: 1/4/8-bit are bit-field pixels, 16-bit is 5-6-5, 24/32-bit is BGRA)
alx::pixel<24>& p = static_cast<alx::image<24>*>(img)->get_pixel(x, y);
p = alx::pixel<24>(0, 128, 255);       // blue 0, green 128, red 255 -- the arguments are BGR, the order the bytes sit in the buffer

// palette (1/4/8-bit)
img->palette();  img->set_palette(pal);

// encode
alx::bytes bmp_data = img->to_bmp();
alx::bytes jpg_data = img->to_jpg(/*quality=*/90);
delete img;                                    // an object from create/load is deleted by the caller
```

On Windows there is also `load_image_hbm(HBITMAP)`.

---

## 7. Database (`asqlite.h`)

```cpp
alx::sqlite db;
db.open("/tmp/a.db");                     // a second argument of true = read-only
db.is_open();  db.last_error();

db.exec("CREATE TABLE t (id INTEGER, name TEXT)");
db.exec("SELECT * FROM t", result);        // the exec that takes a result set

// transactions
db.start_transaction();  db.isin_transaction();  db.commit_transaction();  db.rollback_transaction();

// prepared statements
alx::sqlite::stmt st = db.prepare("INSERT INTO t VALUES (?, ?)");
st.valid();
st.bind(1, 42);  st.bind(2, "alx");        // int/double/long long/bool/string/bytes_view/null are supported
st.bind_index("name");                     // a named parameter
st.exec();                                  // evaluate one step, returning a state: done / fail / data
st.reset();                                 // rewind so the statement can be reused

// read the columns
st.column_count();  st.column_name(0);  st.data_count();  st.data_type(0);
st.get_intg(0);  st.get_real(1);  st.get_text(2);  st.get_blob(3);
st.get_variant(0);  st.get_jsonval(0);    // the two packed ways of taking a value
```

`stmt` / `token` are both `noncopyable` and movable; `stmt` holds a bare `sqlite*` only, so **its lifetime must not outlast the `sqlite`**.

---

## 8. Thread pool (`athreadpool.h`)

```cpp
alx::threadpool pool(/*threads=*/4, /*queue_limit=*/0x7FFF);
pool.enqueue([]{ /* the task */ });
pool.enqueue(func, arg1, arg2);            // variadic forwarding
pool.wait_until_empty();                   // the queue is empty
pool.wait_until_nothing_in_flight();       // the queue is empty and nothing is running
pool.set_pool_size(8);  pool.set_queue_size_limit(1024);
```

The default thread count is `max(2, hardware_concurrency())`; `_threads == 0` is clamped to 1 (not "zero workers").

---

## 9. Thread-safe containers (`athread_safe.h`)

### 9.1 Wrappers and lock macros

```cpp
alx::thread_safe_mutex<std::queue<int>> q;      // container + mutex
alx::thread_safe_readwrite<std::map<...>> m;    // container + reader/writer lock
q.data();                                       // the underlying container (locking is the caller's job)

THREAD_SAFE(obj);       // exclusive lock (RAII, the enclosing scope)
R_THREAD_SAFE(obj);     // read lock
W_THREAD_SAFE(obj);     // write lock
```

### 9.2 `safe_queue` — a queue with blocking semantics

```cpp
alx::safe_queue<std::function<void()>> q;
q.push(f);  q.pop(f);                       // non-blocking
q.block_push(f, /*limit=*/128);             // waits while the queue is full (the limit is not exceeded)
q.block_push(f, 128, /*wait_ms=*/50);       // with a timeout
q.block_pop(f);  q.block_pop(f, 50);
q.size();  q.empty();  q.is_destroy();
q.clear();  q.reset();  q.destroy();        // after destroy every blocking call returns false at once
```

After `destroy()` both `block_push` and `block_pop` return `false` at once and block no more -- the queue's "shutdown" semantics.

### 9.3 `safe_map` — a pointer container that forwards method calls

```cpp
alx::safe_map<std::string, worker> pool;
pool.insert("a", new worker());             // the same key deletes the old value, then replaces it
pool.contain("a");  pool.keys();  pool.remove("a");
bool ok = false;
int n = pool.call("a", ok, &worker::size);  // calls the member function under the read lock; ok=false when the key is absent
```

`safe_map` owns the values (it `delete`s them on destruction, on replacement and on removal).

---

## 10. Logger (`alogger.h`)

```cpp
// the global default logger (built lazily, writing to cout by default)
alx::log_info() << "started" << 42;
alx::log_warn().type("net") << "slow peer";
alx::log_error() << "failed";

// a custom configuration
const char* cfg = R"({
  "prt_time": true, "prt_thrd": true, "prt_level": true,
  "stdout": { "type": "cout", "level": "info", "is_flush": true },
  "file_log": { "type": "file", "level": "debug", "is_async": true,
                "is_append": true, "queue_limit": 1024, "file_path": "./out.log" }
})";
alx::logger log(cfg);
log.log("hello", alx::log_level::info);

// take over the global path (forwarding to a sink of your own, say)
alx::global_logger::instance()->set_log_handle([](const std::string& m, alx::log_level l) {});
```

- Levels: `info` / `debug` / `warn` / `error` / `crash` (extensible).
- Global options: `prt_time` / `prt_thrd` / `prt_level` / `prt_head` (a newline in front of the message).
- Appender types: `cout` (stdout), `file` (a local file, possibly asynchronous), `win32` (`OutputDebugString`, Windows only).
- Convenience macros: `FUNC_TRACKER()` / `FUNC_TRACKER_EX("msg")` -- one record each on entering and on leaving the scope.

The log object is a `log_wapper<LEVEL>` temporary; `operator<<` appends to its buffer and **the destructor** submits the record to the global logger.

---

## 11. Fiber (`afiber.h`)

User-space cooperative scheduling (POSIX with `ucontext`, Windows with Fibers):

```cpp
alx::fiber::initialize();                       // initialize before first use (create() calls it internally too)
alx::fiber* f = alx::fiber::create([]{ /* the body */ });   // nullptr when the arguments are unusable (commit > reserve)
f->set_data(any_variant);  f->get_data();
f->get_state();                                 // READY / RUNNING / SUSPENDED / FINISHED

alx::fiber::yield();                            // yield back to the main fiber
alx::fiber::yield(f);                           // switch into the given fiber
alx::fiber::current();  alx::fiber::main();  alx::fiber::is_fiber();

alx::fiber::deinitialize();
```

`fiber::create` uses a 1 MB default stack, which `create(func, commit_size, reserve_size)` overrides.

---

## 12. Cache (`acache.h`)

```cpp
alx::cache* c = alx::cache::create("cache_lru", /*capacity=*/1024);
c->put("k", alx::variant(1));
alx::variant v;
c->get("k", v);
c->remove("k");  c->clear();

// pooling: several named caches managed by key
alx::cache_pool pool;
pool.create_entity("sessions", "cache_lru", 4096);
pool.put("sessions", "u1", alx::variant(1));
pool.get("sessions", "u1", v, [] { return alx::variant(); });   // the factory fills a miss
pool.list_entitys();  pool.list_types();
```

The implementations register by name; there are three: `cache_fifo` / `cache_lru` / `cache_lfu` (listed by `list_types()`). The name is the implementation class's own class name.

---

## 13. Platform layer (`aplatform.h`)

### 13.1 Running external commands

```cpp
alx::exec_result r = alx::exec_sync("ls -l", /*timeout_ms=*/500);
r.success;  r.timeout;  r.excode;  r.output;      // the merged stdout + stderr

alx::exec_result r2 = alx::exec_sync("make -j7", "/path/dir", 60000);   // with a working directory

std::future<alx::exec_result> fut = alx::exec_async("gcc -v", 2000);
```

The timeout puts the child in its own process group and sends `SIGTERM` to that whole group.

### 13.2 Shared memory

```cpp
alx::mmap shm("my_shm");
shm.open(/*size=*/4096, /*create=*/true);
shm.owner();                                // true = this open() created it
shm.set(0xFF, 0, 16);                       // fill an area
shm.set(0x01, 3);                           // a single byte
shm.cas(0x01, 0x02, 3);                     // compare and swap
shm.write(data, offset, size);  shm.read(buffer, offset, size);
shm.take(buffer, offset, size);             // read and zero
alx::bytes all = shm.read();  alx::bytes got = shm.take_all();
shm.close();                                // detaches this object only
shm.destroy();                              // removes the names of the data and of the companion lock
```

Cross-process exclusion uses a named semaphore (POSIX) / a named mutex (Windows), and operations like `set` / `cas` / `write` take the lock internally.

**Lifetime model**: the process that wins the name in `open(size, true)` is the creator (`owner()` true), and the others are attachers.

- `close()` detaches this object only (munmap + close the fd + close the companion lock), **leaves the name alone**,
  may be called by any holder at any time, and does not disturb the others.
- `destroy()` = `close()` + removing the two names, idempotent (a name already gone counts as removed). It
  should be called only when no new attach can happen afterwards -- the library cannot know who else is
  still attached (POSIX has no "attacher count" query).
- **Destruction**: the creator goes through `destroy()`, an attacher through `close()`; a creator that already
  called `close()` explicitly stays as it is, and the name is left for the next session.
  **The lifetime of the creator object = the lifetime of the name**, so do not put it in a short-lived scope.
- An attacher's `_size` above the block that is already there ⇒ `open()` returns `false` (the old behaviour was success, with a SIGBUS only on reaching the end).
- **Crash debris**: a SIGKILL does not run the destructor. A fixed-name user clears the ground at the start of a session with `destroy(); open(size, true);`.
- Windows difference: a named kernel object has no unlink, so `destroy()` is just `close()` and the name goes
  only when the last handle closes ⇒ with the creator gone first the name is still there (on Linux it
  disappears at once). One API on both sides, only the moment the name goes differs.

### 13.3 Process

```cpp
alx::process_info self = alx::process_info::current();   // this process
std::vector<alx::process_info> list = alx::process_info::find_process("nginx");
alx::process_info::find_process(pid);                     // [size 0|1]
alx::process_info::find_child_process(ppid);
for (const auto& p : list) p.pid, p.ppid, p.thread_cnt, p.base_priority, p.path;
std::string name = alx::file_info(p.path).name();         // split the name yourself

alx::process_ctrl ctrl("/usr/bin/cat");
ctrl.sig_output.connect([](const std::string& s) { /* a raw stdout block */ });
ctrl.sig_errput.connect([](const std::string& s) { /* a raw stderr block */ });
ctrl.start({"file.txt"});  ctrl.input("line\n");  ctrl.wait(1000);
ctrl.output_done(1000);                              // whether the output has finished (0 = sample only; wait() returning ≠ it said everything)

alx::process_ctrl::process_limits limits;   // all or nothing: it does not start unless every item holds
limits.mem_bytes = 256ull * 1024 * 1024;    // POSIX = RLIMIT_AS (address space), Windows = the job's commit
limits.kill_on_parent_exit = true;          // the child dies with the parent (POSIX: PDEATHSIG / Windows: the job)
ctrl.start({"script.ax"}, limits);          // true ⇔ the child is already running it with every limit in place

ctrl.terminate();                                    // soft: POSIX SIGTERM / Windows closes its stdin
ctrl.kill();                                         // hard: SIGKILL / TerminateProcess, reaps and releases
alx::process_ctrl::wait_status st = ctrl.status();   // exited / exit_code / signaled / term_sig
ctrl.pid();                                          // 0 = no child attached right now
```

`process_info` has `path` only, no process name: `path` is the **resolved real executable** (a symlink launch gives the target file, not the start name), and an unreadable one is an empty string (kernel threads, restricted or cross-user processes). `find_process(name)` matches on `basename(path)` by default -- a process cannot change that identity of its own, which is steadier than matching on `comm`; an argument containing `/` (or `\`) is compared against the **whole path**, so passing a full path hits as well. To use the name, split it with `file_info(path).name()`. `start()` is synchronous -- the child is already running the requested image when it returns -- so its identity is available at once, with no polling for `path` to change.

`sig_output` / `sig_errput` deliver the **raw blocks** of `read()` (at most 4096 bytes at a time), **not lines**: one callback may carry several lines or half a line, `\n` included as it stands -- accumulate a buffer yourself to work line by line. The callbacks run on the library's `loop()` thread, and `kill()` / destruction join it, so **do not wait inside a callback on a lock the calling thread holds**.

Killing and reaping:

- `terminate()` is soft: POSIX sends `SIGTERM` (the child may ignore it); Windows can only close its stdin, which is **irreversible** and useless when the child does not read stdin (`input()` returns `false` from then on).
- `kill()` is hard, and reaps on the way, joins `loop()` and closes the pipes, putting the object back into its "not started" state (`pid()` back at 0). Idempotent: with the child already dead it returns `false`, yet still completes that release.
- **A child that died on its own is not reaped automatically**: `status()` / `wait()` are explicit questions, the reaping happens at that instant, and asking again returns the cached value. `pid() != 0 && !exited` means it is still running; `pid() == 0 && exited` means the previous child has ended and been released. The outcome sticks until the next `start()`.
- **`wait()` / `status()` returning ≠ the output has finished**: what they wait for is the child being reaped, while the `loop()` thread is still delivering the blocks left in the pipes -- reading the accumulated output then sees half of it, and releasing the handle (`kill()` / destruction) at that moment loses the remaining blocks for good. `output_done(_mill)` is the only "nothing more is coming": both pipes at EOF, or cut by `kill()`, count; `start()` re-arms it. For complete output: `wait()` → `output_done(…) == true` → then release.
- The `kill()` path does not preserve the child's dying output (`p_id` at 0 is what makes `loop()` exit, without waiting to drain); for the last stderr line before it crashed, take the soft-kill or natural-exit path, and wait for `output_done()` before releasing just the same.
- Destruction = `kill()`, leaving no zombie.
- **One instance belongs to one calling thread** (the library adds no lock, and concurrent `status()` / `kill()` on one instance fight each other); across instances concurrency is free, and locking multi-threaded access to one instance is the caller's job. The exception is `output_done()`: it is an atomic inside, so another thread may read it (the `loop()` thread writes it).
- `start()` is **synchronous**: returning `true` ⇔ the child is already running the requested image with every item of `limits` in place; returning `false` ⇔ one of them could not be done (`exec` failing included) and no child came up. So "the child did not come up" is known synchronously, with no need to guess from an exit code afterwards. `mem_bytes` is **address space** on POSIX and **commit** on Windows; the two mean different things, so do not treat them as equivalents.
- Windows difference: the `signaled` / `term_sig` fields do not exist on that platform; `kill()` uses `TerminateProcess(…, 1)`, so `exit_code == 1` cannot be told apart from "the child ran `exit(1)` itself" (a crash is an exception code `>= 0xC0000000`).
- `input()` probes liveness before writing (it calls `status()` internally, reaping a dead child on the way) and returns `false` right away when the child has already ended -- avoiding a `SIGPIPE` from writing to a pipe whose reader is gone on POSIX (the library does not take over that process-level signal disposition).

---

## 14. Regex (`aregex_pcre2.h`)

A regex on the PCRE2 backend: **a single match has a ceiling, and the host can stop it mid-flight**. A different animal from base's `regex_ex` (`std::regex`, with no bound on its cost); pick between them by whether the input is trustworthy:

| | `regex_ex` (alxbase) | `regex_pcre2` (alxcore) |
|---|---|---|
| Engine / dependency | `std::regex` (ECMAScript) / standard library only | PCRE2 10.48 (`3rdpty/pcre2`, linked in statically) |
| Cost of one match | **No bound**: 30 `a`s against `(a+)+b` measured 165 s, doubling with every extra character | Capped at the factory default of 10⁷ steps (measured ≈ 0.14 s on this machine), and lowerable |
| Interrupt | No (no checkpoint inside a call) | Yes: a callout callback returning `false` aborts it |
| Replacement string | ECMAScript: `$&` / `$1` / `$$` | PCRE2 (a superset): `${1}` / `$0` / `$_` / `$+` / `$<name>` |
| Threads / concurrency | Shareable (`const` is read-only) | **Not concurrent**: no two methods on one instance may run at once |
| Copy | Copyable and movable (a value type) | **Not copyable**, movable: one instance is one matcher -- its own hook, its own status, its own match block |

**The concurrency stance (fixed, do not guess)**:

| Situation | Allowed |
|---|---|
| Two threads matching on **one object** at once | ❌ the match block and `last_status()` / `last_error_code()` are unique to the instance |
| Calling a setter on the same object while a match is in flight | ❌ it writes the match context (PCRE2's stance is "shareable while the context is not modified"); `set_callout` is worse, it is a **use-after-free** (it frees the old binding, and that is what the bridge of the match in flight reads) |
| **One instance per thread**, matching in parallel | ✅ instances share no mutable state (each compiles its own pattern, costing µs) |

The library **adds no lock and does no runtime detection**: a concurrent call is not stopped, it simply is a data race, and **the caller guarantees** this constraint. The stance matches `process_ctrl`: one instance does not run concurrently, across instances concurrency is free.

**Why not copyable, and why the hook belongs to the instance**: one instance is one matcher -- the match block (which PCRE2 requires to be "one per thread"), `status_` / `last_err_` (written during a match), and the callout hook with its `ud` are all its own. Two copy semantics were tried and neither was clean: **inheriting the hook** → two matchers share one stop signal (A's "stop" aborts B's match in flight too, and the callback cannot tell whose it is); **not inheriting it** → one more invisible difference between the original and the copy. So it was settled on no copying: for parallel work, one instance per thread with its own hook. (Were same-object concurrency really wanted, the hook would have to become a per-call argument, and the object-level hook would have to go with it -- two channels amount to the ownership question left unsolved.)

The check: **trustworthy input and a fixed pattern → `regex_ex`; a pattern or input that comes from outside → `regex_pcre2`**.

```cpp
alx::regex_pcre2 re("^\\d{4}-\\d{2}-\\d{2}$");          // the second argument is a compile option, icase / dotall / multiline / nosubs
re.is_valid();  re.is_compliant("2026-09-21");          // a whole-string match (ANCHORED|ENDANCHORED inside, so it may backtrack to an alternative that satisfies the end anchor)
re.find("...");  re.find_all("...");
re.find_all_spans("...");                               // {offset, length} per match: the same scan as find_all
re.match_group("...");  re.match_group_all("...");      // a group that took no part = an empty string
re.replace(str, "$1");  re.replace_all(str, "${1}-$0");

re.set_step_limit(1000000);            // the backtracking step cap for one match; 0 = back to the engine default (10⁷)
re.set_depth_limit(0);                 // the backtracking depth cap; defaults to the step cap's value
re.set_callout(&my_stop, &my_ud);      // called during a match; returning false aborts it (status interrupted)
re.set_substitute_options(bits);       // raw PCRE2_SUBSTITUTE_* bits; UNSET_EMPTY by default

alx::regex_pcre2::match_status st = re.last_status();   // ok / no_match / limit / interrupted / error
alx::int_32 code = re.last_error_code();                // PCRE2's own negative code, 0 = no error
```

`find_all_spans()` is the one scan `find_all()` runs, reporting each match as `{offset, length}` rather than as text (the text view is a projection of it, and gtest pins the two to agree entry for entry). A position **cannot** be read back off the text: a zero-width match does not land at the end of the previous one -- `(?<=\s)x*` over `"a x y"` puts its second match at 4 (the space before `y`), not at the cursor 3. Use it when the host assembles its own slices. (`regex_ex` has no such method: it hands out its `std::regex` handle (`get_regex`), so the host can scan for itself; the handle here is a private opaque type, so only the class itself can report.)

`last_status()` is not decoration: a failed `find()` returns an empty string, and "nothing matched", "the step limit was hit" and "it was stopped" look exactly alike in the return value. The host has to use it to turn the latter two into its own `InterruptedError` / `ResourceError`; otherwise it is silently giving a wrong answer (`no_match` corresponds to `PCRE2_ERROR_NOMATCH(-1)`, `limit` to `MATCHLIMIT(-47)` / `DEPTHLIMIT(-53)`, `interrupted` to `CALLOUT(-37)`).

Registered differences (all pinned in `gtest/alxcore/src/gt_aregex_pcre2.cpp`):

- **Empty-match advance: measured, the two agree**. `\d*` / `a*` / `a*?` / `(a)*` over `"a"` / `"a1"` / `"aab"` / `"b"` / `""`, 20 combinations in all, give `find_all` results identical to `regex_ex` entry for entry; the shapes where **a zero-width match lands past the scan's starting point** (assertions like `\b` / `\B` / `(?=\s)` deciding where the match sits) agree entry for entry too. The advance rule is: after an empty match `offset` **first drops to that match's own position** (a zero-width match may sit past the scan's starting point -- the assertion decides), then "the same position and non-empty" is tried, and only when both fail does it move one byte on.
- **The replacement dialect: measured, the two differ**. `replace("a", "$12")` on `(a)` -- `regex_pcre2` reports `error` and returns `"a"` unchanged (there is no group 12) while `regex_ex` returns `""`; the latter's "group 1 plus a literal 2" has to be written `${1}2`.
- **Pruning**: PCRE2 brings a "required bytes" pre-scan, and `(a+)+b` against 30 `a`s (the shape that measured 165 s on std::regex) **backtracks not one step** (not even a 1000-step budget is touched, status `no_match`). So what the step cap really has to hold back is what the pre-scan cannot cut, such as `^(a+)+$` against 30 `a`s plus `!`.
