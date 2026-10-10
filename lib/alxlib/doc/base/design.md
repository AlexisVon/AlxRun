# alxbase — Design

This document records the **design decisions, implementation points and tuning notes** of `alxbase`.

## Design principles

1. **Zero dependencies**: alxbase depends on the C++ standard library alone (`<string>` `<vector>` `<map>` `<atomic>` …). The layers above -- `alxcore` / `alxcomm` / `alxscpt` -- are all built on it, so it is the leaf of the whole dependency graph.
2. **No exceptions for control flow**: the library barely throws. Failure is expressed as "returns `nullptr` / returns `false` / returns `_def` / `null()` is true". The reason is that the data on the other side (network, file, script) produces malformed input at a high rate, and an exception path costs more than it is worth, with no control over when it fires.
3. **Value semantics outside, pointer semantics inside**: `bytes` / `varmap` / `json_object` behave like values to the caller (a copy is a deep copy), while the implementation uses a reference count or a pointer container, so memory management is never pushed onto the caller.
4. **Zero-copy first**: a read-only path always hands out a window type such as `bytes_view` / `content_meta`, materializing an owning object only when the data has to outlive the window.
5. **Demotion into base (2026-10-09)**: a facility from a higher layer may move here when it (a) is self-contained — no alx module and no third-party library is needed (the standard library, libc and compiler built-ins are fair game), (b) holds no shared mutable state and no concurrency machinery, and (c) has a converged interface — shape and contract frozen, with exposure tightening already done. The last point is why the move follows the freeze instead of triggering it: a lower layer pays more for an interface change, not less.
6. **C++11 floor**: `BUILD_TESTS=ON` asks for C++17 (gtest), while library code stays usable from C++11 (`autility.h` fills in the `_v` trait family for the older standards).

---

## 1. `bytes`: a copy-on-write byte buffer

### 1.1 Block layout -- one allocation

```cpp
struct bytes_block {          // block header
    ref_count ref;            // reference count
    uint_64   size;           // logical length
    uint_64   alloc;          // bytes allocated, rounded up to the alignment
    // uint_8 data[] sits right behind it
};
```

Points:

- **Header and payload sit in one piece of memory** (`malloc(align(size) + sizeof(bytes_block))`, with `data()` located by `(uint_8*)this + sizeof(bytes_block)`). Set against a control block separate from the data, that saves one allocation and one level of pointer indirection, with a better cache locality.
- **Alignment is fixed at 16 bytes** (`default_align = 4`, an exponent): it gives the reinterpret in `to<T>()` a basic alignment guarantee, at the price of up to 15 wasted bytes per block.
- **`size` lives in the block header** -- the key constraint of COW: `size` is **shared by every alias**. Any operation that changes `size` has to detach first.

### 1.2 Reference count

`ref_count` is a three-state atomic counter: `unsharable` (no sharing, `ref()` returns false), `static_ref` (a static object, never destroyed) and an ordinary count (`is_shared()` = count > 1).

- Copy construction / copy assignment: `ref()` raises the count and **copies no data**.
- Destruction: `free` only when `deref()` returns false (the count reached zero).
- Move: the pointer is simply taken over and the source is nulled.

### 1.3 Where detach fires

`detach()` = "deep-copy an exclusive one while the block is shared". Three kinds of trigger:

1. **The non-const `data()`**: once a writable pointer is handed out the block must be exclusive, or the write lands on another alias.
2. **`resize()` / `clear()`**: the header's `size` has to change.
3. **`append()` / `ensure_capacity()`**: the capacity has to grow, or the tail rewritten.

> Revision log: early on `resize()` did not detach on the second kind of trigger, which polluted aliases -- "a shrink changed `size` on a shared block and every other alias came back shorter". Corrected to "detach first while shared". `resize(0)` forks again: shared it goes through `clear()` (detach and release), unshared it only sets `size` to 0 (keeping the allocation).

### 1.4 Growth strategy and failure semantics

- **1.5x growth**: `ensure_capacity` runs `while (_need > new_size) new_size += (new_size >> 1);`. 1.5 rather than 2 is what makes a block freed after several growths more likely to be reused; a 2x step leaves freed blocks too big to be reused.
- **`realloc` first**: an unshared block grows in place through `realloc`, and a failed one keeps the old block: the old block survives a failed realloc, so the caller keeps its data.
- **A failed allocation throws**: `bytes_block::allocate` / `reallocate` are the only allocation entry points (nothing outside `abytes.cpp` calls them), throwing `std::length_error` for a size that cannot be represented and `std::bad_alloc` for a malloc that fails, the same line STL containers take. **That makes "the old block survives a failure, no data is lost" loud**: a failed `realloc` leaves the old block untouched and the exception hands control back to the caller. `null()` means "empty", not "failed".

