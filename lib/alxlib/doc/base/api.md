# alxbase — API Manual

`alxbase` is the **zero-dependency base layer** of AlxLib: byte buffers, type-erased values, JSON/XML, string utilities, binary serialization, the factory and a few general facilities. The layers above -- `alxcore` / `alxcomm` / `alxscpt` -- all depend on it.

- Header directory: `include/alxbase/`
- Library: `libalxbase.so` (or the merged static library `alxlib.a`)
- Namespace: `alx` (string utilities under `alx::strutil`, serialization under `alx::ser`)

```bash
g++ -std=c++17 -I include -I include/alxbase main.cpp \
    -L bin -lalxbase -Wl,-rpath,$PWD/bin
```

This manual is about **how to use** the library; the relevant edge cases are stated where they come up.

---

## 1. Basic types and utilities (`abase.h` / `autility.h` / `aalgo.h` / `arefcount.h`)

### 1.1 Fixed-width aliases and constants

| Alias | Meaning | Alias | Meaning |
|---|---|---|---|
| `int_8` / `int_16` / `int_32` / `int_64` | `char` / `short` / `int` / `long long` | `uint_8` … `uint_64` | the matching unsigned types |
| `real_32` / `real_64` | `float` / `double` | `uint_XX_npos` | the not-found sentinel of that width (its own maximum) |

Constants: `max_int_32` / `max_uint_64` / `max_real_64`, `min_int_32` … and `uint_64_npos` (= `max_uint_64`, what a failed search returns).

### 1.2 Compile-time classification traits

```cpp
alx::is_integer<T>    // int_8/16/32/64
alx::is_uinteger<T>   // uint_8/16/32/64
alx::is_realnum<T>    // real_32/64
alx::is_number<T>     // one of the three above
alx::is_lossless<From, To>   // whether From → To loses nothing (no narrowing, no lost sign, no float truncation)
```

`is_lossless` is the check behind the cross-type numeric conversion of `variant::to<T>()`: the conversion happens only when nothing is lost, and the default comes back otherwise.

### 1.3 Bit and byte operations

```cpp
alx::byte_reverse(v);        // reverse the byte order (16/32/64)
alx::bit_reverse(v);         // reverse the bit order (8/16/32/64)
alx::bit_align(v, n);        // round up to a 2^n boundary; the compile-time form is bit_align_v<v, n>
alx::bit_get(obj, offset);   // read bit offset
alx::bit_set(obj, offset, state);
```

### 1.4 General helpers

```cpp
alx::safe_delete(ptr);            // delete then null; pass true as the second argument for an array
alx::r_interpret<T>(ptr);         // view the memory at ptr as a T&; ptr has to be aligned for T
alx::m_interpret<T>(ptr);         // the same read, answered by value through memcpy, any alignment
alx::m_interpret(ptr, v);         // the writing side: memcpy v into ptr
alx::max_value(a, b); alx::min_value(a, b); alx::border(v, lo, hi);
alx::cond_value(cond, t, f);      // the ternary operator as a function
alx::map_value(map, key, def);    // map lookup, def on a miss (std::map / unordered_map alike)
alx::this_tid();                  // the calling thread's id as an integer
alx::fnv1a_32(str); alx::fnv1a_64(str);   // compile-time FNV-1a hash
```

Which of the two to pick: the whole buffer of a `bytes` is aligned from offset 0 on and can be read directly, while **`bytes_view` gives no alignment guarantee** (it is a sub-view at an arbitrary offset), and neither does a cursor walking a serialized buffer or a message, nor a `void*` handed in by the caller -- those three always use `m_interpret`. An internal buffer that is aligned for what goes in it (a member `uint_8[64]`, say) keeps being read directly, leaving the alignment duty to one `memcpy` at the boundary.

### 1.5 General facilities

**`noncopyable`** -- the base class that deletes copying and keeps moving; `alx::is_noncopyable<T>` tests for it.

