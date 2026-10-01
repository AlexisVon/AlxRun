# alxscpt — API Manual (C++ side)

This is for the **host developer**: how to embed the script engine in a C++ program, register extension capabilities, write a link module, install hooks and channels. The language's own syntax and the engine's internals are in the matching entries of the README document index.

- Header: `include/alxscpt/ascript.h` (the only public header)
- Library: `libalxscpt.so` (or the merged static library `alxlib.a`)
- Namespace: `alx::script`

```bash
g++ -std=c++17 -I include -I include/alxbase -I include/alxcore -I include/alxscpt main.cpp \
    -L bin -lalxscpt -lalxcore -lalxbase -Wl,-rpath,$PWD/bin
```

---

## 1. Minimal embedding

```cpp
#include "ascript.h"

alx::script::engine* eng = alx::script::engine::create();
eng->on_cerr.connect([](const std::string& _s) { fprintf(stderr, "%s\n", _s.c_str()); });

alx::script::engine::result rst = eng->exec("demo.axc");
if (rst.error != alx::script::error_type::NoError) {
    fprintf(stderr, "run failed: %s\n", alx::script::error_type_name(rst.error));
} else {
    // rst.value is the value of the script's last statement
}

delete eng;    // the caller deletes it (engine is not a singleton)
```

The engine ships with **no IO and no extension functions**: `print`, file access and the like are all registered by the host (§5 extension functions, §11 link modules). The engine itself keeps only its diagnostic signals.

---

## 2. Lifecycle and configuration

```cpp
alx::script::engine_config cfg;
cfg.max_stack   = 1024;        // live frames allowed, 0 = unlimited
cfg.max_vecfill = 0;           // elements one vec/lst fill may produce, 0 = unlimited
cfg.parse_depth = 1024;        // syntax nesting depth of one parse, 0 = unlimited
cfg.overflow_check = false;    // integer overflow check

alx::script::engine* eng = alx::script::engine::create(cfg);
eng->config();                 // read the configuration back

// runtime adjustment (affects later compiles and execs only)
eng->set_max_stack(...);  eng->set_parse_depth(...);  eng->set_max_vecfill(...);
eng->set_overflow_check(...);  eng->set_search_paths({"mods", "lib"});
eng->reset();                  // drop the intermediate state of a run: the root store is rebuilt and loaded modules are released with it (hot reload)
```

`engine` is **single-threaded and not reentrant**: one instance runs on one thread at a time. Only two interfaces are meant to cross threads (§10 interrupt).

---

## 3. Execution, calls and compilation

### 3.1 Execution

```cpp
alx::script::engine::result exec(const bytes_view& _data, const std::string& _home_dir);
alx::script::engine::result exec(const std::string& _file_path);   // home_dir becomes the file's own directory
```

The three fields of `result`:

| Field | Meaning |
|---|---|
| `value` | the last statement's value (not a valid result when `error != NoError`) |
| `error` | `error_type`; `NoError` means the run finished normally |
| `elapsed_us` | wall time, in microseconds |

`home_dir` is **the base relative imports resolve against**, and the anchor of the module namespace.

### 3.2 Calling a script function

```cpp
alx::script::engine::result call(const std::string& _name, const varvec& _args);
```

Calls the `def` of that name in the **root scope** (no module path is resolved), passes the arguments as values, and returns the same shape as `exec`.

### 3.3 Compiling into a product

```cpp
bytes compile(const bytes_view& _data, const std::string& _home_dir,
              bool _cmps = false, bool _embed = false, const bytes_view& _hint = bytes_view()) const;
bytes compile(const std::string& _path, bool _cmps = false, bool _embed = false,
              const std::string& _hint = std::string()) const;
```

- `_cmps`: compress the payload.
- `_embed`: resolve and **inline** every `import` into one self-contained product; `link` and `env` are compile errors then, a relative import resolves against the importing module's own directory only, and an import that resolves to nothing is a compile error too. The key of an embedded module is `"@" + sha16(absolute path + content)`.
- `_hint`: the host payload. The `compile(source, ...)` overload **stores it as given and never interprets it**; the `compile(path, ...)` overload reads it as a **file path** (`source/alxscpt/ascript.cpp:456`), so passing raw JSON yields an empty hint. An empty one leaves no `hint` key in the product (backward compatible).