### 1.5 `bytes_view`: the underlying buffer held by value

The members of `bytes_view` are `bytes m_orig` (not `const uint_8*`) plus an offset and a length. That is deliberate, not a slip:

- while the window lives the block necessarily lives (the reference count backs it up), so no caller has to guarantee a lifetime;
- the price is one touch of an atomic counter on every construction, copy and destruction.

Every position-relative parameter (`find` / `equa` / `mid` …) is read as **relative to the window's first byte**; the implementation computes `m_ofst + _from` before calling down and subtracts `m_ofst` from the answer.

---

## 2. `variant`: type erasure without a vtable

### 2.1 A function-pointer table instead of virtual functions

Every supported type gets a **static function-pointer table** at compile time (`__resource_ctrl<Args...>::new_free / new_init / new_move / stk_init / opt_eqal / idx_type / def_meta`), and at run time the type index `m_type` picks the entry:

```cpp
if (m_heap) CTRL::new_init[m_type](m_dptr);
```

Set against "every value carries a vtable pointer", the gains are:

- a variant object **carries no vtable pointer** (8 bytes saved, exactly the inline threshold);
- the type index is the compile-time constant `typelist::index_of<TYPE, T>()`, so `is<T>()` / `to<T>()` degrade to an integer comparison;
- the order of the tables matches the type list one to one, so adding a type touches `typelist` in one place only.

### 2.2 Heap/stack split

```cpp
static constexpr uint_64 STACK_SIZE{8};
heap_mode<T>() = !std::is_trivially_copyable_v<T> || sizeof(T) > STACK_SIZE;
```

- **Trivially copyable and at most 8 bytes** → stored inline in `union { void* ; uint_8[8]; uint_64 }`, no heap allocation;
- everything else (`std::string` / `bytes` / containers / `anyptr`) → heap-allocated, held by `m_dptr`.

That threshold keeps every scalar type (including `long long`, `double` and pointers) out of the heap while every container type goes to it -- a simple implementation, with none of the half-hearted inlining a "small string optimization" brings.

**The shape of the slot is a constraint**: the stack slot is `uint_8[8]` on purpose (the character family, about which alias analysis assumes nothing), and a whole-slot copy goes through the union member `m_dval`. Narrowing the slot to a type such as `uint_64` would let GCC's TBAA decide it cannot alias the `double` or pointer stored into it, and under `-O3` the write is then eliminated or reordered into a silently wrong value (`-O1`/`-O2` show nothing) -- `m_dval` looks like it overlaps `m_dstk[8]`, but it is not a redundant field and must not be deleted.

### 2.3 Move semantics

A heap-state move is **taking the pointer over and setting the source to `m_type = -1`** (the source becomes null, so its destructor frees nothing); a stack-state move copies the 8 bytes by value and nulls the source the same way. A moved-from source is therefore always `null()` -- a tighter promise than the standard "valid but unspecified state", and an easy one for a caller to test.

### 2.4 Lossless conversion policy

```cpp
template <typename FROM, typename TO>
TO to_number_impl(const TO& _def) const noexcept {
    return alx::is_lossless<FROM, TO> ? (TO) r_interpret<FROM>(...) : _def;
}
```

`is_lossless` is decided at compile time (the same type always qualifies; a signed source needs a signed target at least as wide, or a real target strictly wider; an unsigned source needs an unsigned target at least as wide, or a signed or real target strictly wider; a real source needs a real target at least as wide), and at run time only the type index dispatches. **A lossy conversion always answers the default, never a silent truncation** -- deliberate: a `variant` carries "a value shared between the host and the script", and a silent truncation turns into a business error that is hard to track down.

### 2.5 The split between `to` and `as`

| | Types match | Types differ |
|---|---|---|
| `to<T>(_def)` | the stored value comes back | `_def` comes back, **this object is untouched** |
| `as<T>()` | the stored value comes back | **`T{}` is stored** and its reference comes back |
| `as<T>(_ifnot)` | the stored value comes back | **`_ifnot` is stored** and its reference comes back |

A read path uses `to` (no side effect); a "take or initialize" path uses `as`.

### 2.6 Exception safety: commit order and the `noexcept` specification

The heap branch has to allocate, so **assignment may throw `bad_alloc`**. Two rules:

**Commit last** -- in `operator=` the statements writing `m_heap` / `m_type` have to come **after** the operation that may throw:

```cpp
if (_value.m_heap) m_dptr = CTRL::new_init[_value.m_type](_value.m_dptr);  // throws here
else m_dval = _value.m_dval;
m_heap = _value.m_heap;
m_type = _value.m_type;
```