**`signal<Args...>`** -- a thread-safe signal/slot, where `connect` takes the write lock and `exec` / `operator()` take the read lock to emit:

```cpp
alx::signal<const std::string&> on_msg;
on_msg.connect([](const std::string& _s) { printf("%s\n", _s.c_str()); });
on_msg.exec("hello");
```

Forwarding to another signal goes through `connect(const signal&)`; `clear()` drops them all. No write lock is taken while an emission runs, so a slot must **not** `connect` / `clear` the same signal.

**`single<T>`** -- a thread-safe lazy singleton (a C++11 function-local static) that is **not destroyed** at process exit:

```cpp
struct config { int port = 8080; };
alx::config& cfg = *alx::single<alx::config>::instance();
```

**`auto_delete<T>`** -- an RAII guard for a raw pointer, where `take()` hands the ownership on (the destructor deletes nothing then) and `drop()` only abandons the management.

### 1.6 Reference count (`arefcount.h`)

`ref_count` is the atomic reference counter a hand-written handle is built on, with three states: `unsharable` (no sharing), `static_ref` (a static object, never destroyed) and an ordinary count.

```cpp
ref.init_owned();          // count = 1
if (ref.ref()) { ... }     // raise the count; false when the object may not be shared
if (ref.deref()) { ... }   // lower the count; true means "still alive", false that the caller has to destroy it
ref.set_sharable(false);   // refuse sharing from now on (a later ref() returns false)
ref.is_shared();           // count > 1
```

### 1.7 Parent/child objects (`aobject.h`)

`object` provides a parent pointer and automatic unlinking (the constructor takes `_parent` and the node is detached from its parent at destruction). It is meant as the base of objects that hang in a tree.

> **Not implemented yet**: `aobject.h` ships declarations only, there is no implementation file for it in the repository (the `.cmake` list does not carry one), and instantiating it fails to link. For now it is only reserved in the design.

---

## 2. `bytes` / `bytes_view` -- byte buffers (`abytes.h`)

`bytes` is a **copy-on-write (COW)** byte buffer: copy construction and copy assignment only raise a reference count, and the deep copy (detach) happens when something really writes. `bytes_view` is a zero-copy read-only window.

### 2.1 Construction

```cpp
alx::bytes a;                                  // empty (null)
alx::bytes b(1024);                            // 1024 bytes, uninitialized
alx::bytes c(16, 0xFF);                        // 16 bytes, every one 0xFF
alx::bytes d("hello");                         // from a C string (the terminating '\0' is not part of it)
alx::bytes e(std::string("hello"));            // from a std::string
alx::bytes f(ptr, len);                        // copy len bytes from raw memory
alx::bytes g = alx::bytes::from_hex("48656c6c6f");   // "Hello" (either case is accepted)
alx::bytes h = alx::bytes::from_base64("SGVsbG8=");  // "Hello"
alx::bytes i = alx::bytes::from_ordinary(some_struct); // copy the bytes of a POD
```

### 2.2 Capacity and state

```cpp
b.null();      // no block at all (true after default construction)
b.empty();     // null, or size()==0
b.size(); b.capacity();
b.resize(n);   // the added tail is uninitialized; shrinking keeps the allocation
b.reserve(n);  // guarantee a capacity, only ever growing
b.fitsize();   // shrink the allocation so that capacity == size
b.clear();     // release the block (size/capacity read 0) -- an already empty object keeps its block and changes nothing
b.detach();    // deep-copy while shared, after which writing in place is safe
```

> A failed allocation **throws**: a size that cannot be represented (wrap-around) throws `std::length_error` and a malloc that fails throws `std::bad_alloc` -- the same line STL containers take, so a failure never turns into a silently empty object. `null()` still exists, but it means "empty" (default construction / `clear()`) and not "failed".

### 2.3 Data access

```cpp
uint_8* p = b.data();          // the non-const version triggers a detach
const uint_8* cp = b.cdata();  // read-only, no detach
b[0];                          // no bounds check
T& v = b.to<T>(offset);        // interpret the bytes at offset as a T (size and alignment are the caller's)
```

