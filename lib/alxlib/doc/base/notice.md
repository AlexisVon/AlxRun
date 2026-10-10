# alxbase — Known Limits and Pitfalls

Two parts: **§A Open defects** (confirmed, not yet fixed) and **§B Limits and pitfalls** (edge cases outside the behavioural contract, not counted as defects). Every entry states its trigger and its check, so a limit can be tightened later on demand.

---

## §A Open defects

No open defects at present.

> One historical entry: with the current node a `vec`/`lst`, `variant::select` threw `invalid_argument`/`out_of_range` for an empty segment and for an over-long numeric one, instead of answering `_def` as the contract says (the same origin as `avarsolid::get_value`). Changed to a bounded accumulation; measured, the regression case `gt_avarmix.select_rejects_malformed_index` fails on the old code and passes once fixed.

---

## §B Limits and pitfalls

## Memory and ownership

- **A failed `bytes` allocation throws**: a size that cannot be represented throws `std::length_error`, a malloc that fails throws `std::bad_alloc` (`null()` means empty, not failed). Uncaught in the host it reaches `terminate`; thrown on a thread of the library's own -- the send and receive threads of `comm`, say -- it kills the whole process. See the entry of the same name in `doc/comm/notice.md`.
- **`bytes::resize(0)` has two semantics**: with a shared block (reference count > 1) it goes through `clear()` and drops the block; with an unshared one it only sets the block header's `size` to 0 and **keeps the allocation**. `resize(0)` therefore promises no release of memory; use `clear()` / `fitsize()` for that.
- **`bytes::data()` (the non-const one) detaches**: called on a shared block it deep-copies. Taking `data()` on the same shared object over and over on a hot path is one O(n) copy each time; call `detach()` first, or cache the pointer once.
- **`bytes_view` holds the underlying `bytes` by value** (not a raw pointer): the view keeps the block alive until it is destroyed; `data()` answers `nullptr` only while the underlying `bytes` is `null()` (a zero-length view over a live block still hands out a valid pointer), and `operator[]` is unchecked.
- **`bytes::to<T>()` checks no size**: `r_interpret<T>` overlays a T straight away, so bounds and alignment are the caller's business; the payload is allocated with `default_align = 4`, i.e. 16-byte alignment.

## Containers and types

- **`varmap` iterates in the key order of `std::map`**, which is not insertion order. Do not rely on a walk when insertion order is what is wanted.
- **`varmap::insert` is a no-op for a key that is already there**: it returns the existing iterator plus `false` and **does not overwrite** the old value. Use `operator[]`, or `erase` first, to replace one.
- **`varmap`/`varvec`/`varlst` are deep-copy value types**: assignment and copy construction duplicate every element; inside, `varmap` is a `std::map<std::string, variant*>`, so every value costs one heap allocation.
- **A numeric `variant::to<T>()` converts only when the conversion is lossless**: a cross-type read (`int` → `uint`, `long long` → `int`) is decided by `is_lossless<FROM,TO>`, and a lossy one answers `_def` (T() by default) -- no throw, no truncation. `variant::to_number()` collapses to `double`, so a large integer loses precision; outside the contract -- called on a non-numeric value -- it answers **NaN** rather than 0, because 0 would pass for a legitimate answer while NaN propagates through the arithmetic that follows.
- **`json_array` derives from `std::vector`, and the base's `erase` / `pop_back` / `resize` still move raw pointers**: no element is destructed, so they leak or double-free. `clear()` and the copy/move/destructor take over the ownership (see the header comment); to change the element set, use `clear()` and `append` again.
- **Every `json_array` element is `new`ed**: appending one `json_value` costs one heap allocation, so a large number of small elements is worth a `reserve` first.

## Serialization (aserial)

- **`r_interpret(ptr, size)` truncates a non-multiple length rather than reporting it**: `count = size / sizeof(T)`, and a tail too short for one T is dropped silently. A blob whose length field does not match its type is not refused; it is merely under-read.
- **Short data on the decode path leaves the default silently**: with `_dat.size < sizeof(T)`, `deserer_var_base_ordinary` does **not write** the target variant (it keeps the default-constructed value) and throws nothing. A field that never decoded and a field whose value happens to be the default are therefore indistinguishable in the result.
- **Only a direct call to `ser::deserer<...>::parse` needs the initialization**: the type handlers it needs are registered by the library-internal `init_core()`, a static that no header declares; the public entries (`varsolid::to_varmap` / `get_value` and the rest) run it on the way in. Calling `parse` without it answers a null variant every time -- a common source of false negatives in a check.
- **A path component of `varsolid::get_value` resolves as a decimal index**: solid does not transcode strings -- values are raw bytes -- and the path walk only does the positional parse. An index that is not valid (empty, non-decimal, overflowing) answers `_def` as the contract says.

## Singletons and global state

- **The singleton `single<T>::instance()` hands out is not destroyed at process exit**: the implementation is a function-local `static T* obj = new T();`, deliberately never destroyed -- another static destructor may still be logging. A resource the singleton holds (a file, a thread) is therefore not promised to be released at exit. It also means the initialization is C++11 once-only, with no data race.
- **The default logger of `global_logger` is built on first use**: when several threads log for the first time at once, a CAS picks one winner and the losers release their own copy, so there is only ever one default logger instance.

## Other

- **`strutil::split` with an empty delimiter cuts into single bytes**: it returns `len + 2` elements (one empty element before the first and after the last, one per byte in between), not an empty list.
- **The `regex_ex` copy assignment is `noexcept`**: assigning the internal `pattern` can in theory throw `bad_alloc`, which then terminates (vanishingly unlikely, left as it is).

## Encryption and digest

- (moved from alxcore, 2026-10-09) **The AES padding semantics changed, so old ciphertext is not guaranteed to unpad under the new rules**: `ANSIX923` and `ISO10126` now **always** append a whole block, an already aligned buffer included, and unpadding accepts `pad_len == 16` accordingly. The old version appended nothing when the buffer was aligned, so the last byte of old ciphertext is data itself; unpadding it under the new rules may strip 1..16 bytes too many. Check: the old implementation was not self-consistent (it appended nothing yet stripped by the last byte, so any last byte below 16 was mis-stripped), and there is no old format worth migrating, so no compatibility is attempted.