`destroy()` has already run, so after a throw the object rests at `m_type == -1`: a **legal null state**, not the torn state of "claims to be a type whose pointer is null" (where `is<T>()` is true and one read segfaults). The semantics are the **basic guarantee** (a failure leaves the object null), not the strong one (a failure leaves the old value in place) -- the strong guarantee needs old and new side by side, which doubles the peak on a host under a tight `RLIMIT_AS` and so fails more often, not less; a bad trade.

A constructor needs no such order: a throw leaves the object unfinished and its destructor never runs, so no half-built state is observable, and copy construction / move construction still assign the state flags first.

**The null state is expressed by the tag alone**: while `m_type == -1` the payload is undefined, and no read point ever looks past the tag at it -- default construction writes the tag only, and `destroy()` puts the tag back to -1 (the release happens only when a type was stored), so wiping the payload would be neither necessary nor right.

**`noexcept` covers the stack branch only**:

```cpp
__variant_impl(T&& _value) noexcept(!heap_mode<std::decay_t<T>>())
```

The stack branch is 8 trivially written bytes and genuinely cannot throw; the heap branch does allocate, and marking it `noexcept` would promise to land `bad_alloc` as `std::terminate`, out of the host's `try/catch` reach. `noexcept(false)` lets the exception travel, so the host can treat it as a resource error.

---

## 3. `varmap` / `json_object`: pointer container, value-semantics interface

`__varmap_impl<MapType, Type>` derives from `MapType<std::string, Type*>` (`protected` inheritance, so only the controlled interface is exposed), yet behaves to the caller like a `map<string, Type>`:

- copy construction / assignment: element-by-element `insert` (**a deep copy**);
- `erase` / `clear` / destruction: `delete` the value pointer;
- `value(key)` on a miss answers the shared static default `def_val()` (a function-local static: free to reach and thread-safe);
- `operator[]` is "create a default when the key is absent", `insert` is "keep what is there when the key is present" -- their roles are clearly divided, which rules out the hidden "`[]` inserts on the way past" behaviour.

**Why pointers at all**: `variant` and `json_value` may each contain the other recursively (a `variant` inside a `varmap`, a `json_value` inside a `json_object`), and a by-value member needs the type complete; a pointer breaks that cycle and keeps a map node a fixed size (no node carries the size of a whole variant).

> The price: one heap allocation per insert and one pointer hop per walk. Enough for what this library uses them for (configuration, protocol packets, JSON).

---

## 4. JSON / XML

### 4.1 JSON

- **Parsing is recursive descent** (the `syn_json*` family), over a lexical layer `lex_json` that advances byte by byte. **Check the bound before reading** is a hard rule: while `_next == _size` the code must never read `_data[_next]` (a one-byte out-of-bounds read lived here once, reproducible under ASan).
- **`json_value` reuses the type erasure of `variant`** (`__variant_utils_base`, which takes the scalars and the two containers only), so a read goes through a wrapper such as `to_intg` / `to_string` with an `_ok` out-parameter rather than the `to<T>()` template -- the read semantics of JSON are "a type mismatch has to give the caller an explicit failure signal", and `_ok` says that more plainly than a default value does.
- **Integers all become `long long` and floats `double`**: the widening happens in the constructors, so `int` / `long` do not produce different type indexes across 32- and 64-bit platforms.
- **`bytes` goes Base64 automatically**: JSON has no binary type, and encoding is the only lossless choice.

**The ownership model of `json_array`** (the subject of one dedicated revision): inside it is a `std::vector<json_value*>`, and the **raw-pointer storage was kept** (rather than switching to `vector<json_value>`), at the price of filling in every lifetime operation by hand:

| Operation | Semantics |
|---|---|
| Destruction / `clear()` | `delete` every element |
| Copy construction / copy assignment | deep-copy the elements (copy assignment goes through copy-and-swap, exception safe) |
| Move construction / move assignment | `vector::swap` takes them over; move assignment `clear()`s itself first |
| `operator==` | compares the values element by element (the inherited vector version compared the **pointers**) |
| The inherited `erase` / `pop_back` / `resize` | **still raw-pointer operations that know nothing of ownership** (noted in the header) |

Why the raw pointers stay: it matches the incomplete-type plus fixed-node-size constraint of `variant` / `json_value`, and the change is small. The price is the table above -- **any new interface that removes some of the elements has to carry the deletion semantics explicitly**.

**The two conversion routes (copying / moving)**: the moving one **hands the heap buffers of `std::string` / `bytes` / containers over one by one** (no copy), leaving the source an empty shell (same type index, no content) that **may only be destroyed afterwards**.

| Direction | Copying | Moving |
|---|---|---|
| variant ↔ json_value | `from_variant(const variant&)` / `to_variant() const` | `from_variant(variant&&)` / `take_variant()` |
| Container level | `from_varmap` / `from_varvec` / `from_varlst` / `to_varmap` / `to_varvec` / `to_varlst` (`const&`) | the `&&` overloads of the same names; the way back is `take_varmap` / `take_varvec` / `take_varlst` |