### 2.4 Slicing and searching

The copying and the zero-copy form come in pairs:

| Operation | Copying (returns `bytes`) | Zero-copy (returns `bytes_view`) |
|---|---|---|
| `_len` bytes from `_pos` | `mid(pos, len)` | `mid_view(pos, len)` |
| the left `_len` bytes | `left(len)` | `left_view(len)` |
| the right `_len` bytes | `right(len)` | `right_view(len)` |

`_len` is always **clamped** to the length available (never past the end, never an error).

```cpp
uint_64 i = b.find("HTTP", 4);          // search forwards; uint_64_npos when it is not there
uint_64 j = b.find("HTTP", 4, 10, 100); // limited to [10, 100)
uint_64 k = b.rfind(tag, tag_len);      // search backwards
b.find_ordinary(some_struct);           // find the bytes of a POD
bool  eq = b.equa("GET", 3, 0);         // compare 3 bytes from offset 0
b.equa_ordinary(some_struct, 8);
```

`find`'s `_from` / `_to` are a **half-open range**; `rfind`'s are **both inclusive** (`_rfrom` is the highest offset the pattern's *last* byte may occupy, `_rto` the lowest its *first* byte may occupy). `_to` passed `max_uint_64` (the default) means the end of the buffer.

### 2.5 Appending and writing out

```cpp
b.append("abc", 3);      b.append(std::string("x"));   b.append(other_bytes);
b.append_ordinary(pod);  // append the bytes of a POD
b << "abc" << other_bytes << std::string("y");         // operator<<
```

### 2.6 Conversion

```cpp
std::string s = b.to_string();       // the raw bytes (may hold '\0')
std::string hx = b.to_hex();         // a hex string, lowercase
std::string b64 = b.to_base64();
std::vector<uint_8> v = b.to_vector();
b.revers();                          // reverse the byte order in place
```

### 2.7 `bytes_view` -- the zero-copy window

`bytes_view` **holds the underlying `bytes` by value** (which keeps it alive) along with an offset and a length, and every position parameter is **relative to the window's first byte**:

```cpp
alx::bytes buf = read_something();
alx::bytes_view head = buf.left_view(16);        // the first 16 bytes, no copy
alx::bytes_view body = buf.mid_view(16);         // drop the head
uint_64 i = body.find("\r\n\r\n", 4);            // the position is relative to body's first byte
alx::bytes owned = body.to_bytes();              // materialize only when ownership is needed
```

The read-only interface is complete (`find` / `rfind` / `mid` / `left` / `right` / `equa` / `to<T>`) and there is no writing one to go with it. The `data()` of an empty view is `nullptr` only while the underlying `bytes` is `null()` -- `empty()` looks at the length and `data()` at the block, so test emptiness with `empty()` / `null()`.

---

## 3. `variant` and the containers (`avariant.h`)

### 3.1 Supported types

`variant` is a type-erased value container supporting `char` / `short` / `int` / `long long`, the four unsigned twins, `float` / `double`, `bool`, `bytes`, `std::string`, `varmap`, `varvec`, `varlst` and `anyptr`.

```cpp
alx::variant v1 = 42;
alx::variant v2 = std::string("hello");
alx::variant v3 = std::vector<int>{1, 2, 3};   // this concrete type is stored, not a varvec
alx::variant v4;                                // empty (null)
```

The type is sorted out at construction: an `int` is stored as an `int`, `1.0f` as a `float`, and a `char*` as a `std::string`.

### 3.2 Reading a value

```cpp
int n = v1.to<int>();                 // on a type mismatch the default comes back (0 here); nothing is thrown
int m = v1.to<int>(7);                // with a default of your own
double d = v1.to<double>();           // across numeric types: converted only when nothing is lost (int → double is lossless)
v1.is<int>();  v1.is_string();  v1.is_map();  v1.is_vec();  v1.is_lst();
v1.is_integer(); v1.is_uinteger(); v1.is_number();
v1.to_integer();  // any signed integer → long long
v1.to_number();   // any numeric type → double
v1.type();        // the current type index; -1 means null
v1.type_name();   // the RTTI name (compiler-dependent)
```

**Note**: a cross-type numeric conversion goes through `is_lossless`, so a lossy direction such as `int → unsigned int` or `long long → int` answers the default rather than a truncated value.

### 3.3 Changing a value and overwriting

```cpp
v1 = 3.14;                 // straight assignment, and the type changes with it
int& r = v1.as<int>();     // on a type mismatch: `int{}` is stored in place and its reference comes back
int& r2 = v1.as<int>(7);   // on a type mismatch: 7 is stored
v1.destroy();              // empty it (back to null)
v1.null();
```

The difference between `as<T>()` and `to<T>()`: `to` only reads and hands back a default on a mismatch, while `as` **changes** the current value.

### 3.4 Container types

```cpp
alx::varmap m;                       // std::map<std::string, variant>, in key order
m.insert("port", 8080);              // a key already there is not overwritten; {it, false} comes back
m["name"] = "alx";                   // a key that is absent is created with a default, then assigned
m.contain("port");  m.value("port", 0);  m.erase("port");  m.clear();
for (auto it = m.begin(); it != m.end(); ++it) it.key(), it.value();

alx::varvec vec;   vec.push_back(1);  vec.push_back("x");   // std::vector<variant>
alx::varlst lst;   lst.push_back(1);                        // std::list<variant>
```

`varmap` / `varvec` / `varlst` are all **deep-copy value types** and can be built straight from a standard container: `varmap(std::map<std::string,int>)`, `varvec(std::vector<T>)`, `varlst(std::list<T>)`.

### 3.5 Path navigation `select`

```cpp
alx::variant root = /* nested map/vec */;
const alx::variant& leaf = root.select({"server", "0", "port"}, alx::variant::def_val());
```

`select` descends segment by segment: a segment hits a key of a `varmap`, or is used as a decimal index into a `varvec` / `varlst`; any segment that misses answers `_def`.

An index segment goes through a **bounded decimal parse**: an empty segment, one holding a non-decimal character, and one beyond `uint_64` are all taken as "no match" and answer `_def`.

### 3.6 Comparison

`variant` supports `==` / `!=` (a comparison with a value of the same type; a different type is false), and a container type compares element by element.

---

## 4. JSON (`ajson.h`)

### 4.1 Value types

`json_value` supports 6: `json_object`, `json_array`, `long long`, `double`, `bool`, `std::string`.

```cpp
alx::json_value v1 = 42;              // every integer type is widened to long long
alx::json_value v2 = 3.14;            // every float type becomes a double
alx::json_value v3 = "text";          // char* → std::string
alx::json_value v4 = alx::bytes(...); // bytes is Base64-encoded into a string automatically
alx::json_value v5 = true;
```

### 4.2 Reading a value

```cpp
bool ok = false;
long long n = v1.to_intg(0, &ok);     // on a type mismatch _ok = false and the default comes back
const std::string& s = v3.to_string({}, &ok);
v1.is_null(); v1.is_intg(); v1.is_bool(); v1.is_double();
v1.is_string(); v1.is_array(); v1.is_object();
```

Every `to_*` has a const and a non-const version; the non-const one returns a writable reference.

### 4.3 Objects and arrays

```cpp
alx::json_object obj;
obj.insert("name", alx::json_value("alx"));   // a key already there is not overwritten
obj["port"] = 8080;                           // created when it is absent
obj.contain("name");  obj.erase("name");  obj.clear();

alx::json_array arr;
arr.append(alx::json_value(1));               // append makes a copy of its own, and the array owns it
arr.append(2);                                // json_value built implicitly
arr.size();  arr[0];  arr.at(0);
arr.clear();                                  // delete every element (so does the destructor)
arr == other;                                 // element-by-element comparison (not a comparison of pointers)
```

Inside, `json_array` is a `std::vector<json_value*>`: a copy is **deep**, a move takes the elements over, and destruction deletes them. The `erase` / `pop_back` / `resize` inherited from vector **know nothing of ownership** (they leak or double-free), so pair them with hand-kept bookkeeping if you use them.

### 4.4 Parsing and generating

```cpp
// parse
bool ok = false;
alx::json_object obj = alx::json_doc::from_json(std::string(R"({"a":1})"), &ok);
alx::json_value  val = alx::json_doc::from_value("[1,2,3]", &ok);
if (!ok) { /* the parse failed, an empty object came back */ }

// generate
std::string compact   = alx::json_doc::to_json(obj, true);
std::string indented  = alx::json_doc::to_json(obj, false);
std::string one_value = alx::json_doc::to_string(val, true);

// moving form: the tree is eaten as it is serialized (each entry handed back the moment it is written,
// so the peak is on the order of max(tree, text))
std::string compact2  = alx::json_doc::to_json(std::move(obj), true);   // obj is an empty shell afterwards
std::string one_value2 = alx::json_doc::to_string(std::move(val), true); // the payload is taken; the container stays

// string escaping: the first _ofst bytes of _dst are left alone, the escaped text is written from _ofst on,
// and _dst is resized to _ofst + the result length
std::string s = "\"a\"\n";
alx::json_doc::escape(s);                    // the whole string in place = escape(s, s, 0); the old signature stays
std::string out = "prefix";
alx::json_doc::escape(s, out, out.size());   // appending: one pass, flat runs copied in bulk
alx::json_doc::descape(s);                   // unescape in place; the three-argument form follows escape's contract
```

The escape set follows RFC 8259: `"`, `\` and every control character in U+0000-001F (the short forms `\b \f \n \r \t`, the rest `\u00xx`); keys and values follow the same set.

The test for a failed parse always goes through `_ok`: without it, a failure cannot be told from "the content was empty".

### 4.5 Interconversion with `variant`

```cpp
alx::variant var = json_value.to_variant();          // json → variant
alx::json_value back = alx::json_value::from_variant(var);   // variant → json

alx::varmap vmap = obj.to_varmap();
alx::json_object obj2 = alx::json_object::from_varmap(vmap);
alx::varvec vvec = arr.to_varvec();
alx::varlst vlst = arr.to_varlst();
alx::json_array arr2 = alx::json_array::from_varvec(vvec);
alx::json_array arr3 = alx::json_array::from_varlst(vlst);
```

The mapping `from_variant` applies: an integer → `long long`, a float → `double`, `bytes` → a Base64 string, `varvec` → `json_array`, `varmap` → `json_object`; an unsupported type gives a null `json_value`.

**Moving conversion** (the payloads change hands one by one and the source is left an empty shell that **may only be destroyed afterwards**):

```cpp
// forward: a static overload, so only std::move (or a temporary) selects the moving version, and an lvalue falls back to the copying one
alx::json_value jv = alx::json_value::from_variant(std::move(var));
alx::json_object o2 = alx::json_object::from_varmap(std::move(vmap2));
alx::json_array a2 = alx::json_array::from_varvec(std::move(vvec2));
alx::json_array a3 = alx::json_array::from_varlst(std::move(vlst2));

// reverse: the take_ name itself says "take it away and leave it empty" (as mmap::take_all does); an lvalue or an rvalue both work
alx::variant back2 = jv.take_variant();
alx::varmap vm = obj.take_varmap();
alx::varvec vv = arr.take_varvec();
alx::varlst vl = arr.take_varlst();
```

- The semantics: the heap buffers of `std::string` / `bytes` / containers **change hands one by one** (no copy), so the peak is one copy and not two, and the source is an empty shell (same type, no content).
- Why the reverse is another name rather than `to_variant() &&`: giving a parameterless const member a `&&` qualifier first forces `const &` onto the existing one, which changes the symbol and breaks the ABI (`_ZNK…` → `_ZNKR…`); `take_*` costs nothing.
- The two that cannot be spared: `bytes` needs Base64 encoding (which necessarily allocates a copy), and a stack-inlined scalar has no heap block to begin with.

---

## 5. XML (`axml.h`)

`xml_value` is one element: a name plus an attribute table plus a content list. `xml_object` is the document root, with the "global content" (comments, PIs, DTD) around it.

### 5.1 Structure

```cpp
alx::xml_object doc("root");
alx::xml_value& child = *doc.cont().add_elem("item", {{"id", "1"}}, {"text"});
child.attr().insert("class", "a");     // the attribute table (unordered_map)
child.cont().add_text("hello");        // append text
child.cont().add_note("a comment");    // a comment
child.cont().add_cdat("<raw>");        // CDATA
child.cont().add_enty("&amp;");        // an entity
```

Content kinds (`xml_value::content_type`): `ELEM`, `TEXT`, `NOTE`, `PI__`, `DTD_`, `CDAT`, `ENTY`.

### 5.2 Walking it

```cpp
const std::list<alx::xml_value::content_meta>& items = elem.cont().container();
for (const auto& meta : items) {
    if (meta.is_type(alx::xml_value::TEXT)) { const std::string& t = *meta.stri_; }
    if (meta.is_type(alx::xml_value::ELEM)) { const alx::xml_value& e = *meta.elem_; }
}
std::list<alx::xml_value*> hits = elem.cont().get_elem("item");   // child elements by name
elem.attr().value("id");     elem.attr().contain("id");
```

### 5.3 Parsing and generating

```cpp
bool ok = false;
alx::xml_object doc = alx::xml_object::from_bytes(std::string_view_bytes, /*skip_blanks=*/true, &ok);

alx::bytes out = doc.to_bytes(false);        // the compact and the indented form
std::string esc = alx::xml_value::escape("<a&b>");
alx::json_value j = elem.to_json();          // element → JSON view
```

---

## 6. CSV (`acsv.h`)

Namespace `alx::csv`. A **plain text matrix**: the outer `varvec` is rows and the inner one cells, with `row 0` the header row and every cell a `std::string`. The core takes "raw pointer + length" (a `bytes` / `mmap` / socket buffer can be fed straight in), and the `std::string` version is a thin wrapper in the header.

### 6.1 Reading

```cpp
bool ok = false;
alx::varvec rows = alx::csv::from_bytes(text.data(), (alx::uint_64) text.size(),
                                        /*delim=*/',', /*strict=*/true, &ok);
// or feed a std::string directly
alx::varvec same = alx::csv::from_bytes(text, ',', true, &ok);

// reading a value takes two hops: table → row → cell (variant does not index)
std::string head = rows[0].to<alx::varvec>()[0].to<std::string>();  // cell 0 of the header row
std::string cell = rows[2].to<alx::varvec>()[1].to<std::string>();  // cell 1 of row 2

// loose mode: no field count is checked, so each row keeps its own (the matrix may be ragged)
alx::varvec loose = alx::csv::from_bytes(text, ',', /*strict=*/false, &ok);
```

The encoding is detected automatically (UTF-8 read as is, a UTF-8 BOM stripped, UTF-16 converted as a whole / GBK cell by cell) and the output is always UTF-8. A failure is always "an empty `varvec` and `*_ok == false`": an unclosed quote, junk behind a closing quote, a quote inside a field that was not opened with one, a delimiter outside the allowed range, a cell the cell-by-cell conversion cannot decode (a mangled sequence cut by the delimiter), and a field count that disagrees in strict mode.

### 6.2 Writing

```cpp
alx::bytes out   = alx::csv::to_bytes(rows);                 // returning form
alx::bytes moved = alx::csv::to_bytes(std::move(rows));      // consuming form: a row handed back per row written, peak max(table, output)

alx::bytes buff;                     // streaming: an append returning false means "does not fit" and stops the walk there
alx::ostream_buff sink(buff);
bool all = alx::csv::to_bytes(rows, sink, ',');
```

A cell must hold a `std::string`, else the whole call fails -- this module does not pick the text form of a number or float for the caller (the library's own float display rule is there for the script to display with, and writing data with it would lose precision silently). **An empty table is written out as a non-null empty `bytes`**; only a failure gives `bytes::null` (the streaming form reports it as a bool). The delimiter has to be ASCII (`< 0x80`, and not `"` / CR / LF / NUL). The output ends rows with `\n`, is UTF-8, carries no BOM, and quotes only where it must; the only cell of a row, when empty, is written `""`, and it still reads back as that one empty cell.

---

## 7. String utilities (`astring.h`)

Namespace `alx::strutil`. The core functions each have a "raw pointer + length" version (for zero-copy use) and a `std::string` convenience one.

### 7.1 Formatting and slicing

```cpp
std::string s = alx::strutil::format("port=%1 host=%2", 8080, "localhost");  // %1 %2 ...
alx::strutil::left(ptr, len, index, count);   // count bytes taken leftwards from index
alx::strutil::right(ptr, len, index, count);  // count bytes taken rightwards from index
```

### 7.2 Searching and splitting

```cpp
alx::strutil::check(str, tag, start);                 // prefix comparison
alx::strutil::find(str, tag, from, to);               // forwards
alx::strutil::rfind(str, tag, rfrom, rto);            // backwards
alx::strutil::split("a,b,c", ",");                    // → {"a","b","c"}
alx::strutil::join({"a","b"}, ",");                   // → "a,b"
alx::strutil::compute_lps(pattern);                   // the KMP LPS table
alx::strutil::stringfy("hello");                      // → "\"hello\"" (wrapped in double quotes)
alx::strutil::remove_all(str, ' ');                   // delete every occurrence in place
```

`split` with an **empty delimiter** cuts into single bytes: it returns `len + 2` elements (one empty element at each end plus one per byte).

### 7.3 UTF-8 encoding and decoding

```cpp
std::string u = alx::strutil::to_utf8(0x4E2D);        // code point → UTF-8 bytes
size_t next = 0;
uint_32 cp = alx::strutil::from_utf8(s, 0, next);     // decode one code point and take the next offset
```

### 7.4 Code conversion

```cpp
alx::strutil::CODE_FORMAT f = alx::strutil::detect_format(ptr, len);   // probe the encoding (a BOM-only read is possible)
std::string utf8 = alx::strutil::code_conver(gbk_str, alx::strutil::UTF8, alx::strutil::GBK);
std::wstring w = alx::strutil::to_wstring(utf8);
std::string back = alx::strutil::from_wstring(w);
```

The format constants: `GBK` / `UTF8` / `UTF8_BOM` / `UTF16_LE` / `UTF16_BE` (with `_BOM` variants), and `AUTO` may be passed as the source format to detect it.

---

## 8. Binary serialization (`avarsolid.h` / `aserial.h`)

### 8.1 `varsolid` -- `varmap` to and from binary

```cpp
alx::varmap m;
m["port"] = 8080;
m["name"] = "alx";

alx::bytes blob = alx::varsolid::to_bytes(m);          // serialize (an empty bytes when it fails)
alx::varmap back = alx::varsolid::to_varmap(blob);     // decode (an empty varmap when it fails)
alx::varsolid::is_valid(blob);                         // check the header and tail only, decoding nothing

// reach a value by path without decoding the whole blob
alx::variant port = alx::varsolid::get_value(blob, {"port"}, 0);
```

A path segment of `get_value`: a key at a `varmap`, a decimal index at a `varvec` / `varlst`; any segment that is not valid or is not there answers `_def` as the contract says.

### 8.2 `aserial` -- the generic serialization framework

`alx::ser` provides the three layers `serable` / `serer` / `deserer`: `serable` defines the element types and the data types, `serer` encodes and `deserer` decodes. Before use, the type handlers that will be needed have to be registered in `deserer::CORE_BASE` / `CORE__VEC` / `CORE_LIST` (inside the library an initialization entry does it).

```cpp
// take one element by path (without parsing the whole blob)
alx::variant v = alx::ser::deserer<...>::parse(blob, {"a", "0"}, alx::variant::def_val());
```

---

## 9. Factory and self-registration (`afactory.h`)

```cpp
// the abstract product
class shape { public: virtual ~shape() = default; virtual double area() const = 0; };

// the factory: create by name
using shape_factory = alx::factory<shape>;            // TYPE, Args...
shape* s = shape_factory::create("circle");           // nullptr when the name is not registered
shape_factory::enable("circle");
shape_factory::regist("circle", [] { return new circle(); });

// or have the implementation register itself
class circle : public alx::product<circle, shape> { /* ... */ };   // <IMPL, TYPE, Args...>
```

`product<IMPL, TYPE, Args...>` registers `IMPL` with the factory under its RTTI name (through `abi::__cxa_demangle` on GCC) during static initialization, and `create` looks it up under the same name. The name is the **fully qualified one** (the demangled result of `typeid(IMPL).name()`, with namespaces kept as they are; only the `class ` / `struct ` prefix is cut on the MSVC side) -- the library's own implementations all sit in the global scope, so an implementation's name is its class name.

---

## 10. The other facilities

### 10.1 `regex_ex` (`aregex_ex.h`)

A thin wrapper over `std::regex` that compiles at construction and reports a failure through `is_valid()`:

```cpp
alx::regex_ex re(R"(\d+)");
re.is_valid();
re.is_compliant("abc123");              // does the whole subject match
re.find("a1b2");                        // the first match
re.find_all("a1b2");                    // every match
re.match_group("2024-01-02");           // the groups (index 0 is the whole match)
re.match_group_all(...);
re.replace(str, "$1");  re.replace_all(...);
re.get_pattern();
```

Construction and copying are `noexcept` (a failed compile does not throw; `is_valid()` is the gate).

**The cost is unbounded, so it serves trusted input only**: `std::regex` has neither a step limit nor a callback point, and a perfectly legal pattern can still blow up on backtracking -- 30 `a`s against `(a+)+b` measured 165 s, doubling with every extra input character, and no time window can be inserted inside this call. When the pattern or the input comes from outside (a script, a proxy, a network), switch to alxcore's `regex_pcre2` (a step limit plus interruptible callouts; see `doc/core/api.md` §16).

### 10.2 `anyptr` (`aanyptr.h`)

A type-erased **owning handle**: the object it holds, the deleter and cloner of its type, and the reference count.

```cpp
alx::anyptr p = alx::anyptr_ex<my_type>::make(new my_type());
my_type* raw = alx::anyptr_ex<my_type>::as(p);      // nullptr when the type does not match
```

`anyptr_ex<T>` wraps a `T*` into an `anyptr` and keeps the type information with it; a `variant` can carry an arbitrary object this way, which is how host objects are passed to the script engine.

### 10.3 The stream abstraction (`astream.h`)

```cpp
// output
alx::bytes buff;
alx::ostream_buff os(buff);
os << std::string("hello") << some_pod;     // append / append_ordinary
os.total();  os.flush();  os.reset();

// input (zero-copy)
alx::istream_buff is(alx::bytes_view(buff));
alx::bytes_view chunk = is.get(0, 5);
is.total();
```

`ostream` / `istream` are pure virtual interfaces; the concrete implementations (a file stream, a compressing stream) live in `astream_ex.h` of `alxcore`.

### 10.4 Type list (`atypelist.h`)

The compile-time `typelist::type_list<...>` and the metafunctions `index_of` / `value_at` / `length_of` / `append_on` and the rest. The type index table of `variant` is built on it; ordinary business code does not use it directly.