### 3.4 Reading a product back

```cpp
static bool unpack(const bytes_view& _axp, varmap& _out);   // key → dump
static std::string prtast(const bytes_view& _data);         // AST text
static bytes prtfmt(const bytes_view& _data);               // lexical-level reformat
static std::string prtinf(const bytes_view& _data);         // metadata (everything but the AST)
```

The keys `unpack` puts into `_out`: `"ast"` (AST dump), `"modules"` (module key → dump), `"info"` (a subset of the metadata), `"hint"` (the host payload in clear).

### 3.5 Diagnostic signals

| Signal | Parameter | Fires on |
|---|---|---|
| `on_cerr` | `const std::string&` | engine diagnostics (runtime error messages and the like) |
| `on_cmpl` | `const compile_error&` | a compile error (with row, col and byte offset) |
| `get_csys()` | `signal<uint_64, const std::string&>&` | pool-level events (module load/unload, link failures and the like); the callback gets (thread id, message) |

`get_csys()` returns a reference to a **pool-level shared signal** that outlives any single engine, so it is safe to keep across engines.

---

## 4. Data exchange

```cpp
alx::variant* load(const std::string& _name, bool _auto_create = false);   // read/write a root variable (usable outside a run too)
```

- A script value is an `alx::variant` (scalars, `bytes`, `std::string`, `varmap`/`varvec`/`varlst`, `anyptr`).
- On the script side integers are all `long long` semantics and floating point is `double`; a host reads one back under `to<T>()`'s **lossless conversion** rule, and a type that does not fit gives the default value.
- An object handed in from C++ travels in an `anyptr` (see `wrap<T>` in §11).

---

## 5. Extension functions `$name`

An extension function is the language superset a host injects: the script writes `$foo(args)`, the parser checks the table for it while compiling, and the walker calls the C++ function the host registered.

```cpp
bool set_extend(const std::string& _name, native_func _handler);   // native_func = void (*)(fwrap&)
void del_extend(const std::string& _name);
native_func get_extend(const std::string& _name) const;
bool fid_extend(const std::string& _name) const;

eng->set_extend("print", [](alx::script::fwrap& _fw) {
    std::string out;
    for (size_t i = 0; i < _fw.size(); ++i) out += _fw[i].to_string();
    printf("%s\n", out.c_str());
    _fw.freturn();      // no return value
});
```