- **Why the reverse is named `take_*` and not `to_variant() &&`**: giving a parameterless const member a `&&` qualifier first forces `const &` onto the existing one ([over.load]: ref-qualifiers must be present on every declaration sharing a parameter list or on none), and the symbol then changes from `_ZNK…` to `_ZNKR…` ⇒ **an ABI break**. Another name costs nothing, and the name itself says "take it away and leave it empty" (as `mmap::take_all()` does).
- **Why the forward direction can take a `&&` overload outright**: a static function is told apart by its parameter list (`const variant&` vs `variant&&`), so no existing symbol moves.
- **The two that cannot be moved**: `bytes` needs Base64 (the encoding necessarily allocates a fresh copy) and a stack-inlined scalar has no heap block -- both fall into the copying path, with semantics word for word those of the older version (including "a type outside the set gives a null `json_value`").
- **The contract**: after a throw the source is "half moved" and may still only be destroyed; `std::move` is the caller's declaration (the source is dead), and an lvalue still selects the copying version.

**The two routes of serialization (`json_doc`)**: the `const&` forms of `to_json` / `to_string` only read the input; the `&&` forms **eat the tree as they write it** (each entry is handed back the moment its text is out).

| | How | Peak |
|---|---|---|
| Copying | append layer by layer into the caller's string (RVO builds the returned string in place, no object copy) | `tree + text` |
| Moving | the same, plus removing each entry from its parent as soon as it is written (object `erase(key)`, array deletes the element and empties the slot, string `swap`s itself back to the system) | on the order of `max(tree, text)` |

