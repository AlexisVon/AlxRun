# alxscpt — Known Limits and Pitfalls

Boundary cases and pitfalls that lie **outside the behaviour contract but do not count as defects**. Each entry gives its trigger and its check.

## Compile time

- **The nesting depth cap defaults to 1024** (`engine_config::parse_depth`, 0 = unlimited): the static entry of `parser::parse` now defaults to 1024 too, where the old implementation passed 0 (unlimited) — source nested deeper than 1024 levels went from "parses" to a compile error. `eval` and dynamic `iload` parse under the host's configured `parse_depth`, the same rule the main source follows. Hitting the cap is **unrecoverable**: one `nesting too deep` is reported and no secondary syntax errors follow it (other errors before the cap are reported as usual).
- **`set_extend` and `set_define` share the `$` namespace**: when the two names collide, or the name is a keyword or reserved word, `set_extend` returns `false` (the registration fails) and nothing is overwritten. Removal is `del_extend` / `del_define`.
- **A `$name` static definition is a compile-time constant fold**: the `set_define` value is folded into a literal node while compiling, and changing the definition at run time does not affect an already compiled product.
- **Assignment targets are checked**: `1 = 2;`, `"abc" = 1;`, `$name = 1;` and the like were silently dropped by the old implementation (neither applied nor reported) and are compile errors now; a bad target on a dynamic `@()` path reports a run-time `TypeError`. `delete` targets have always had this check (`delete 1;` was an error to begin with).

## Run time

- **How coarse an exec checkpoint is**: with `hook_interval = 0` **no** exec event is produced at all; a leaf literal and the `LOAD` shortcut skip the checkpoint; a loop body adds one extra checkpoint per turn (which is why an empty `for(;;) {}` is still interruptible). `hook_info::freq`, and with it the checkpoint count, can be changed by the callback.
- **The interrupt is cooperative and takes effect at a checkpoint only**: both `engine::set_interrupt()` (safe across threads) and a hook callback returning `false` take effect at the next checkpoint; a host native is not interrupted while it runs, so a long-running extension function has to return in pieces of its own accord.
- **A `hook_event::import` / `link` callback returning `false` is an uncatchable interrupt** (`InterruptedError`) that a script-side `catch` cannot see; this differs from the old semantics, where returning `false` refused the import (`ImportError`).
- **The `hook_info::wkdt` read interfaces check nothing**: a stale handle, an out-of-range index or an unknown key is UB (the engine does not check), and they are guaranteed valid during the callback only. A value passed in is always treated as untrusted.
- **`hook_info::desc` is written only when refusing**: one exec keeps the last explanation string written.
- **The tail-call optimisation re-checks by name only**: `op_tcall` confirms once more that the name still resolves to the same `def`, and a name shadowed by a local or a parameter **falls back to an ordinary call** — that recursion layer is then no longer removed (the recursion depth grows as usual).
- **The `delete` boundaries**: `delete x.f` on a non-container value reports `TypeError`; a delete across a module boundary reports `NameError` (consistent with the read path).

## Host interface

- **Classify `engine::exec`'s return by `result::error`**: anything but `NoError` means the script ended in an exception, and what `value` holds is to be read according to `error_type` (do not go by whether `value` is empty alone).
- **`engine::call` acts on root-scope `def`s only**: it calls by name and passes the arguments as values; a name that does not resolve is an error, and no module path is resolved.
- **`compile`'s `_embed` is mutually exclusive with `link`/`env`**: with `_embed = true` a relative import resolves against the importing module's own directory only, an import that resolves to nothing is a compile error, and `link`/`env` are compile errors outright; the key of an embedded module is `"@" + sha16(absolute path + content)`.
- **`_hint` is a host payload the engine does not interpret**: it is stored as given (compressed when `_cmps` is set), and an empty one leaves no `hint` key in the product; `unpack`'s `"hint"` gets the clear text back.
- **`get_csys()` returns a pool-level shared signal**: it outlives any single engine (the engine forwards to the singleton resource pool), and the callback semantics are `(thread tag, message)`.
- **The configuration of `engine::create` can be changed at run time** (`set_max_stack` / `set_parse_depth` / `set_overflow_check` / `set_max_vecfill` / `set_search_paths` / `set_hook` / `set_pipe`), but a change affects later compiles and execs only.