- Name conflict: when the name is a script keyword or reserved word, `set_extend` returns `false` (the registration is refused).
- The `$` prefix is **lexical-level hard isolation** — a script author cannot define `$xxx` (an identifier carries no `$`), so no name the host registers can collide with a script user function.
- **Argument passing**: only a bare variable name travels by reference (it points at the variable's slot); an index, a member, a literal or an expression is evaluated into a copy first. A native that modifies in place (`$vec_push` and its kind) therefore gets a copy when handed `m["a"]`, and the change never lands back in the container — to modify in place, use a path assignment: `m["a"][null] = 7`.
- **Thread safety**: an extension function can be entered concurrently, so it has to be thread-safe by itself (no shared state, or the host's own lock).

### 5.1 The `fwrap` interface

| Member | Purpose |
|---|---|
| `size()` / `operator[](i)` | read an argument (`variant&`; a bare variable name aliases the script's variable, any other argument is a temporary; **dead once the call returns**; throws IndexError past the last argument) |
| `freturn(v)` / `freturn()` | set the return value |
| `raise(info, error_type)` | throw an exception the script can catch |
| `call(func, args)` | call a script function from the native (`func` may be a function name string) |
| `bind(name, func, area)` | register a native function into an area (what a link module uses) |
| `load/store/remove(name)` | reach the calling link instance's own data store |
| `object()` / `unwrap<T>()` | the C++ object bound to the current area |
| `config()` | read the engine configuration (the pipe pointers included) |

---

## 6. Static definitions `$name`

```cpp
bool set_define(const std::string& _name, const variant& _value);
void del_define(const std::string& _name);
alx::variant get_define(const std::string& _name) const;
bool fid_define(const std::string& _name) const;
```

A host constant read as `$name` (**no parentheses**). It shares the `$` namespace with the extension functions (the two are mutually exclusive by name; a conflicting registration returns `false`). The value is **folded into a literal while compiling**, which leaves the product self-contained with no run-time lookup; a misspelling is a compile error rather than a run-time empty read.

---

## 7. Version gate

```cpp
void set_etype(const std::string& _type);   // engine major version (a string)
const std::string& etype() const;
void set_vtype(uint_64 _ver);               // version of the supported extension set (a number)
uint_64 vtype() const;
```

- A compiled product carries the etype/vtype of its compile; the check on the way in is **always on** (it cannot be turned off). A plain engine that set neither skips it.
- `etype` matches exactly: a product with no tag, or one that differs from the engine's, gives `VersionError`.
- `vtype` is checked one way only: a product whose vtype is greater than the engine's is refused as too new. Removing or changing an extension is a breaking change and calls for bumping `etype`; only an added extension bumps `vtype`.

---

## 8. The hook `set_hook`

```cpp
void set_hook(hook_fn _fn, void* _ud, uint_64 _interval);
// hook_fn = bool (*)(hook_info&); returning false = interrupt the run
```

| `hook_event` | Fires on | `info` holds | Effect of returning `false` |
|---|---|---|---|
| `exec` | an instruction checkpoint | checkpoints fired so far | an interrupt (`InterruptedError`) |
| `import` | a module load about to happen | the resolved path | an **uncatchable interrupt** |
| `link` | a dynamic library load about to happen | the resolved path | an **uncatchable interrupt** |
| `trap` | a script called `trap(...)` | `{here: posmap, args?}` | an interrupt |
| `debug` | once per statement (needs `set_debug_enable(true)`) | empty — the position goes through `wkdt_pos()` | an interrupt |

- `_interval` is the checkpoint interval; `0` means **no `exec` events at all**. A callback may rewrite the interval that follows through `hook_info::freq`. `import`/`link`/`trap`/`debug` are **not limited by the interval**.
- The `debug` event arrives once per statement: a host that does not care should filter on `info.type` and return on the callback's first line. A hook installed with `debug_enable` on means one callback per statement (negligible in volume, but worth knowing).
- `hook_info::desc` is a writable string the engine owns; **write it only when refusing** (one exec keeps the last explanation written).
- `hook_info::hkdt` is the host data passed to `set_hook`, handed back unchanged on every event.

### 8.1 The execution view `wkdt`

A callback may observe the current execution state read-only (frames, entities, links, testing what a value is). **The engine does not check arguments**: a stale handle, an out-of-range index or an unknown key is UB, and all of it is valid during the callback only.

```cpp
uint_64 wkfm_size();                        // frames alive, 0 = at the top level
std::string wkfm_func(uint64 i);            // frame name ("eval" for an eval frame, "?" for a block or loop frame)
std::vector<std::string> wkfm_data_keys(uint64 i);
const variant* wkfm_data_cptr(uint64 i, const char* key);

const void* wken_of(const variant* v);      // value -> entity handle
const void* wken_root();
std::vector<std::string> wken_data_keys(const void* e);
const variant* wken_data_cptr(const void* e, const char* key);
std::string wken_file(const void* e);  std::string wken_name(const void* e);
const void* wken_pptr(const void* e);

const void* wklk_of(const variant* v);      // value -> link handle
std::vector<std::string> wklk_data_keys(const void* l);
const variant* wklk_data_cptr(const void* l, const char* key);
std::vector<std::string> wklk_area_funs(const void* l, const char* area);

bool wkis_ent(const variant* v);   bool wkis_link(const variant* v);
bool wkis_func(const variant* v);  bool wkis_area(const variant* v);

varmap wkdt_pos();                    // the statement being executed {row, col, ofst, file}; row = 0 = the position is unknown
std::list<varmap> wkdt_fpos();        // the statement chain {row, col, ofst, file, func}, innermost first and the top level last
                                      // func: the function name / "eval" / "?" for a nameless layer (a block or loop layer with a position is listed too)

const engine_config& wkconfig();
```

### 8.2 Row-level position `set_debug_enable`

```cpp
void set_debug_enable(bool _on);      // false by default; at the end of the vtable
```

- **One switch, two effects**: at parse time it decides whether position markers go in, at exec time whether positions are recorded and `debug` events fire. The semantics match `-g`: turning it off **suppresses**, it is not an error.
- **With it on**: a marker is inserted before every statement (a static annotation; the AST's shape is unchanged); at run time a frame keeps one slot only (**the top one is the current position**) and **no history**.
- When an uncaught error is brought out through `on_cerr`, each trace line is `[file] func:row:col` (the suffix is omitted when the row is unknown); the per-layer positions are what `wkdt_fpos()` returns.
- `row = 0` means **unknown** (and is normal): that is what an AST without markers gives (an `.axp` someone else compiled, or one from the shared cache).
- The granularity is the **statement**: no marker goes inside an expression, and the row at a call site is **the start of the statement the call sits in** (a stable anchor); a one-line `if (c) f();` is fully accurate, a multi-line bare body (the single-statement body of an `if`/`while`) falls back to the `if` line — the row of the carrying statement **itself** and of the **condition evaluation** is still accurate.
- With it off the behaviour is exactly the old build's with no position markers (instruction budget included: a marker consumes no checkpoint).

---

## 9. Host IO channels `set_pipe`

```cpp
using pipe_out = void (*)(const std::string* _out, void* _ud);   // engine -> host output
using pipe_in  = std::string (*)(void* _ud);                     // host -> engine input

void set_pipe(pipe_in _in, pipe_out _out, void* _in_ud = nullptr, void* _out_ud = nullptr);
```

The channels are per-engine; with none set (`nullptr`) the host's default is stdio. An extension function or a link module gets the two pointers from `fwrap::config()` and builds `$print` / `$input` and the like on them.

---

## 10. Interrupt

```cpp
bool running() const;        // true while an exec() is running (call() does not count)
void set_interrupt();        // ask for an interrupt (callable from any thread)
```

The interrupt is **cooperative**: once the request is raised it takes effect at the next checkpoint of the execution flow (the run comes back as `InterruptedError`). The checkpoints are governed by `set_hook`'s `_interval`, but **the flag is looked at on every checkpoint** — an interval of 0 only turns `exec` events off, `set_interrupt()` still works.

---

## 11. Link module development

A link module is a dynamic library that gives a script three layers: **a namespace, its functions, and object instances**.

### 11.1 Entry symbols

| Symbol | Signature | When |
|---|---|---|
| `alexis_script_load` | `void (*)(fwrap&)` | library level: once, at load |
| `alexis_script_unload` | `void (*)(fwrap&)` | library level: once, at unload |
| `alexis_script_create` | `void (*)(fwrap&)` | instance level: for every link instance created |
| `alexis_script_release` | `void (*)(fwrap&)` | instance level: when an instance is destroyed |

Constant names: `LINK_LOAD` / `LINK_UNLOAD` / `LINK_CREATE` / `LINK_RELEASE`; function type aliases `script_load` / `script_unload` / `script_create` / `script_release`.

```cpp
extern "C" ALXSCPT_API void alexis_script_load(alx::script::fwrap& _fw) {
    _fw.bind("abs", fn_abs, "mt");          // a static area: the function is registered directly
    _fw.bind("mkdir", fn_mkdir, "fs");
}
```

### 11.2 The two area kinds

**A static area** (registered at load time, no C++ object) — the script side is `b.mt.abs(-5)`, `b.fs.mkdir("d")`.

**A dynamic area instance** (a C++ object bound at run time):

```cpp
_fw.wrap<alx::file>(new alx::file(), "x", {
    {"read",  fn_read},
    {"write", fn_write},
    {"close", fn_close},
});
```

`wrap<T>` does two things: it stores `anyptr_ex<T>::make(_obj)` in the data slot keyed by the area name, and `bind`s each method into that area. When the script calls `b.x.read()`, the engine sets the area to `x` first, and `_fw.unwrap<alx::file>()` inside the native hands that C++ object back.

**`anyptr_ex<T>::make` splits on copyability**: a copyable type gets a copier; a `noncopyable` type (such as `alx::file`) takes the non-copyable version.

### 11.3 Callback conventions

```cpp
void fn_read(alx::script::fwrap& _fw) {
    alx::file* f = _fw.unwrap<alx::file>();
    if (nullptr == f) return _fw.raise("no file instance");
    _fw.freturn(alx::variant(f->read(...).to_string()));
}
```

- Arguments come from `_fw[i]`, the return value goes out through `_fw.freturn(...)`.
- On failure `_fw.raise(msg, error_type)` throws an exception the **script can catch**; a host exception works too — the engine wraps it into `NativeError`. The library's own allocation failures (`std::bad_alloc` / `std::length_error`) are not wrapped into `NativeError`: they are translated into the script-side `MemoryError` (a script `try` can catch that).
- An argument is a **pointer view** that is dead once `call()` returns; copy it if you need to keep it.

### 11.4 Lifecycle

| Stage | What happens |
|---|---|
| Creation | `wrap<T>()` in `create` (stores the anyptr and binds the methods) |
| Call | the engine sets the area → `object()` gives the anyptr → `unwrap<T>()` |
| Rebuild | `remove(area)` runs the anyptr's destructor (the C++ object dies) → `wrap<T>()` again |
| Instance deleted | the area's natives and data slot are cleared, the anyptr's destructor deletes the C++ object |
| Namespace deleted | only the area's natives are cleared; instances already created are untouched |
| Shutdown | **the link instances are destroyed first, `dlclose` after** — the anyptr's destructor lives in the dynamic library and has to be called before the unload |

A link instance's private data slot is invisible to the script (reading, writing and deleting all throw `TypeError`), so the script reaches it only indirectly, through the functions registered in the area.

---

## 12. Error handling

```cpp
struct script_exception { error_type type; std::string info; };
```

A script-side `throw`, a host-side `raise` and an engine-internal failure all land in `error_type`:

| Class | Values |
|---|---|
| Normal | `NoError` |
| Runtime | `RuntimeError`, `TypeError`, `NameError`, `ConvError`, `IndexError`, `KeyError`, `ArgError`, `NavError` |
| Arithmetic | `DivZeroError`, `DivOverflowError`, `OverflowError`, `ShiftError` |
| Resource | `StackError`, `ResourceError`, `MemoryError` |
| Compile | `ParseError` |
| Module | `ImportError`, `LinkError`, `VersionError` |
| Host | `NativeError` (a wrapped C++ exception), `InterruptedError` (an interrupt, uncatchable) |
| Fallback | `UnknownError` |

- A script-side `try/catch` catches only what is catchable; `InterruptedError`, and the interrupt an import/link hook returning `false` in §8 causes, both **pass through the script's catch**.
- For a string, use `error_type_name(type)`.
- A compile error goes through `compile_error` instead (`msg` / `loc.row` / `loc.col` / `loc.index`), reported through `on_cmpl`.
- An **uncaught error** is reported through `on_cerr`: the first line is `Uncaught: [type] msg`, and after it the folded call stack (one `[file] func:row:col` per line, a recursion folded into `(Nx)`). The row and col come from the position markers of §8.2 — **with the switch off or an AST without markers there is no row/col** (the trace keeps its old shape).
- **A module init failure**: `info` = `in module <path>: <type>: <msg>` plus that module's **internal** folded trace (its row numbers are readable).