- **One implementation only**: the 6 writer templates of `alx::json_doc_impl::writer` (a namespace plus a static class, **inside the .cpp**, never in a header) take `NODE&&` -- **const-ness travels with the argument**: a read selects the `drop()` no-op overload and a mutable argument the one that really releases. **Whether to eat is not a switch, it is the type**, so the two routes cannot drift apart.
- **Leaf strings**: `text()` bulk-appends in runs (breaking only at a character that needs escaping), replacing the old two whole-string temporaries of `to_string()` copy → `stringfy` making another → `+=`.
- **Two places have to hand things back one at a time**: `std::string::clear()` does not shrink (the memory is not returned) and `json_object::clear()` would delete what has not been written yet; removing from the front of a `vector` is O(n²), so an array deletes elements by index and empties the slot, and only `clear()`s at the very end.
- **Measured** (probe: `ru_maxrss`, 64 MB of data): a single leaf 196 → 196 MB (**the `&&` form adds nothing** -- the whole string has to live until the write is done), 32x2 MB 194 → 132 MB (the tree shrinks as it is written, absorbing even the moment the output grows).
- **The sink need not be a string**: the writer templates take `_SINK&`, and a sink only needs `append(const char*, size_t) -> bool`. Two instantiations -- `std::string` (always true, so the check folds away; **in-place/aliasing** resize plus backward write is something only it can do) and `alx::ostream` (the host writes straight into storage it owns: a mapped block, a socket, a file; `append` returning false means "it does not fit", the walk stops right there and `to_json` returns false. **A zero length is not "does not fit"**: what `escape_into` pushes is "the source text after the previous escape point", and for an empty string, for a first character that needs escaping, and for two escapes side by side that run is 0 bytes, which a sink has to accept). **The default path was not changed to `ostream&`**: that would put a virtual call in every append, and the JSON exit is the hottest one there is. The JSON exit can therefore inline end to end -- `writer::text` calls the internal `escape_into`, not the exported `json_doc::escape` (which crosses the PLT and cannot be inlined; that one point is the whole of the 8~13% by which `to_json` beats the other way of writing it).
- **The landing contract of `escape` / `descape`**: `(src, dst, ofst)` -- the first `ofst` bytes of `dst` are **left alone**, the result starts at `ofst`, and `dst` is resized to `ofst + result length`; `src` may be `dst` itself (then `ofst` is 0 and the path is count plus backward write, the write side always above the read side). The escape set follows RFC 8259: `"`, `\` and U+0000-001F (the short forms `\b \f \n \r \t`, the rest `\u00xx`), keys and values alike.

### 4.2 XML

- **`content_meta` is "a union plus a type tag"**: `ELEM` (an `xml_value*`) and the family of string kinds from `STRI` up (`TEXT` / `NOTE` / `PI__` / `DTD_` / `CDAT` / `ENTY`) share one `void*`. `free()` / `copy()` dispatch on `type_` to destroy or deep-copy, and destruction is **recursive** (destroying an element takes its subtree with it).
- **An element is a name plus an attribute table plus a content list**: attributes in an `unordered_map` (order is of no interest), content in a `list<content_meta>` (order kept, insertion and walking stable).
- **No name may repeat in the attribute table** (`from_bytes_attr` refuses the attribute when the key is already on the node): that is what keeps the parse free of the ambiguity where a later same-name attribute overwrites an earlier one. The parser tolerates malformed input poorly (it would rather fail), because XML in this library carries configuration and documents, where not being parsed is safer than being parsed wrong.

### 4.3 CSV

- **The table is just a `varvec`**: the outer level is rows and the inner one cells, with `row 0` the header row -- no new table type is invented, and there is no "document object" as in `json_doc` / `xml_object`; the entry points are free functions under `alx::csv` (the same shape as `strutil`).
- **The entry takes a raw byte range** (`const char* + uint_64`), with `std::string` an inline thin wrapper in the header -- the same shape as `json_doc::from_value` (`include/alxbase/ajson.h:432`): a `bytes`, an `mmap` or a socket buffer can be fed straight in, with no container built first (`csv` has no `bytes_view` overload while `json_doc` does; a `bytes` in the caller's hand gives up `data()`/`size()` anyway, so one more overload would buy no new case). The price is that reading a value takes two hops (`rows[r].to<varvec>()[c].to<std::string>()`), because `variant` does not index -- that is the bill for "no new table type".
- **The output side follows the json family**: `to_bytes` has a returning form (`bytes`) and a streaming one (`ostream&`, where an `append` returning false means "it does not fit" and the walk stops right there), each with a borrowing version and an `&&` consuming one (a row handed back per row written, so the peak is max(table, output) rather than their sum). Inside, the returning form wraps a `bytes` in an `ostream_buff` (`include/alxbase/astream.h:159`) and shares the one stretch of writing logic with the streaming form.
- **The read contract**: a cell is always `std::string`; RFC 4180 quoting (`"` around it, `""` for one literal quote, the delimiter and line breaks allowed inside); `\r\n` / `\n` / `\r` all end a row, and the last row may omit its break; **a blank line is not a record** (a row that opens and ends at once is skipped -- "a single empty field" and "a blank line" cannot be told apart in the text, and skipping is the reading Go takes, which keeps strict mode from being killed by a blank line at the end of a file); whitespace outside quotes is data (nothing is trimmed); row 0 is kept as it stands. **An unclosed quote, junk behind a closing quote, a quote inside a field that was not opened with one, and a delimiter outside the range (= quote / line break / `\0`) all fail the whole table** -- the loose mode relaxes the field count and that dimension alone.
- **Strict / loose** (`_strict`): strict (the default) requires every record to carry as many fields as row 0 held, else the whole table fails; loose pads and truncates nothing, so each row keeps its own field count and the matrix may be ragged.
- **Encoding**: a first pass looks for any high byte -- plain ASCII (the common case for a text csv) is parsed as UTF-8 directly, **zero-copy, never disturbing iconv**; only with a high byte present does `detect_format` (`source/alxbase/astring.cpp:108`) probe: **UTF-16 is converted as a whole** (the delimiter is two bytes there and cannot be cut cell by cell); **any other non-UTF-8 (GBK and the like) is split on the raw bytes and converted cell by cell** (`source/alxbase/acsv.cpp:211`) -- a cell that cannot be converted fails the whole table, no half of it is handed out. **Input holding one BOM alone is an empty table** (not a failure).
- **The delimiter has to be ASCII** (`< 0x80`, and not `"` / CR / LF / NUL): the splitting happens on the raw bytes, and only a trailing-byte range of a multi-byte encoding (0x40-0xFE for GBK) that does not overlap the delimiter cuts cleanly.
- **Writing takes text cells only**: a cell must hold a `std::string`, else the whole call fails (an empty `bytes` / `false`) -- the module does not pick a float format for the caller, because the library's `%.6f` with trailing zeros dropped (`source/alxscpt/script/ascript_utils.h:102`) is for **display**, and writing a data format with it would lose precision silently; a numeric cell is the caller's to format, or a later addition can follow the shortest round-trip form of `source/alxbase/ajson.cpp:38`.
- **A quote goes on only when it is needed**: the cell holds the delimiter, a quote or a line break, or its first or last byte is a space or lower (plenty of readers trim). Two special cases: **the only cell of a row, when empty, is written `""`**, and **a row with no cells at all is written `""` too** -- otherwise that line is a blank line in the text, skipped on the way back in, and the data vanishes.
- **No algorithm layer**: no sorting, grouping or joining. Over a plain text matrix "sort" has no single right answer (lexicographic versus numeric), so picking one in the library gives the other half of the cases a **silent wrong answer** (a downstream host has a comment on a test case recording exactly this trap); a comparator callback in C++ has already been judged "not worth it" on that side too. An unambiguous structural helper (locating a column by its header, say) waits until there is a real sticking point.
- **No input size cap**: the entry takes the buffer the caller already holds, not a size claimed in a file header -- so there is no "forged size" attack surface here (against the compression side of `doc/core/design.md` §2.3, where a claimed size has to be clamped). The memory blow-up of the matrix (one `variant` plus one `std::string` per cell, empirically 3~5 times the text) is for the caller to weigh by row count; the library sets no threshold for it.

---

## 5. String utilities

- **`find` / `rfind` dispatch on the pattern length** (`source/alxbase/aalgo.cpp:27-34`, and the same table for rfind): 1 / 2 / 4 / 8-byte patterns go to a whole-value comparison (read point by point as `uint_8/16/32/64`); the rest at 8 bytes or fewer in a window under 512 go to brute force; 16 bytes or fewer in a window under 512 to Rabin-Karp; longer, or a larger window, to Boyer-Moore. The reason for the dispatch is that searches mostly happen on **protocol boundary scans** (looking for `\r\n\r\n`, looking for a delimiter) where short patterns are the overwhelming majority, and there a value comparison or a brute-force match beats a general algorithm on a short window. `find_by_kmp` is still in `aalgo.h`, with no caller at present.
- **Two interfaces, "raw pointer + length" and `std::string`**: the core implementation knows only `(ptr, len)`, and the `std::string` version is a thin wrapper. That way a zero-copy case over a `bytes_view` / `bytes` reuses it directly, with no `std::string` built first.
- **`format` uses `%1 %2 …` placeholders** (not printf style): it avoids runtime errors such as a format string that does not match its argument types, and every argument is handled as a `std::string`.
- **Code conversion**: `to_wstring` / `from_wstring` / `code_conver` go through the platform APIs (on POSIX the locale/iconv family, on Windows the `MultiByteToWideChar` family), and `detect_format` probes the source encoding from the BOM and the byte distribution.

---

## 6. Binary serialization

### 6.1 Layers

```
serable    -- element types and data type definitions (data size / stack-only / type id)
serer      -- encode: write the head (type + name + data size) → payload → tail
deserer    -- decode: walk the metadata → dispatch to the handler per type
```

The format is **self-describing**: every node carries a type id and a data size, so the decoding side needs no schema. Type handlers are registered in the three tables `deserer::CORE_BASE` / `CORE__VEC` / `CORE_LIST`, dispatched by the `BIT16` type id.

### 6.2 Path extraction `varsolid::get_value`

The goal is **to reach one field without decoding the whole thing**: locate the data area straight from `serer::HEAD`, then parse the metadata node by node (a map matched by key, a vec/list counted by index) and decode the node that hits. It suits "read a few fields out of a large configuration packet".

- A type id that does not match, a path segment that is not valid, and an index out of range all answer `_def` as the interface promises (**no exception**).
- The model of `varsolid`: **solid does not transcode strings -- every value is raw bytes** -- and the path step is a positional parse: a path component is used as a decimal index in the index branch.

> Revision log: numeric keys were once parsed with `std::all_of(isdigit)` + `std::stoull`, where an empty string threw `invalid_argument` and an over-long number threw `out_of_range`, contradicting the "answers `_def`" contract. Changed to a hand-written bounded accumulation (empty, non-decimal and overflow all return false). **The same pattern lived in one more place, `variant::select`**, and was fixed there with the same bounded accumulation.

### 6.3 Boundary discipline on the decoding side

Serialized data may come from outside (a disk, a network), so the decoding side treats **metadata as untrusted**:

- `r_interpret(ptr, size)` **truncates rather than over-reading** a non-multiple length (`count = size / sizeof(T)`, the trailing remainder dropped);
- list decoding stops when the space left is short of one whole element (half an element is never read);
- a fixed-width scalar is checked for `size >= sizeof(T)` before decoding, and a short one **stays at its default** and is not written into the target.

---

## 7. Factory and self-registration

`factory<TYPE, Args...>` holds a static `name → creator` table; `product<IMPL, TYPE, Args...>` is a static registrar whose static member `_register` is constructed during **static initialization** and registers `IMPL` with the factory on the way.

- The name registered is `typeid(IMPL).name()`: mangled under GCC/Clang and restored with `abi::__cxa_demangle`, while under MSVC the `typeid` name is taken as it is with the `class ` / `struct ` prefix cut off. `create("circle")` therefore looks up the **demangled, fully qualified name**: namespaces are kept as they are (on the MSVC side only the `class ` / `struct ` prefix is cut). The library's own implementations all sit in the global scope, so an implementation's name is its class name; a host that puts its implementation class in a namespace registers and looks it up under the namespace prefix.
- The gain: a new implementation is one class to write, with no registry to edit; the price: the registry is free of static initialization order (each table is a function-local static), but **the linker may drop an object file nothing references**, so a scenario that creates by name has to make sure the object file holding the implementation class is linked in.

---

## 8. Trade-offs in the other facilities

| Facility | Decision | Rationale |
|---|---|---|
| `signal` | many reads, few writes: a `shared_mutex` to emit under and a `unique_lock` to connect under; below C++17 it degrades to a `mutex` | an emission is the hot path (message dispatch) and a connect the cold one |
| `single<T>` | function-local `static T* obj = new T();`, **never destroyed** | C++11 guarantees the initialization to be once-only and thread-safe; it is not destroyed because another static destructor may still be logging (`global_logger` is one such user) |
| `ref_count` | three states (unsharable / static / ordinary count) | a "static object" has to be expressible as "never destroyed", or a static `bytes` breaks on destruction |
| `anyptr` | a handle of deleter plus cloner plus reference count | it lets a `variant` carry an arbitrary host object while keeping value semantics |
| `noncopyable` | copy deleted, **move kept** | combining it with base classes such as `auto_delete` / `object` needs no extra move operations written |
| `object` | links to its parent at construction and unlinks at destruction | a tree (GUI widgets, AST nodes) needs no hand-kept unlink bookkeeping |

---

## 9. Revision log (behaviour changes that touch the design)

| Topic | Change | Reason |
|---|---|---|
| `bytes::resize` | detach before changing `size` on a shared block | `size` lives in the shared block header, so not detaching pollutes every alias |
| `aserial` read points | the inline scalar `r_interpret` is gone and the read/write points go through `m_interpret` (`memcpy`) and its writing overload | a serialization buffer sits at arbitrary offsets, so reading an `uint_32`/`uint_64`/`HEAD` straight off it is an unaligned access (34 UBSAN reports); the reading `m_interpret` now also goes through an `alignas` scratch buffer, which covers a type with no default constructor (a BMP header) |
| `aserial` nameless node | `swrite_head` skips the `memcpy` when the length is 0 | the caller passes null for an empty name, and `memcpy(p, nullptr, 0)` is UB (UBSAN reports `null pointer passed as argument 2`) |
| `bytes` allocation entry points | `allocate` / `reallocate` test for an "impossible size" first (both the `1 << _align` step and the `+ sizeof(bytes_block)` step are tested for wraparound); the 1.5x growth of `ensure_capacity` tests for wraparound; `bytes::reallocate` / `ensure_capacity` return the block unchanged when it is null | a wrapped-around `malloc` argument is a small value while the block header records the pre-wrap amount, so the object **lies about** a `size()`/`capacity()` of the order of 2^64 (measured: `bytes(2^64-24)` is non-null and `capacity()` reports 2^64-16) |
| `json_array` | destruction / deep copy / move / value comparison filled in | the raw-pointer vector had no ownership at all (elements leaked and copies were shallow) |
| `json` lexer | `lex_json` checks the bound on entry | reading before checking walked one byte past the end while `_next == _size` |
| `varsolid` numeric keys | changed to a hand-written bounded accumulation | `stoull` throws on an empty string and on an over-long number, contradicting the contract |
| `single<T>` | `call_once` plus a raw-pointer fast path dropped in favour of a function-local static | in the old implementation the fast path read a raw pointer while the write happened inside `call_once`, so two threads taking the singleton for the first time raced |
| `xml` attribute parsing | the cursor is committed only on success, and a missing `=` fails at once | after a failure the cursor had already moved, which made a caller retry off by one and misread `<a />` as an attribute |
| `regex_ex` copy assignment | `delete regx` before the assignment | the old implementation overwrote the pointer and leaked the previous compiled regex |
| `strutil::split` | an empty delimiter is defined as "cut into single bytes" | the old implementation fell into a boundary branch of `find`, with an undefined result |
| `bytes::append` | throws when it cannot grow (`length_error` / `bad_alloc`) instead of carrying on into the `memcpy` | carrying on after a failed allocation writes out of bounds, while a silent early return cannot tell "nothing appended" from "an empty append" |
| `json` conversion | moving entries added (`from_*(&&)` / `take_*`) | const overloads alone meant two representations alive at once while converting a large value (a host hits its memory wall) |
| `json` serialization | the private writers go append-style; the new `&&` entries eat the tree as they write | a leaf string was copied twice and every subtree built one more temporary string; serializing a large value had to feed the tree and the text both |
| `variant::select` | index parsing changed to a bounded accumulation: an empty key, a non-decimal one and an overflow all answer `_def` as the contract says | `std::stoull` throws on an empty string and on an over-long number, contradicting the contract that "any segment that does not resolve answers `_def`" (the same origin as the path parsing of `aserial` / `varsolid`) |

## 10. Encryption (AES)

### 10.1 The three-layer structure

```
aes_base          the algorithm core: key expansion, the round functions, GHASH, padding (stateless, pure functions)
  └ aes<128|192|256>   holds round_key, defines key_len / round_num
      └ aes_iv<...>        adds the IV and its backup (for reset)
          ├ aes_cbc
          └ aes_ctr
      └ aes_gcm            carries its own counter/ghash state, protected inheritance (the core stays hidden)
```

A work mode is **a template plus virtual functions**: the bit size is fixed at compile time (`key_exp_size` is a constant, `round_key` an on-stack array, with no heap allocation) and the mode lives in the polymorphic layer.

### 10.2 The soft / hard dual implementation

`cipher` / `inv_cipher` / `ghash` each come in a `_soft` and a `_hard` version, and each probes the CPU's capabilities at run time to pick one (`aes_base::hardcal()` reports AES-NI alone; GHASH is decided by a separate PCLMULQDQ probe). `TRY_AES_HARD` controls whether the hard path and the 16-byte aligned key array are enabled at all; with it off both probes can only answer false and every `_hard` entry point is an empty body.

**GHASH's hard path computes in the reflected domain**: GCM numbers a block's bits from left to right, the leftmost being the coefficient of `y^0`; reversing the bit order of every byte (leaving the byte order alone) turns it into the plain polynomial "bit `q` is the coefficient of `y^q`", with the same sparse modulus as before, `y^128 + y^7 + y^2 + y + 1` -- the 128x128-bit product is assembled with PCLMULQDQ, and the high 128 bits fold back into the low ones with two passes of a multiplication by `0x87`. Both the reflection and its inverse happen inside `ghash_mul_hard`, so the two hardware entry points are semantically word-for-word identical to `ghash_soft` / `ghash_mul_soft`; they are **private**, like the `cipher_impl_hard` family, and reachable only through the self-probing `ghash()` -- a public entry point cannot carry a precondition such as "the CPU must have PCLMULQDQ" that the caller has no way to check.

> Why the reflected subkey is not cached in `aes_gcm`: each GHASH block is one serial dependency chain (XOR → multiply → reflect), and the subkey reflection is unrelated to that chain, so out-of-order execution hides it inside the multiply's latency -- measured, with GCM's present serial usage there is **no difference**, and only reshaping the interleaving into a throughput-bound form could pay it back (and that account should be settled together with the power table for `H`).

### 10.3 The semantics of the padding modes

`buf_padding` / `buf_unpadding` are **public API** (brought in with a `using` in `aes<N>`), because the caller needs to pad before encrypting and strip after decrypting.

| Mode | What is padded |
|---|---|
| `PKCS7` | every byte is the pad length |
| `ZEROS` | fills 0, **nothing appended when already aligned** |
| `ANSIX923` | the first n-1 bytes are 0 and the last byte is the length; a whole block is appended when already aligned |
| `ISO10126` | this implementation fills `0xff` (the standard asks for random) and the last byte is the length; a whole block is appended when already aligned |

> Revision log: `ANSIX923` / `ISO10126` originally padded nothing when "already aligned", while unpadding still stripped by the last byte -- **one implementation contradicting itself** (an aligned buffer's last byte is data itself, so any value below 16 was mis-stripped). Both now "always append a whole block", as the standards say. There is no reliable consistency in old ciphertext worth migrating.

### 10.4 The CTR and GCM counters

`aes_ctr` is a **stream**: `iv_index` records the offset inside the current block and carries across several `xcrypt` calls; the counter is incremented as one big-endian number (the carry walks from the last byte backwards). `encrypt` / `decrypt` share `xcrypt`.

`aes_gcm` keeps J0 / the counter / the GHASH state in the object; the three-part `start(aad)` → `encrypt`/`decrypt` → `finish(tag)` guarantees that both the AAD and the ciphertext go into GHASH, and `finish` runs `reset()` on its way out (so the object is reusable).

---

## 11. Digest

- **CRC is parameterized by template**: `crc_base<CRC, POLY, INIT, XORO, INRE, OURE>` makes the polynomial, the initial value, the output xor and the input/output reflection all compile-time parameters, and `CRC_32` and `CRC_32C` are just two sets of them. The lookup table is built lazily on first use by `static const bool initialized = [] { init(); return true; }();`.
- **SHA-1 / SHA-256 have a hardware path**: after probing for SHA-NI they run `__m128i` instructions (holding the round constant table and the mask table), and otherwise fall back to the pure software implementation; `hardcal()` lets the caller ask which of the two is in use.
- **One factory**: the algorithms self-register into the `verify` factory through `alx::product`, `list()` gives the registered names, and `create(enum)` / `create(name)` are the two ways in.

---
