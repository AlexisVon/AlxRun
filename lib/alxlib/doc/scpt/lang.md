# Alexis Script Language Reference

<div style="page-break-after: always;"></div>

---

# 1. Getting started

```js
var name = $input();
$print("Hello, %1!\n", name);
```

Run:

```bash
Scpt file.axc             # execute a file
Scpt -e '$print("hi");'    # execute inline code (statements need a semicolon)
Scpt -i                    # interactive REPL
Scpt -c file.axc          # compile to .axp
Scpt -x file.axp          # execute the compiled product
```

**Comments**: `//` line comment, `/* */` block comment (not nestable).

```js
// line comment
var x;  // trailing comment
/*
   block comment
   multiple lines
*/
```

<div style="page-break-after: always;"></div>

---

# 2. Keywords + operator precedence

## 2.1 Full keyword table

19 base keywords + 5 constants + 13 builtin function names (reserved, not usable as identifiers):

```
var    def    if     else   while  for
break  continue  return  import  link  as
try    catch  throw  switch  case   default
delete
true   false  null   nan    inf
```

Builtin function names (reserved words; forbidden as variable / function / parameter / `as` alias):

```
int   float  string  bool   bytes  vec    map
lst   type   env    here   eval   trap
```

The `$xxx` extension mechanism: the `$` prefix namespace is a lexical-level hard isolation (`$` is a reserved symbol; identifiers do not contain `$`). The base engine is **zero extensions** — any `$xxx` call is a compile error. The host registers extension functions and predefined constants through `engine::set_extend(name, handler)` and `engine::set_define(name, value)`, giving scripts superset capabilities of the engine platform (IO, system interaction, compiler tools, etc.). An unregistered `$xxx` is a compile-time error; a registered one is a run-time table lookup. Extension functions are registered by the host at its option; they are not inherent to the language.

`map` is a keyword that doubles as the conversion function name, and is not usable as an identifier. `{ }` is always a code block; dictionaries use `map{}`. `[...]` can be used bare (defaults to vec) or spelled explicitly as `vec[...]` / `lst[...]`.

## 2.2 Operator precedence

| Level | Operators | Associativity |
|:----:|--------|:-----:|
| 1 | `,` | left |
| 2 | `= += -= *= /= %= <<= >>= &= \|= ^=` | right |
| 3 | `?:` | right |
| 4 | `\|\|` | left |
| 5 | `&&` | left |
| 6 | `\|` | left |
| 7 | `^` | left |
| 8 | `&` | left |
| 9 | `==` `!=` | left |
| 10 | `<` `>` `<=` `>=` | left |
| 11 | `<<` `>>` | left |
| 12 | `+` `-` | left |
| 13 | `*` `/` `%` | left |
| 14 | `++` `--` `!` `~` `+` `-` (unary) | right |
| 15 | `++` `--` `.` `[]` `()` (postfix) | left |
| 16 | `@` `delete` (prefix) | right |

<div style="page-break-after: always;"></div>

---

# 3. Basic types

| Type | Description |
|------|------|
| `int` | 64-bit signed integer |
| `float` | double-precision floating point |
| `bool` | boolean `true` / `false` |
| `string` | UTF-8 string |
| `bytes` | byte sequence (passive type, produced only by conversion functions) |
| `null` | null value |

## 3.1 int

64-bit signed integer, equivalent to C `long long`, range `[-9223372036854775808, 9223372036854775807]`.

```js
var x = 42, y = -1;
var hex = 0xFF;           // hexadecimal
var oct = 0o77;           // octal
var bin = 0b1010;         // binary
```

> `-9223372036854775808` (INT64_MIN) cannot be written directly (same as C/C++), the absolute-value part overflows. Use `-9223372036854775807 - 1` instead.

## 3.2 float

64-bit IEEE 754 double-precision floating point, equivalent to C `double`.

```js
var pi = 3.14;
var sci = 1e5;
var small = 1.5e-3;
```

## 3.3 bool

```js
var flag = true;
var off = false;
```

> bool is a distinct type: it cannot be compared with int (`true == 1` → TypeError) and cannot take part in bitwise operations (`true & true` → TypeError). Emphatically "bool is not int".

## 3.4 string

```js
var s = "hello";           // double quotes — " inside must be escaped as \"
var t = 'world';           // single quotes — ' inside must be escaped as \', the complementary " needs fewer escapes
var esc = "line1\nline2";  // escape sequences
var raw = `raw \n "a" 'b'`;  // raw string — no escapes, may span lines
```

The three quote kinds complement each other: `"..."` needs no escape for a `'` inside, `'...'` needs none for a `"` inside, and `` `...` `` escapes nothing at all.

**double/single** support escape sequences: `\t` `\r` `\n` `\\` `\'` `\"` `\0` `\xNN`

**raw string** (`` `...` ``): characters inside the backticks are kept verbatim, no escape processing, may span multiple lines. Suited to regexes, embedded JSON, and text with many backslashes.

## 3.5 null

```js
var x;                 // defaults to null
$print(null);           // "null"
```

## 3.6 nan / inf

```js
var x = nan;           // IEEE 754 NaN (Not a Number)
var y = inf;           // IEEE 754 +Infinity
$print(nan == nan);     // false — NaN is not equal to itself
$print(inf == inf);     // true
$print(type(nan));      // "float"
$print(type(inf));      // "float"
```

- `nan`: IEEE NaN, not equal to itself (`nan != nan` is true), contaminates by propagation (`nan + 1` = `nan`)
- `inf`: IEEE positive infinity, `inf + 1` = `inf`, `inf > any finite number` is true
- `float("inf")` / `float("+inf")` / `float("-inf")` / `float("nan")`: explicit string conversion allowed
- inf/nan produced by overflow, such as `float("1e999")` → ConvError
- `string(inf)` → `"inf"`, `string(nan)` → `"nan"`, `float`↔`string` closes the loop

## 3.7 bytes

Byte sequence, a **passive type** — no literal syntax, produced only by conversion functions, used for passing between host APIs.

```js
var b = bytes("hello");          // raw copy
var b2 = bytes("你好", "utf-8"); // UTF-8 encoding
var s = string(b);               // raw copy back to string
var s2 = string(b2, "utf-8");    // UTF-8 decoding
$print(b);                       // bytes(5)
```

- Only `==` / `!=` byte-wise comparison is supported; every other operator → `TypeError`
- `[]` indexing/assignment is not supported
- `type(b)` → `"bytes"`
- `bool(bytes())` → false (empty), `bool(bytes("x"))` → true (non-empty); a non-string argument → `TypeError`
- Supported encodings: `utf8` / `utf8bom` / `gbk` / `utf16` / `utf16le` / `utf16lebom` / `utf16be` / `utf16bebom` / `hex` / `base64`
- Case and separator words in an encoding name are arbitrary: the name is first folded to "alphanumerics only + lowercase" and then matched, so `UTF-8` / `utf8` / `utf_8` / `utf 8` are equivalent. Bare `utf16` = `utf16le` (no BOM)
- Unknown name → `ConvError`; the error reports the 3 nearest candidates plus every legal name

## 3.8 Truthiness rules

**Control flow** (`if`/`while`/`for`/`&&`/`||`/`!`/`?:`) accepts the following types:

| Type | truthy | falsy |
|------|--------|-------|
| `bool` | `true` | `false` |
| `int` | non-zero | `0` |
| `float` | non-zero | `0.0` |

`string`, `bytes`, `vec`, `map`, `lst`, `null` **cannot be used directly in control flow**; they need an explicit `bool()` conversion. The truthiness rules of `bool()`:

- non-empty string / bytes → true, empty → false
- non-empty vec / map / lst → true, empty container → false
- `null` → false

> Design intent: numeric `0`=falsy is a cross-language consensus, so it is supported directly. string/bytes truthiness is uniformly a non-emptiness test, with no semantic ambiguity. `cov_bool` never throws.

## 3.9 Literal syntax

| Type | Examples |
|------|------|
| integer | `123` `0xFF` `0o77` `0b1010` |
| float | `3.14` `1e5` `1.5e-3` |
| string | `"hello"` `'world'` `"esc\n"` `` `raw str` `` |
| boolean | `true` `false` |
| null value | `null` |
| special float | `nan` `inf` |

Identifier: `[a-zA-Z_][a-zA-Z0-9_]*`, may not collide with a keyword.

<div style="page-break-after: always;"></div>

---

# 4. Container types

## 4.1 vec

Dynamic array, `[...]` or `vec[...]` literal, contiguous memory.

```js
var v = [1, 2, 3];           // default vec
v = vec[1, 2, 3];            // explicit vec
v[0] = 99;
$print(v[-1]);       // back element → 3
```

## 4.2 lst

Linked list, `lst[...]` literal, O(n) traversal and read/write at any position (avoid random indexing on large lst), for-each traversal.

```js
var l = lst[1, 2, 3];        // lst literal
l = lst([1, 2, 3]);          // vec → lst conversion
$print(l[0]);               // 1
var x; for (x : l) { $print(x); }
```

## 4.3 map

Key-value pairs, `map{}` literal. `{ }` is always a code block. A key must be a string; a non-string throws TypeError (including `null` and numbers — it will not be silently turned into `""`).

```js
var d = map{"key": "value", "n": 1};
d["new"] = 42;       // insert
$print(d.key);        // dot access → "value"
$print(d["key"]);     // index access → "value"
$print(d[null]);      // 2 — key count (§6.3)
```

## 4.4 fill syntax

`vec[...]`, `lst[...]` and bare `[...]` all support fill syntax:

```js
[0:5]           // five 0s (default vec)
vec[:5]         // five nulls (explicit vec)
lst[1:3]        // [1, 1, 1] (lst)
[1,2,3:3]       // [1, 2, 3, 3, 3] — fill suffix
```

- `[val:N]` — N copies of val
- `[:N]` — N copies of null
- `[..., last:N]` — take the last element as val, pad out to N copies
- The run-time count is bounded by `max_vecfill`; exceeding it throws `ResourceError`

<div style="page-break-after: always;"></div>

---

# 5. Type conversion matrix

## 5.1 Conversion functions

> **null default value**: every conversion function returns the type's default value for a null input (`int→0`, `float→0.0`, `string→""`, `bool→false`, `bytes→empty`, `vec/lst→[]`, `map→{}`). The same holds for argument-less calls (`int()`, `float()`, etc.).

```js
int(v)      // → int, failure ConvError. null→0. String accepts 0x/0o/0b prefixes (same as literal syntax)
float(v)    // → float, failure ConvError. null→0.0. String accepts JSON-like format + "inf"/"+inf"/"-inf"/"nan"
            //   inf/nan produced by overflow is refused (e.g. "1e999"→inf → ConvError)
            //   an explicit "inf"/"nan" string is allowed — intent is distinguished from accident
string(v)   // → string, failure ConvError. null→"". Basic type conversion; double goes through %.6f, trailing zeros trimmed but one digit kept (3.14→"3.14", 2.0→"2.0", 0.1→"0.1")
            //   for |v| < 5e-7 %.6f would print "0", so %g is used instead (1e-7→"1e-07"); the precision caps at 6 decimals, use the overloads below for more
            //   the following overloads are formatting features, not basic conversions:
string(int, base)   // int→string conversion in a base. base: 2/8/10/16. 2→0b, 8→0o, 16→0x prefix. 10 has no prefix.
string(double, prec) // double→string precision control. prec=0→integer (%.0f), prec>0→N decimals (%.*f), prec<0→N significant digits (%.*g). Use this overload when the format must be controlled (these three paths do not trim trailing zeros, they keep a fixed format).
string(fmt, args...)  // multiple args: the first is the format string (%1 %2 ...), returns the formatted result. %% emits a literal %
bool(v)     // → bool, never throws
bytes(v)           // → bytes, raw copy (v must be a string); null→empty bytes
bytes(v, enc)      // → bytes, encoding conversion (v must be a string)
string(b)          // → string, raw copy (b must be bytes)
string(b, enc)     // → string, decoding conversion (b must be bytes)
// enc supported encodings (name folding rule in §3.7; case and separator words arbitrary):
//   "utf8"          UTF-8
//   "utf8bom"       UTF-8 + BOM
//   "gbk"           GBK
//   "utf16"         UTF-16 LE (no BOM, same as "utf16le")
//   "utf16le"       UTF-16 LE
//   "utf16lebom"    UTF-16 LE + BOM
//   "utf16be"       UTF-16 BE
//   "utf16bebom"    UTF-16 BE + BOM
//   "hex"           hex encode/decode
//   "base64"        base64 encode/decode
vec(v)      // → vec, failure ConvError; null→empty vec
map(v)      // → map, failure ConvError; null→empty map
lst(v)      // → lst, failure ConvError; null→empty lst
```

## 5.2 Conversion matrix

| Source \ Target | `int` | `float` | `bool` | `string` | `bytes` | `vec` | `map` | `lst` |
|-----------|-------|---------|--------|----------|---------|-------|-------|-------|
| **int** | itself | promote | ≠0 | to_string | ConvErr | [int] | ConvErr | [int] |
| **float** | truncate | itself | ≠0.0 | %.6f, trailing zeros trimmed, one digit kept | ConvErr | [float] | ConvErr | [float] |
| **bool** | 1/0 | 1.0/0.0 | itself | "true"/"false" | ConvErr | [bool] | ConvErr | [bool] |
| **string** | stoll | stod | non-empty=T | itself | raw/enc | per-char | ConvErr | per-char |
| **bytes** | ConvErr | ConvErr | non-empty=T | raw/enc | itself | ConvErr | ConvErr | ConvErr |
| **vec** | s1_recur | s1_recur | !empty() | int→UTF-8 / str→join | ConvErr | itself | ConvErr | copy |
| **map** | ConvErr | ConvErr | !empty() | ConvErr | ConvErr | ConvErr | itself | ConvErr |
| **lst** | s1_recur | s1_recur | !empty() | int→UTF-8 / str→join | ConvErr | copy | ConvErr | itself |
| **null** | 0 | 0.0 | false | "" | empty bytes | [] | {} | [] |

- **UTF-8 encode/decode**: string→vec/lst decodes the UTF-8 bytes into Unicode code point integers (`vec("你好")` → `[20320, 22909]`). vec/lst→string: all-int encodes each int as a code point into UTF-8 bytes (`string([20320, 22909])` → `"你好"`); all-string concatenates. An invalid UTF-8 sequence or an illegal code point → `ConvError`
- **bytes encode/decode**: `bytes(str)` raw copy, `bytes(str, enc)` encoding (hex/base64/GBK/UTF-8/UTF-16 and so on). `string(b)` raw copy, `string(b, enc)` decoding. The encoding-name folding rule is in §3.7; hex decoding is case-insensitive on input, encoding outputs lowercase
- **s1_recur**: when size==1, recursively convert the single element
- **cov_bool(string/bytes)**: non-empty = true, empty/null = false. Never throws
- **null default value**: `int(null)→0`, `float(null)→0.0`, `string(null)→""`, `bool(null)→false`, `bytes(null)→empty bytes`, `vec(null)→[]`, `map(null)→{}`, `lst(null)→[]`. Every argument-less conversion (`int()`, `float()` etc.) likewise returns the type's default value

## 5.3 type()

```js
type(42);        // "int"
type(3.14);      // "float"
type("hello");   // "string"
type(true);      // "bool"
type(bytes());   // "bytes"
type(null);      // "null"
type([1,2]);     // "vec"
type(lst[]);     // "lst"
type(map{});     // "map"
```

A value can also be an opaque **handle**, which only the engine and the host produce — a script holds one when it reads a module function (`import m; m.fn`), a namespace or area function (`b.mt.abs`), or an object a host extension handed over. `type()` names the engine's own handle kinds and leaves every other handle (a host object) under the bare type name:

| Value | `type()` |
|-------|----------|
| a callable handle (a module / link / area function) | `"func"` |
| an import / link / area entity handle (a shielded namespace, §11.3) | `"import"` / `"link"` / `"area"` |
| any other handle (an object a host handed over) | `"anyptr"` |
| an empty handle (one that holds no object) | `"null"` |

A value of a variant type the script layer does not model — only a host can pass one in: a 32-bit float, an unsigned integer, a `std::vector<T>` — reports `"unknown"`. A `null` value and an empty handle both answer `"null"`, so **empty reads as null, not as "unknown"**; `type()` never answers a null value itself.

> `type`, `int`, `float`, `string`, `bool`, `bytes`, `vec`, `map`, `lst`, `env`, `here`, `eval`, `trap` are all reserved words and cannot be used as a variable name, function name, parameter name or `as` alias. `$xxx` extension function names are registered by the host and are not reserved words.

<div style="page-break-after: always;"></div>

---

# 6. Variables and access

## 6.1 var declaration (optional initialiser supported)

```js
var x;               // declare x, initialised to null
var y = 42;          // declare and initialise
var a = 1, b, c = 3; // mixed: a=1, b=null, c=3
```

- `var` **forces a new variable in the current scope**; if the name already exists (variable/function/namespace) → `NameError`
- The initialiser expression is optional: no `=` → defaults to `null`; with `=` → the right-hand expression is evaluated and assigned
- In one `var` declaration each name independently decides whether it is initialised
- `for (var i = 0; ...)` and `for (var x : expr)` support the optional initialiser the same way

```js
var x;      // declaration (forced new, conflict checked), x = null
var y = 1;  // declare and initialise, equivalent to var y; y = 1; (but done in one operation)
```

### 6.1a typed var sugar (declaring with a type keyword)

The 8 type keywords can be used directly as declarations:

```js
int a;              // declare and initialise to the type default: 0
int a = 42;         // declare and initialise to the given value (type conversion)
float b = 3.14;     // float
string c = "hello"; // string
bool d = true;      // bool
bytes e;            // empty bytes
vec f;              // empty vec
map g;              // empty map
lst h;              // empty lst
```

**Type default values**:

| Type | Default |
|------|--------|
| `int` | `0` |
| `float` | `0.0` |
| `string` | `""` |
| `bool` | `false` |
| `bytes` | empty bytes |
| `vec` | `[]` (empty vec) |
| `map` | `{}` (empty map) |
| `lst` | `[]` (empty lst) |

**typed var in a for loop**:

```js
for (int i = 0; i < 10; ++i) {}    // C-style for loop
for (int i; i < 10; ++i) {}        // no initialiser (default 0)
for (int x : arr) {}               // for-each (default 0)
for (int i = 0, j = 1; ...) {}     // multiple declarations (same type)
```

- desugars to `var` + a type-conversion function call: `int a;` → `var a = int()`, `int a = 42;` → `var a = int(42)`
- a `var` declaration and a typed var declaration cannot be mixed: `var a, int b;` → syntax error
- the name-conflict rules are exactly those of `var`

- `x = 1` without `var`: assignment takes priority — walk the scope chain, found→assign; not found→`NameError`
- **Name conflict rules** (checked uniformly for `var`/`def`/`link`/`import`):

| Occupied first ↓ \ declared later → | `var x` | `def x` | `link "..." as x` | `x = 1` (assignment) |
|---------------------|---------|---------|-------------------|----------------|
| `var x` | NameError | NameError | NameError | OK (re-assignment) |
| `def x` | NameError | NameError | NameError | NameError |
| `link "..." as x` | NameError | NameError | NameError | NameError |
| `x = 1` (assignment) | NameError | NameError | NameError | OK (re-assignment) |

## 6.2 Assignment and compound assignment

`=` is an expression-level operator (precedence 2, right-associative); every assignment operator chains:

```js
x = 1;
a = b = 42;  // chained assignment: a = (b = 42)
a += b += 2; // compound assignment chains too: a += (b += 2)
x += 2;      // x = x + 2
x -= 3;      // x = x - 3
x *= 4;      // x = x * 4
x /= 2;      // x = x / 2
x %= 3;      // x = x % 3
x <<= 1;     // x = x << 1
x >>= 1;     // x = x >> 1
x &= 7;      // x = x & 7
x |= 8;      // x = x | 8
x ^= 3;      // x = x ^ 3
x += "!";    // x = x + "!"  (string concat)
v1 += v2;    // v1 = v1 + v2  (vec/lst concat)
m1 += m2;    // m1 = m1 + m2  (map merge, right overwrites)
```

**Assignment target**: must be a variable, a member access, an index expression or an `@()` dynamic path (the same rule as the `delete` target); any other shape (a literal, an arbitrary expression, a host constant folded out of `$name`) is a compile error. Slice targets are the exception — the grammar lets them through and the run time reports `TypeError` (see §6.4); whether an `@()` path is writable is likewise decided at **run time**, and an illegal one reports `TypeError`.

**Compound assignment semantics:**

- `x += f()` ≡ `x = x + f()`: the read of `x` happens **before** the call to `f()` (read first, then call; any modification `f()` makes to `x` is overwritten by the write-back).
- **Purification optimisation**: `a += b` / `a = a + b` (target a simple variable or a pure member chain, rhs a pure read expression) compiles to a single `O_ASS_*` opcode — the target is resolved once. Shapes with side effects (function calls and the like) stay expanded as `a = a op b`: the LHS index expression is evaluated twice, so in `v[expr()] += 1` the call `expr()` happens twice.
- **Compound assignment on `[null]` (the append index) is a compile error**: both reading and writing the append index carries a push side effect. Write it explicitly: `v[null] = 0; v[-1] += x;`.

## 6.3 [] indexing

| Index | Semantics | Applicable types |
|------|------|---------|
| `0` | front element, read+write | vec / lst / string |
| `-1` | back element, read+write | read: vec / lst / string; write: vec / lst |
| `null` | read→`size()` / write→push_back | read: vec / lst / string / map; write: vec / lst |
| `"key"` | dictionary key, read+write | map |

**Read / write / compound-assignment behaviour:**

| Type | Operation | `0` | `-1` | `null` | Any `[i]` |
|------|------|-----|------|--------|-----------|
| vec | read | ✅ | ✅ | ✅ size | ✅ |
| vec | write | ✅ | ✅ | ✅ push | ✅ |
| vec | compound assignment | ✅ | ✅ | ❌ compile error | ✅ |
| lst | read | ✅ front | ✅ back | ✅ size | ✅ O(n) |
| lst | write | ✅ overwrite front | ✅ overwrite back | ✅ push | ✅ O(n) |
| lst | compound assignment | ✅ | ✅ | ❌ compile error | ✅ O(n) |
| string | read | ✅ | ✅ back | ✅ size (byte count) | ✅ |
| map | read | — | — | ✅ size (key count) | ✅ string key only |

> **The `[null]` rule**: size semantics only at **the terminal of a chain + in a read position**, uniformly across the four containers; a scalar parent value (`n[null]`, n being int/bytes etc.) → `TypeError`. In a write position only vec/lst append, everything else is `TypeError` without exception. A non-terminal `a[null].b` → `IndexError`; `delete a[null]` → `ConvError`.
>
> string does not support indexed writes (any index → `TypeError`). A map key must be a string; a non-string (including `null` and numbers) → `TypeError` — it is not silently turned into `""`.

## 6.4 Slicing `[from, to]` / `[from, to, step]`

A comma-separated index is a slice operation, `[from, to)` half-open, returning a new container of the same type (read-only).

```js
var v = [0, 1, 2, 3, 4, 5];
v[1, 4];         // [1, 2, 3]     — indices 1,2,3
v[0, 6, 2];      // [0, 2, 4]     — step 2
v[5, 1, -2];     // [5, 3]        — reverse, step -2
v[-3, -1];       // [3, 4]        — negative indices
v[2, null];      // [2, 3, 4, 5]  — null = size
v[3, 3];         // []            — empty range

var l = lst[10, 20, 30, 40];
l[1, 3];         // [20, 30]      — an lst slice returns an lst

"hello world"[0, 5];  // "hello"   — string slice
"hello"[3, 0, -1];    // "lle"     — string reversed

v[1, 3] = 99;    // ❌ TypeError: slice is read-only
```

| Semantics | Description |
|------|------|
| `from` / `to` | integer expressions, may be negative, may be `null` (=size) |
| `step` | defaults to 1, a negative step traverses in reverse |
| out of bounds | IndexError (no clamp) |
| step=0 | ArgError |
| return type | a new container of the same type (vec/lst/string), an rvalue |

```js
var a = [10, 20, 30];
$print(a[0]);       // 10
$print(a[-1]);      // 30
$print(a[null]);    // 3 — size

a[null] = 99;      // push_back → [10, 20, 30, 99]

"hello"[null];     // 5 — string length (byte count)
"hello"[0];        // "h"
"hello"[-1];       // "o" — last character; [-2] is out of bounds (only -1 is back)
map{"k": 1}[null]; // 1 — map key count
var o = map{"arr": [1, 2], "s": "hi"};
o.arr[null];       // 2 — holds on a dot chain too
o.s[null];         // 2
o.s[-1];           // "i"
```

> `[]` inside a dot chain accepts **literal** indices only (integer/string/null), so `o.s[0]` / `o.s[-1]` work while `o.s[i]` is illegal.

> **Rule**: `null` has size semantics only at **the terminal + in a read position** (uniform across vec/lst/string/map). `a[null].b` (non-terminal) → `IndexError`. In a write position only vec/lst append, everything else → `TypeError`. `delete a[null]` → `ConvError` (a null index may not be deleted), use `delete a[-1]` to drop the last element.
> 
> `[]` inside a dot chain accepts literals only (integer/string/null). A variable index `a[i]` is illegal in a dot chain — when the index must be computed at run time, build the path string first and access it through `@(path)` reflection. A standalone index `a[i]` (not in a dot chain) is not subject to this restriction.




<div style="page-break-after: always;"></div>

---

# 7. Operators

## 7.1 Arithmetic

```js
a + b    // int/float addition / string concat / container concat (map merge)
a - b    // subtraction
a * b    // multiplication
a / b    // division (divide by zero → DivZeroError, INT64_MIN/-1 → DivOverflowError)
a % b    // modulo (int only, divide by zero → DivZeroError)
```

## 7.2 Comparison

```js
a == b   a != b
a < b    a > b    a <= b   a >= b
```

- int↔float are converted to a common type before comparing
- the same type compares directly (string by lexicographic order)
- `null`: only `==`/`!=` are supported (`null == null` → true, against anything else `==`/`!=` → false/true, no exception is thrown). It converts to each type's **default value** (`bool`→false, `int`/`float`→0, `string`→`""`, `bytes`→empty, container→empty); ordered comparison is not supported (`<` `>` `<=` `>=` throw `TypeError`)
- across types (including string↔number) comparison is impossible → `TypeError`

## 7.3 Logical

```js
a && b   // short-circuit: a falsy means b is not evaluated
a || b   // short-circuit: a truthy means b is not evaluated
!a       // logical not
```

## 7.4 Bitwise

```js
a & b    // bitwise and
a | b    // bitwise or
a ^ b    // bitwise xor
~a       // bitwise not
a << b   // left shift
a >> b   // right shift
```

`int` only. Non-int → `TypeError`. Shift out of range → `ShiftError` (requires `overflow_check`).

## 7.5 Concatenation and merge + +=

```js
// string concat
"hello " + "world"  // "hello world"
x += "!"            // x = x + "!"  (string concat)

// array / list concat
var a = [1, 2];
var b = [3, 4];
var c = a + b;      // [1, 2, 3, 4] (new vec)
a += b;             // a = [1, 2, 3, 4] (in-place)

var l1 = {1, 2};
var l2 = {3, 4};
l1 += l2;           // l1 = {1, 2, 3, 4}

// map merge
var m1 = {"a": 1};
var m2 = {"b": 2};
var m3 = m1 + m2;   // {"a": 1, "b": 2} (new map)
m1 += m2;           // m1 = {"a": 1, "b": 2}
// duplicate key: the right side overwrites
m1 += {"a": 99};    // m1 = {"a": 99, "b": 2}
```

`+` / `+=` are allowed only between containers of the same type, otherwise `TypeError`. In a map merge the value of a duplicate key is overwritten by the right-hand side (rhs).

## 7.6 Ternary condition ?:

```js
var max = a > b ? a : b;
```

Short-circuit: a truthy condition evaluates only conseq, otherwise only altern.

## 7.7 Increment / decrement

```js
++x    // prefix, returns the new value
--x    // prefix, returns the new value
x++    // postfix, returns the old value
x--    // postfix, returns the old value
```

int only. Non-int → `TypeError`. Overflow → `OverflowError` (requires `overflow_check`).

**Evaluation order**: binary operators always evaluate left→right, and `++`/`--` write back to the variable immediately, so the behaviour is determined.

```js
a = 0;
a++ + ++a  // → 2  (post returns 0,a=1; pre returns 2,a=2; 0+2)
++a + a++  // → 2  (pre returns 1,a=1; post returns 1,a=2; 1+1)
a++ + a++  // → 1  (post returns 0,a=1; post returns 1,a=2; 0+1)
++a + ++a  // → 3  (pre returns 1,a=1; pre returns 2,a=2; 1+2)
```

> Unlike C/C++, applying `++`/`--` to the same variable several times inside one expression is **not** undefined behaviour.

## 7.8 Type checking rules

| Category | Operators | Operand requirements | Error |
|------|--------|-----------|------|
| arithmetic | `+` `-` `*` | int/float (int preferred, falls back to float) | `ConvError` |
| arithmetic | `/` | as above + divide-by-zero check | `DivZeroError` |
| arithmetic | `%` | int only + divide-by-zero check | `DivZeroError` |
| comparison | all | null special case / same type / int↔float | `TypeError` |
| logical | `&&` `\|\|` `!` `?:` | bool / int / float (0 / 0.0 = falsy) | `TypeError` |
| bitwise | `&` `\|` `^` `~` | int only | `TypeError` |
| shift | `<<` `>>` | int only | `TypeError` |
| concatenation | `+` | string+string / vec+vec / lst+lst / map+map | `TypeError` |
| increment/decrement | `++` `--` | int only | `TypeError` |
| arithmetic compound | `+=` `-=` `*=` `/=` | as arithmetic (int/float); `+=` additionally supports vec/lst concat and map merge | as arithmetic |
| modulo compound | `%=` | int only + divide-by-zero check | `DivZeroError` `DivOverflowError` |
| shift compound | `<<=` `>>=` | int only | `TypeError` |
| bitwise compound | `&=` `\|=` `^=` | int only | `TypeError` |
| bytes comparison | `==` `!=` | bytes byte-wise comparison of the same type | `TypeError` |
| bytes otherwise | every other operator | — | `TypeError` |

<div style="page-break-after: always;"></div>

---

# 8. Statements

## 8.1 if / else

```js
if (x > 0) {
    $print("正\n");
} else if (x < 0) {
    $print("负\n");
} else {
    $print("零\n");
}
```

## 8.2 for

```js
var i;
for (i = 0; i < 10; ++i) {
    $print("%1\n", i);
}
for (var j = 0; j < 5; ++j) {  // var in init — j scoped to loop
    $print("%1\n", j);
}
for (;;) { break; }    // infinite loop (empty condition = true)
```

The loop body pushes a frame implicitly; a `var` declared inside the loop does not leak to the enclosing scope. After each iteration the body's temporary variables are cleared by a checkpoint reset.

## 8.3 for-each

```js
var v;
for (v : arr) { $print("%1\n", v); }           // assign to an existing variable
for (var v : arr) { $print("%1\n", v); }       // var declaration — v scoped to loop
for (kv : map) { $print("%1: %2\n", kv.key, kv.value); }  // map → {key, value}
```

Only vec / lst / map are iterable.

## 8.4 while

```js
while (n > 0) {
    n = n - 1;
}
```

## 8.5 switch

```js
switch (x) {
    case 1:
        $print("一\n");
        break;
    case 2:
        $print("二\n");
        break;
    default:
        $print("?\n");
}
```

There is no fall-through; every case carries its own break semantics. An explicit `break` inside a case is equivalent to the switch's return — it exits the switch only and does not propagate to an enclosing loop. When a switch is nested in a loop, a `break` inside a case does not exit the outer loop.

## 8.6 break / continue / return

```js
break;         // exit the innermost loop or switch
continue;      // jump to the next loop iteration
return;        // returns null
return 1;      // returns a value
```

> Inside a switch, `break` is equivalent to the switch's return; inside a loop it exits the innermost loop. When a switch is nested in a loop, neither penetrates the other.

## 8.7 try / catch / throw

```js
try {
    throw "error";
} catch (e) {
    $print("%1: %2\n", e.what, e.info);  // "RuntimeError: error"
}
```

- `throw expr` → `RuntimeError`, `e = map{"what": "RuntimeError", "info": expr}`. **The throw-container exception**: map/vec/lst cannot be converted to a string (`cov_string`), so `ConvError` is actually thrown — throw supports scalars/strings only
- an exception penetrates `def` boundaries and travels up the call stack
- an uncaught exception terminates the script, prints the call stack automatically, and does not crash the host
- **Declaration-time name conflicts**: a `var`/`def` name conflict inside a function body can be caught by `try/catch`, but try pushes an extra frame, so this is of little practical use. An `import/link as` name conflict is not catchable (the `as` grammar is confined to the top level and cannot be embedded in a `try` block). The recommended practice: `delete` proactively before declaring, to avoid the conflict.
- **Defensive `delete .xxx`**: `delete` is an expression (returns `bool`) and silently returns `false` for a name that does not exist. So `delete` first, then declare, and the declaration cannot conflict:

```js
delete .init;           // silent — deletes if present, and it does not matter if it is not
def init(x, y) { ... }  // always succeeds, the engine will not stop it for a name conflict
```

## 8.8 Exception type table

| Type | Source |
|------|------|
| `RuntimeError` | `throw expr` |
| `NameError` | undefined variable, var↔namespace conflict |
| `TypeError` | `.` on a non-map, `[i]` on a non-container, comparison of mismatched types, etc. |
| `ConvError` | a type conversion failed |
| `DivZeroError` | `/` `%` with a zero divisor |
| `DivOverflowError` | INT64_MIN / -1 |
| `OverflowError` | integer arithmetic overflow |
| `ShiftError` | shift count out of range |
| `IndexError` | vec/lst/string index out of bounds |
| `KeyError` | map key absent, or a middle segment of a dot chain absent |
| `ArgError` | a parameter with no default is missing |
| `ImportError` | module not found / circular import / a link library that will not dlopen, etc. |
| `LinkError` | a native call arrived outside any walk, i.e. from a load/unload entry point |
| `StackError` | call frames exceeded `max_stack` |
| `ResourceError` | a resource limit — a `max_vecfill` fill cap and other guard refusals |
| `MemoryError` | allocation failure (the `bad_alloc` / `length_error` thrown by `bytes`, translated by the engine into a script-catchable error) |
| `ParseError` | compile-time syntax/lexical error |
| `VersionError` | the .axp version tag does not match the engine |
| `NativeError` | a wrapped C++ native exception (resource errors are not in this class, see above) |
| `NavError` | reverse navigation past the entity tree depth |
| `InterruptedError` | the host hook forced an interrupt (uncatchable, a script's try/catch cannot swallow it; see engine interface semantics · execution interrupt hook) |

<div style="page-break-after: always;"></div>

---

# 9. Function definitions + extension functions

## 9.0 Static definitions $name (host-registered constants)

The host registers **compile-time constants** through `engine::set_define(name, variant)`. `$name` without parentheses reads one; the parse stage folds it straight into a literal node, so the `.axp` is self-contained with zero run-time table lookups:

```js
$print($PI);                  // 3.14159 — folded at compile time (the host must have registered $PI and $print)
var r = $cfg_max + 1;         // usable anywhere in an expression (same rules as a literal)
$cfg_list[0];                 // indexing works
$cfg_map["k"];                // map indexing works
var m = $cfg_map; m.k;        // dot member access needs a variable first ($name is a value, not a name)
```

Rules:

- **The `$` namespace is shared**: a static definition and an extension function exclude each other (registering the same name returns false) — a `$name` has exactly one meaning
- **Deterministic lookup**: `$name` without parentheses consults the definition table only, and a miss is a compile error (`undefined static definition`); if the name is an extension function it is still a compile error but the message is `extension function cannot be read as value` (the name exists, it just cannot be read as a value); with parentheses `$name()` consults the extension function table only
- **Value type unrestricted**: any variant type may be registered (containers included); serialisation is the host's responsibility
- **Reflection**: `@("$name")` without parentheses consults the definition table at run time (fast path); unregistered → NameError, registered as an extension → TypeError (callable but not readable, same as `@("$print")`)
- deletion (`del_define`) does not affect an already compiled .axp (the value was folded into the product)

The host may pre-register version constants, which a script can read directly:

```js
$vtype;   // extension support set (same as engine::vtype())
$etype;   // engine version string
```

## 9.1 Extension function examples (host registration required)

The following extension functions are **examples**; the base engine does not provide them. The host registers them as needed through `engine::set_extend(name, handler)`. Calling one that is not registered is a compile error.

**Assuming the host registered `$print` (output to stdout)**:

```js
$print("hello");                  // direct output
$print("%1 + %2 = %3", 1, 2, 3); // formatted (1-based)
$print(42);                       // int → "42"
$print(true);                     // bool → "true"
$print(null);                     // null → "null"
$print([1, 2]);                   // vec → []:2
$print(map{"x": 1});             // map → {}:1
```

- one argument: output directly; a container shows its type tag + size
- multiple arguments: the first is the format string, `%1` `%2` ... correspond to the arguments that follow
- `%%` emits a literal `%`
- if you need the formatted string returned (rather than output), use `string(fmt, args...)`

**Assuming the host registered `$tojs` (to JSON) and `$input` (read stdin)**:

```js
var v = [1, 2, [3, 4]];
$print("v: %1\n", $tojs(v));

var name = $input();
$print("Hello, %1!\n", name);
```

**Assuming the host registered `$datetime` (current local time)**:

```js
var t = $datetime();
$print("now: %1-%2-%3 %4:%5:%6", t.year, t.month, t.day, t.h, t.m, t.s);
```

The returned map's fields: `year`, `month` (1-12), `day` (1-31), `h` (0-23), `m` (0-59), `s` (0-59), `ms` (0-999), `us` (0-999999).

## 9.2 def

```js
def add(a, b = 1) {
    return a + b;
}
add(3);     // 4
add(3, 2);  // 5

// closure
def outer() {
    var x = 10;
    def inner() { return x; }
    return inner();
}
```

- a parameter with no default that is not supplied → `ArgError`
- **Argument evaluation semantics (values taken at the end, uniformly)**: the argument expressions are evaluated left to right (so the side-effect order is determined); the value of a variable argument is read **once every argument expression has been evaluated** — an assignment/rewrite of an earlier variable argument by a later argument expression is visible to the callee. In `f(x, x = 5)` the `x` is passed as `5`; in `f(count, next_count())` the `count` is passed as its post-call value. Script functions and link native functions behave identically
- tail call optimisation (TCO): when `return f(args)` has `f` as the current function's name, the current frame is reused automatically — no new frame, no depth limit (does not apply when the name is locally shadowed, see §9.2.1)
- indirect calls use the `@` prefix syntax, see §10
- `delete funcName` removes a function definition (within the current module)

### 9.2.1 Tail recursion

The engine supports TCO deterministically. The parser detects the `return <function name>(args)` pattern and emits a tail-call node; the walker rewrites the parameter values in place and re-executes the function body — no new frame, no growth of the call stack.

```
def countdown(n) {
    if (n <= 0) { return 0; }
    return countdown(n - 1);   // tail call — no new frame
}
countdown(50000);  // OK, no stack overflow

// tail-recursive fib
def fib_tail(n, a, b) {
    if (n <= 1) { return b; }
    return fib_tail(n - 1, b, a + b);  // tail call
}
```

Constraints:
- mutual-recursion TCO is not supported
- a tail call in a loop body is optimised as well: `return f(args)` inside `while`/`for` emits a tail-call node, frame reuse targets the function frame (nested block frames do not interfere), and the return exits the loop
- when the tail call sits inside a nested statement (`if`/`while` branch): the **function frame** is reused, not the top frame (it once bound to the top frame and looped forever)
- when the function name is locally shadowed (`var f` / a parameter `f`) it is not a self tail call: `return f(args)` resolves through the current binding — if it binds to something callable it is called, if not the error is `NameError` (the same as a statement-form `f(args)`). It once ignored the shadowing, re-entered the function body in place, and became a silent infinite loop

### 9.2.2 Argument expansion `expr[]`

In a function call, `expr[]` spreads a container into separate arguments. **Allowed only in the last argument position**.

- **vec / lst**: spread in order, filling position by position
- **map**: matched to parameter names by key name

```js
def add3(a, b, c) { return a + b + c; }

var v = [1, 2, 3];
add3(v[]);              // → add3(1, 2, 3)
add3([10, 20, 30][]);   // → add3(10, 20, 30) (the spread must be the last argument)

var lst_v = lst[4, 5, 6];
add3(lst_v[]);        // → add3(4, 5, 6)

// map[]: matches parameters by key name
def display(name, age) {
    $print("%1 is %2\n", name, age);
}
display(map{"age": 30, "name": "Alice"}[]);  // → name="Alice", age=30

// map[] together with defaults: override only the parameters you need
def greet(who, times) { return who + "x" + string(times); }
greet(map{"times": 3}[], "?");  // who="?" (positional), times=3 (map)
```

Constraints:
- `[]` must follow an argument expression, and must be the last argument (otherwise a parse error)
- more spread elements than parameters → the extra elements are silently ignored
- zero matching spread elements → `ArgError`
- spreading a non-vec/lst/map type → `TypeError`

## 9.3 here — source position constant

`here()` returns a **compile-time constant**: the source position of the call site itself, fixed at the parse stage, zero run-time cost. It is **the only compile-time feature left in the language layer** — `$xxx` extension functions are run-time table lookups, so a handler cannot obtain compile-time information such as the source position.

```js
var p = here();             // {row: 12, col: 9, ofst: 254, file: "/abs/path/script.axc"}
p["row"]                    // 1-based line number
p["col"]                    // 1-based column number
p["ofst"]                   // 0-based byte offset
p["file"]                   // absolute path of the source file; empty string when there is no file context
```

- function form only, `here()`; a bare `here` is a syntax error (reserved word)
- the position is that of the `here()` itself: written in a function body → the body's line; written at a call site → the call site's line
- `file` is a compile-time absolute path (`conver_std_path`); an imported module carries its own file path; with a directory context only (passing home_dir straight to `exec(src, dir)`) the directory path is returned
- `row`/`col` count characters (a tab indent counts as 1 character, not a visual column)
- typical uses: script-level asserts / logging / custom exceptions that carry their own call-site position

```js
def assert(cond, mesg, pos) {
    if (!(cond)) throw map{"what":"AssertError", "info":map{"mesg":mesg, "file":pos.file, "row":pos.row, "col":pos.col, "ofst":pos.ofst}};
}
assert(x > 10, "x too small: " + string(x), here());   // on failure it reports the assert call site
```

## 9.3b trap — explicit breakpoint instrumentation

`trap()` is breakpoint instrumentation: a call wakes the host's execution hook (`set_hook`), carrying the call-site position and optional argument data. **With no host hook it is a no-op** (one atomic null check, the script runs normally) — instrumented production code has zero impact. With no hook it **evaluates no expression at all** (zero side effects).

```js
trap();              // info = {here: position map} (the same {row, col, ofst, file} as here())
trap(args_expr);     // one argument: evaluated and passed as args, fires unconditionally
trap(cond_expr, args_expr);  // two arguments: a falsy cond_expr skips it (args_expr is not evaluated); truthy fires, info = {here, args: <args_expr>}
```

- function form only, `trap()` / `trap(args_expr)` / `trap(cond_expr, args_expr)`; a bare `trap` is a syntax error (reserved word)
- at most 2 arguments (more than 2 is a compile-time error); the `here` key is always present, the `args` key only when there is an argument
- with two arguments the first is the condition (`to_bool_strict`); falsy skips and does not evaluate the second expression
- every firing calls the host hook event (not limited by the checkpoint interval); a host hook returning false → the execution is interrupted (`InterruptedError`, uncatchable)
- typical uses: breakpoint debugging, conditional breakpoints, gathering run-time data on a hot path (combined with the host's `hook_info::wkdt` read-only execution view for frames/variables/module data, see Appendix B "Execution hook"; the engine-side mechanism is in `doc/scpt/design.md` §4.9b)

<div style="page-break-after: always;"></div>

---

## 10. Reflection

Indirect operations on variables/functions that **already exist**: read, write, delete, call. In `@name` / `@(expr)` the result of evaluating `name`/`expr` must be a `string`, used to determine the target name at run time.

In a declaration position (`var`/`def`/`import as`/`link as`) `@` is **forbidden**; the name must be fixed at compile time.

- **`@var`**: `var` must be an already defined variable whose value is a `string`. Its value is then used as the target name.
- **`@(expr)`**: the result of evaluating `expr` must be a `string`. Any run-time expression is accepted.
- The string must be a **legal identifier** (simple name) or a **path string** (containing `.` / `[N]`, such as `"a.b[0]"`).
- non-string → `TypeError`, target does not exist → `NameError`, reading/writing a namespace by mistake → `TypeError`.

### 10.1 Value reflection `@`

Indirect operations on variables/functions that **already exist**: read, write, delete, call. The implementation and the opcodes name these *indirect* (`O_ILOAD`, `O_ICALL`) — "reflection" is only this section's word for the same mechanism, so a grep of the sources will not find it.

```
var x = "y";
var y = 42;

// read / write / compound / inc-dec / delete
$print(@x);       // 42 (reads y)
@x = 5;          // writes y
@x += 1;         // y += 1
++@x;            // increment
delete @x;       // delete y

// call — the string may contain . for namespace navigation
var fn = "f";
@fn();           // calls f()
var fn2 = "math.abs";
@fn2(-5);        // call with a path → 5

// @(expr) accepts any expression
@("arr[0]") = 99;           // a path string containing an index
@(fn)();                    // call indirectly through an expression
```

A function name is just a string — to refer to a function indirectly, store its name as a string value and call through `@`:

```js
def add(a, b) { return a + b; }
def mul(a, b) { return a * b; }

var ops = map{"+": "add", "*": "mul"};   // a function name is just a string
var op = ops["+"];
$print(@op(3, 4));   // → 7  (calls add)

op = ops["*"];
$print(@op(3, 4));   // → 12 (calls mul)
```

**Extension functions**: a name string starting with `$` is a reference to an extension function, and `@` consults the extend table directly (fast path, no re-parsing of source). Extension functions must be registered by the host through `engine::set_extend()`; the base engine has no extensions at all:

```js
@("$print")("hello");        // calls $print (the host must have registered it)
var n = "$print";
@(n)("computed name");       // a string from an expression works the same
```

The constraints match those of extension functions: callable but not readable (a `@("$print")` without parentheses → TypeError); the name string is not checked at compile time, and an unregistered one → NameError at run time.

**Constraints**:

- a bare dot chain after `@` is not read as a name: `@a.b` evaluates the chain and uses its **value** as the target name, exactly like `@(a.b)`. Calling through a literal dot chain is the compile error (`@a.b(...)` → "@ requires a simple variable name, not a dot chain"); use a variable or `@(expr)`
- an indirect call does not trigger TCO (self-recursion cannot be determined at compile time)
- a link module reads and writes script variables through `nload` (see §12.4, the fwrap interface)
- `for-each`, `catch` and function parameters do not take `@` (they are fixed syntaxes that introduce new names)
- **avoid modifying the target name string through `@` inside a loop**: if the loop body changes the variable a `@i` depends on, the loop-variable binding becomes uncontrollable, much like a `goto` (the grammar does not forbid it, but it is strongly discouraged).

### 10.2 Code reflection eval

`eval(str)` executes a string as code — code-level reflection, complementing `@` (name-level): `@` operates on names that already exist, eval creates new code. Together they compose recursive reflection: code can be stored in a map, rewritten by a script, executed, and during execution distributed again through `@`.

```js
eval("1+1;");                    // 2 — the last statement's value
eval("return 42;");              // 42 — return short-circuits
eval("var t = 10; t * 2;");      // 20 — several statements, returns the last value
eval("");                        // null
```

- **function form**, `eval` is a reserved word: `eval(str)` or `eval(str, map)`
- `str` is any expression; its run-time value must be a string
- the optional second argument `map` supplies **named parameters**: the key names become names usable inside the code

```js
eval("a + b;", map{"a": 1, "b": 2});    // 3
var a = 100;
eval("a;", map{"a": 1});                // 1 — the parameter shadows the outer same-named variable
```

- **Scope**: the code can read variables of the current scope (closure semantics) and can write through to outer variables of the same name; `var`/`def` declarations are valid only inside this code block and disappear when it finishes

```js
var x = 10;
eval("x + 1;");                 // 11 — reads the outer variable
eval("x = 99; x;");             // 99 — writes through to the outer variable, x becomes 99
eval("def g(a) { return a; } g(1);");  // 1 — the def is valid only for this execution
```

- **Multi-layer eval**: the code may assemble a new string and execute that in turn (recursive reflection)

```js
var s = "1+1;";
eval("return eval(s) * 2;");    // 4
var gen = "var t = \"3*3;\"; return eval(t) + 1;";
eval(gen);                      // 10 — generated code generates code
```

- **Errors**: a syntax error in the string → `ParseError`, catchable by try/catch, with the message `eval: syntax error at <row:col>: <msg>` (the position is inside the eval string, and only the first error is reported, no cascade); a run-time error inside the code follows the existing exception path
- **Combined with `@`**: inside an eval string `@` can distribute dynamically (`eval("return @(fn)(21);")`)
- **TCO**: self-recursion of a named function defined inside eval code is tail-recursive as usual; eval itself does not take part in tail recursion
- **`here()`**: inside an eval string it returns the position within the string (`file` is empty); pass the call-site position explicitly when you need it: `eval(str, {"here": here()})`

<div style="page-break-after: always;"></div>

---

# 11. import + scope + env

## 11.1 import

```js
import "math.axc" as math;    // source
import "math.axp" as math;    // compiled product
```

- `as NAME` is mandatory; omitting it is a syntax error
- top level only
- alias conflict (a var/def/link/import of the same name already in this module) → `NameError`
- importing the same module several times in one module (under different aliases) is legal: the defs are shared, the data is isolated
- the body runs on the first execution; later ones skip execution and copy the initial state directly
- an optional compile-time embed (`compile(..., _embed=true)`): imports are resolved and inlined recursively at parse time (absolute paths looked up directly / the `"."` rule = the module's own directory), key = `"@"+sha256(absolute path+content)[0:16]`; in embed mode link/env() are compile errors, an import that cannot be resolved is a compile error, a circular import is a compile error, and an imported .axp must have an empty unresolved-dependency list. The product is self-contained
- `delete aliasName` removes an import namespace; it may be imported again afterwards
- across modules a variable may only be read and written, not created or deleted
- **multiple engines in parallel**: several engines in one process (in parallel across threads) may import the same module at once — parsing is shared, instances are isolated; once every module instance has been released, importing again re-parses (hot reload, so new content on disk takes effect)

**import as a class pattern**: `as` is the instantiation, the module is the class. The module body runs once as a template, and every `as` gets an independent module instance. You can write an `init()` method and any other member methods:

```js
// point.axc
var px = 0;            // a module body variable = an instance member (kept in the module's persistent data)
var py = 0;

def init(x, y) {       // the "constructor" — assignments go to the module variables declared with var
    px = x;
    py = y;
}
def len() {
    return px * px + py * py;
}
```

```js
// use
import "point.axc" as p1;
import "point.axc" as p2;
p1.init(3, 4);         // px=3, py=4 in module p1
p2.init(5, 6);         // px=5, py=6 in module p2
$print(p1.len());       // 25
$print(p2.len());       // 61 — the data is isolated
```

## 11.2 Namespace access

```js
import "math.axc" as math;
math.add(1, 2);       // calls math's add

b.ex.slice(v, 0, 2);
```

## 11.3 Scope rules

- **Variable lookup**: the current frame's var_map → parent frames of the same module → the entity's persistent m_map. At a module boundary (the frame's ent changes) the frame search stops and only the current entity's m_map is used as a fallback
- **Module isolation**: different import/link aliases create independent modules whose variables do not connect
- **Namespace shielding**: an import/link `as` alias is an opaque namespace, reachable only through `.` for its members (`alias.func()`, `alias.x`); it cannot be read or copied as a value (`var x = alias` → `TypeError`). link additionally shields dot writes (`alias.x = v` → `TypeError`); dot writes on an import are allowed (they go into the import entity's `m_store`)
- **Frames**: only block / function call / for / while / for-each push a new frame implicitly, and popping them rolls back (scope_frame RAII). A module's top level has no frame — `var` writes straight into the entity's persistent m_map
- **dot chains across modules**: a positional dot (`::` / `..`) consults the target entity only and cannot penetrate function frames (module value-semantics isolation)

## 11.4 env

```js
env([".", "/usr/lib/alexis"]);   // set the current module's search paths
var paths = ["./mods"];
env(paths);                       // a variable works too
```

Path resolution (run time): an absolute path is looked up directly → the current module's env list in order (**before any `env()` call the list holds the module's own directory as an absolute path** — the root module's is its home directory, and a module leads with its own directory so nested imports resolve next to it; calling `env()` replaces the list; `env([])` empties it and `env()` with no argument does nothing) → the list the host set through `engine::set_search_paths` → fallback to CWD (CLI/cache mode with no script path). The module's own directory is the only implicit base; there is no other fallback. Paths are not canonicalized (no symlink resolution, no normalization of `..`/`.`), so a host that wants sandbox isolation should restrict the search paths.

## 11.5 embed

`compile(data, home_dir, cmps, _embed=true)` compiles a **pure .axc project** (the main script plus its tree of relative imports) into **a single self-contained .axp**: every reachable module is resolved recursively at compile time and inlined (the module ASTs go into the inner `modules` table, import nodes are rewritten to `@key`, key = the first 16 characters of `sha256(absolute path+content)`). The product can be copied to any machine and `exec`'d directly, with zero file dependencies and zero path resolution at run time.

Usage constraints (in embed mode violating one is a compile error):

- **import paths**: absolute, or relative to **the directory of the importing module file** (the `"."` rule). env is not consulted, search paths are not consulted, there is no CWD fallback — if it cannot be resolved it is a compile error
- **link is forbidden** (a dlopen'd shared library cannot be packed)
- **env() is forbidden** (embedding has nothing to do with env; env is a run-time mechanism)
- **circular imports are forbidden** (A→B→A is a compile error; the same module reached by several paths inside the DAG is legal and is embedded once)
- **importing an already compiled .axp**: its `imports`/`links` dependency lists must be empty (otherwise it is an error) — only a self-contained .axp produced by an embed compile can itself be embedded
- an **old .axc** whose module top level calls env()/link cannot be embed-compiled and has to be reworked first

```js
// project: main.axc + mods/a.axc + mods/b.axc (a imports b)
// compile (C++ host):
//   bytes bin = eng->compile(bytes_view(main_src), main_dir, /*cmps=*/true, /*embed=*/true);
// product: bin is self-contained; written to disk as .axp it runs on any machine (no source tree needed)
```

<div style="page-break-after: always;"></div>

---

# 12. link + library authoring

## 12.1 link syntax

```js
link "mylib" as mylib;     // dlopen → dlsym("alexis_script_load"), the engine guesses the platform suffix
```

- `as NAME` is mandatory, top level only
- alias conflict (a var/def/link/import of the same name already in this module) → `NameError`
- `delete aliasName` removes the link instance (refcount-- → dlclose at zero); it may be linked again afterwards
- **multiple engines in parallel**: several engines (in parallel across threads) may link the same library at once — the dlopen handle is shared, the template/instance is per-engine and independent; once every instance has been released, linking again re-dlopens (hot reload)
- the four link ABI functions (all `void (*)(fwrap&)`, all optional):
  - `alexis_script_load` — whole-link initialisation (once after dlopen, builds the template)
  - `alexis_script_create` — instance initialisation (a fresh area per instance; if absent the template is copied)
  - `alexis_script_release` — instance destruction (releases the instance's resources)
  - `alexis_script_unload` — whole-link destruction (before dlclose)
  - the lifecycle is a determined sequence: `load → create×N → release×N → unload`
- inside a callback, register functions by calling `args.bind("name", callback)`
- linking the same library several times (under different aliases) gives independent instances that share the module, with a refcount managing the lifetime
- **link has its own data block**: C++ bind functions can operate on it through `args.load/store/remove`
- **the script layer may not touch a link's data block**: `mylib.x` reaches only the functions the link registered

## 12.2 Built-in modules

| .so | Functions |
|-----|------|
| `base.so` | **mt**: `abs` `sqrt` `pow` `min` `max` `ceil` `floor` `round` `sin` `cos` `tan` `log` `log2` `log10` `exp` `pi` `e`<br>**ex**: `find` `keys` `values` `has` `size` (read-only); `insert` `remove` `sort` `reverse` `merge` (modify in place, arguments are passed as views)<br>**st**: `trim` `upper` `lower` `replace` `startswith` `endswith` `repeat` `split` `join`<br>**sys**: `exec` `sleep`<br>**fs**: `mkdir` `rmfile` `rmdir` `mvfile` (static); `open(name, path, mode)` `info(name, path)` (factories, they create an area instance) |
|  `test_store.so` | `set` `get` `has` `del` `read_parent` `call_script` `raise_error` `throw_native` `write_arg` (writable view) — area: `sub.echo()` |

> **Merge principle**: pure-function modules (math, algo) can be merged into a single .so. A stateful module can be mixed into the same .so through the area object-binding pattern (`wrap<T>/unwrap<T>`) — the C++ object's lifetime is managed by `anyptr`, so a separate .so is no longer needed.

### 12.2.1 Sub-scope (area)

area has two meanings:

**1. Static function grouping** — `bind(_name, _func, _area)` registers a function into a named sub-scope:

```cpp
args.bind("echo", fn_echo, "sub");   // area "sub"
args.bind("abs", fn_abs, "mt");      // area "mt"
```

```js
ts.sub.echo(42);
b.mt.abs(-5);
```

**2. Dynamic area instances** — `wrap<T>()` binds a C++ object plus methods to an area at run time, giving class-instance isolation:

```cpp
// inside a factory function:
args.wrap<alx::file>(f, name, {
    {"read", fn_read},
    {"write", fn_write},
    {"close", fn_close},
});
```

Inside `wrap<T>`: `anyptr_ex<T>::make(f)` → `store(name, anyptr)` → `bind(method, func, name)`. The area name = the data key.

```js
// script side
link "base" as b;
b.fs.open("x", "/path", "r");  // creates area "x", binds a file object
b.x.read();                     // area "x" → fn_read → unwrap<file>()
b.x.close();
b.fs.info("y", "/path");       // creates area "y", binds a file_info object
b.y.name();
```

- an area nests one level only (`link.area.func`)
- an area instance's methods obtain the C++ object automatically through `unwrap<T>()`, with no manual pointer handling
- re-init: call `args.remove(name)` before `wrap` to clear the old object, otherwise a repeated area name throws
- teardown: the `anyptr` destructor deletes the C++ object automatically, so no `alexis_script_release` is needed
- reflection `@` needs `@(string)` to call an area function indirectly
- **Module namespace shielding**: an `import`/`link` `as` alias is an opaque namespace — a bare read (`var x = alias`) or a compound assignment (`alias += 1`) throws `TypeError`. link additionally shields dot writes (`alias.x = v` → `TypeError`); dot writes on import are allowed. A link's internal data is completely invisible to a script — unreadable, unwritable, undeletable. Native code can reach it through `nload`; the restriction applies to the script layer only
- `delete` rules:
  - `delete link.area_instance` → ✅ allowed (destroys the area instance, runs the anyptr destructor)
  - `delete link.area_namespace` → ✅ allowed (removes the whole area registration)
  - `delete link.func` → ❌ TypeError (a callable is not an object, deletion forbidden)
  - `delete link.area.func` → ❌ TypeError (a callable is not an object, deletion forbidden)
  - `delete link.data_var` → ❌ TypeError (link data is shielded from scripts)
  - reading `link.data_var` / writing `link.data_var = val` → ❌ TypeError (link data is shielded from scripts)
- `@` reflection behaves exactly like the direct syntax: `delete @("b.x")` ≡ `delete b.x`

## 12.3 Writing a new link library

```cpp
// link/mystr.cpp
#include "ascript.h"
#include "astring.h"
using namespace alx::script;

static void fn_upper(fwrap& args) {
    if (args.size() == 0 || !args[0].is<std::string>()) { args.freturn(); return; }
    std::string s = args[0].to<std::string>();
    for (auto& c : s) c = static_cast<char>(toupper(c));
    args.freturn(s);
}

extern "C" void alexis_script_load(fwrap& args) {
    args.bind("upper", fn_upper);
}
```

```js
// in a script
link "str" as str;
str.upper("hi");  // "HI"
```

## 12.4 The fwrap interface

| Method | Description |
|------|------|
| `args.size()` | argument count |
| `args[i].to<T>()` | the i-th argument — a **writable view** (a variable argument writes back to the caller; out of range throws `IndexError`; invalidated by `fw.call()` invoking back into the script, after which it must not be used) |
| `args.freturn(v)` | set the return value; with no argument it returns null |
| `args.load("x")` | read a link private variable → `variant*` (simple name) |
| `args.store("y", val)` | write a link private variable (`const&` copies; `&&` moves — a non-copyable object is moved to keep it alive) |
| `args.remove("z")` | delete a link private variable |
| `args.nload("key")` | unified dot-chain access → `variant*` (copy-free, readable and writable; a miss returns `nullptr`) |
| `args.call(func, args_vec)` | call: `string` → nload resolution → call_able → execute; `anyptr<call_able>` → execute directly |
| `args.bind("name", func)` | register a native function (optional third argument `_area`) |
| `args.bind("name", func, "area")` | register into a sub-scope |
| `args.raise(val[, type])` | throw a script exception |
| `args.object()` | the `anyptr*` stored for the current area |
| `args.unwrap<T>()` | `anyptr_ex<T>::as(*object())` — type-safe access to the C++ object |
| `args.wrap<T>(ptr, "area", {{"m1", fn1}, ...})` | store the obj + register methods into the area |

> **The `nload` path**: a simple name → data_store (`O_ILOAD` → `resolve_dot`).

> **What extend and link can use**: fwrap is designed for link modules as a whole (`bind`/`area`/`object`/`wrap`/`load`/`store` are all link concepts). An extension function handler borrows the same interface but has **no private domain** — its `m_store` points at the current module's root store. Inside an extend the data plane must **use `nload` only** (reflection semantics, with the full scope/frame machinery, and there is always an active walker during a call); `load`/`store`/`remove` consult the root store directly and bypass scope and `var` declaration checks — they cannot see frame variables, and a write goes straight into the module's root table, so this is misuse and is forbidden. A link's load/unload callbacks (no active walker, `nload` unusable) are the legitimate setting for `load`/`store`.

## 12.5 The C++ object wrapping pattern

`anyptr` + `wrap<T>/unwrap<T>` binds a C++ object to an area instance for class isolation. The engine manages the lifetime automatically; no manual `alexis_script_release` is needed.

### 12.5.1 Basic pattern

```cpp
// link/base/filesys.cpp — wrapping alx::file
#include "afile.h"
#include "ascript.h"
using namespace alx::script;

// method function — obtains the C++ object through unwrap<T>()
static void fn_read(fwrap& args) {
    alx::file* f = args.unwrap<alx::file>();
    if (!f) args.raise("file: not initialized");
    bytes b = f->read(0, uint_64(-1));
    args.freturn(std::string((const char*)b.data(), b.size()));
}

static void fn_write(fwrap& args) {
    alx::file* f = args.unwrap<alx::file>();
    if (!f) args.raise("file: not initialized");
    const std::string& data = args[0].to<std::string>();
    args.freturn(f->write(data.data(), data.size()));
}

// factory function — creates an area instance at run time
static void fn_open(fwrap& args) {
    std::string name = args[0].to<std::string>();
    std::string path = args[1].to<std::string>();

    alx::file* f = new alx::file();
    f->open(file_info(path), alx::file::READ);

    args.remove(name);  // clear the old object on re-init
    args.wrap<alx::file>(f, name, {
        {"read", fn_read},
        {"write", fn_write},
        {"close", fn_close},
    });
    args.freturn(true);
}

// load: register the factory function only, store no object
extern "C" void alexis_script_load(fwrap& args) {
    args.bind("open", fn_open, "fs");
}
// no alexis_script_release needed — the anyptr destructor deletes automatically
```

### 12.5.2 anyptr type erasure

`anyptr` (`aanyptr.h`): holds a `void*` + a deleter + a copier, and calls the deleter automatically on destruction.

`anyptr_ex<T>` provides type-safe access:
- `make(T*)` → `anyptr` (automatic SFINAE: a copyable type gets a copier, a `noncopyable` subclass does not)
- `as(const anyptr&)` → `T*` (checks the type through the deleter's address, returns `nullptr` on failure)

### 12.5.3 Non-copyable types

`alx::file` derives from `noncopyable`, and `anyptr_ex<T>::make()` picks the copier-less version automatically. `store(&&)` uses move semantics to avoid a copy losing the object pointer.

### 12.5.4 Script side

```js
link "base" as b;
b.fs.open("f1", "data.txt", "r");  // creates area "f1"
b.fs.open("f2", "log.txt", "w");   // creates area "f2" (a different instance, isolated data)
b.f1.read();                        // reads data.txt
b.f2.write("hello");               // writes log.txt
b.f1.close();
b.f2.close();
```
> **The same pattern for import**: `def init(...)` / `def uninit()` are the conventional function names, and `uninit` is never called automatically — an import object is reclaimed wholesale by the engine, so no manual release is needed.

<div style="page-break-after: always;"></div>

---

# 13. dot chains

A dot chain is the unified access model. An ordinary dot (`a.b.c`) is the subset whose nav segment is empty. A positional dot (`..a.b.(c.d)`) is split in two by the `.(...)` bridge: the nav segment walks the entity tree, and inside the bridge data access happens.

Chapter 6 covered `var` declarations, assignment and `[]` indexing. Chapters 11–12 covered `import` and `link` — `import` creates a sub-**module**, `link` mounts a native library under a module. The root script is itself a module too. Modules form a tree:

## 13.1 The module tree

```
[root script]
 ├── import "math.axc" as math    // sub-module
 │    └── import "util.axc" as util   // grandchild module
 ├── link "base" as base       // link library (not a module, a namespace under the module)
 └── import "geom.axc" as geom    // another sub-module
```

Each **module** (the entity an import creates) owns an independent scope (variables, functions, sub-modules, link references). A link is mounted under a module and provides native functions and area namespaces, but it is not a module — it cannot contain sub-modules or a variable scope. The root module is the destination of `::`, the parent module that of `..`.

> In the implementation a module is called an **entity** (a module instance). This text uses "module" throughout.

## 13.2 dot chains

`a.b.c` is flattened into a chain at the expression level. The lookup order for each key segment is:

1. **Variable**: search up the scope chain, stopping at the current module's root frame
2. **Sub-module**: a direct `import` / `link` child of the current module
3. **link namespace**: cannot be read as a variable → `TypeError`

### Data access

If a key resolves to a variable and that variable is a container (map/vec/lst), the following keys go through data access:

```js
var d = map{"a": map{"b": 42}};
d.a.b              // 42 — "d" is a variable (map), the following keys take data
d.a.b = 99;        // write
d.a.newKey = 88;   // the chain's last key does not exist → created
```

- read: the chain's last key absent → returns `null`
- write: only the **last** key of the chain may be absent → created automatically; a missing segment anywhere in the middle → `KeyError`
- **Named-element constraint**: an element in the middle of a chain must be **named** — a variable or a navigation token (`.`/`..`/`::`). A computed value (a temporary such as a function call result) does not support `.` cascading; use `[]` indexing:

```js
def f() { return map{"a": 42}; }
f().a              // not supported
f()["a"]           // 42 — a computed value takes [] for access
var x = f();
x.a                // 42 — store it in a variable first, then the variable chain works
```

### Module navigation

If a key finds no same-named variable in the current module's scope chain, the sub-modules are checked:

```js
import "math.axc" as math;
math.add(1, 2);    // "math" is not a variable → enter sub-module math → call its function add
```

- **Variable shadowing**: if the current frame has a `var math`, the variable wins and the sub-module `math` is unreachable
- a dot chain can only **drill down** into a direct sub-module; it cannot go up or jump to the root — those use a positional dot (§13.3)

### Function calls

`a.b(args)` looks for the function `a` in the current module → failing that, it checks the link module → `NameError`. `a.b.f(args)` first locates `a.b` (a module or a variable), then looks for the function `f` in the target module.

### Mixed dot+index

`.` and `[]` nest arbitrarily:

```js
var obj = map{"arr": [10, 20]};
obj.arr[0]          // dot → index: take obj's key "arr" (a vec), then [0] to get 10

var m = map{"a": map{"b": [1, 2, 3]}};
m.a.b[1]            // map → map → vec[1] → 2
m.a.b[null] = 99;   // dot + null write → push_back (vec/lst only, §6.3)
m.a.b[null];        // read → 4 (size; string/map hold the same way)
```

## 13.3 Positional dot

A dot chain prefixed with `..` / `::` / `.` states its starting entity explicitly. **The nav segment only walks the entity tree** — variables/functions/link/area are reachable only through the `.(...)` bridge. Without a bridge a prefixed dot chain just returns the entity itself (meaningless at the script layer, it returns void). Before the bridge is the nav segment (entity navigation), inside the bridge the access segment — a dot chain lookup starting from that entity.

| Prefix | Meaning | Example |
|------|------|------|
| `.`  | lock onto the current entity | `.a.b` (current → sub-entity a → variable b) |
| `..` | the parent entity (stackable, usable mid-chain) | `..x`, `a..b`, `....x` |
| `::` | the root entity (chain start only) | `::x`, `::a.b.(f)()` |

```
.x               → current entity → x
.a.b             → current → sub-entity a → sub-entity b or variable b
.a.(f)(args)     → current → a → bridged function call (a bare `.(` with no prefix = parse error)

..x              → parent entity → x
..(f)(args)      → parent entity function call
....x            → two levels up → x

::x              → root entity → x
::a.b.(f)(args)  → root → sub a → sub b → bridge → function f
```

The root entity's base = the script's directory (with `exec(file_path)` the home_dir is the script's directory; `::`/`..` root navigation does not depend on CWD).

### `..` mid-chain

```
a..b             → enter sub-module a → back to the parent level → find sibling module b
a....b           → enter a → two levels up → find b
```

### The `.(...)` bridge

A positional dot only walks the entity tree. The nav segment stops once it lands on an entity. To continue from that entity looking for a sub-module, function or variable, use `.(...)` to **bridge** and append another dot chain. The part after the bridge is equivalent to a dot chain starting at that entity.

**A bridge must carry a nav prefix**: when `.(` is present, the chain in front of it must be a nav chain starting with `.` / `..` / `::` — `.(b)`, `..a.(b)`, `::x.(b)` are legal; `a.(b)` (bridging straight off an ordinary name) is a compile error, use `a.b` for ordinary member access.

> A bridge is not enforced by the grammar — without `.(...)` a positional dot returns the entity itself, which shows up at the script layer as void and is of little practical use.

```
..a.b.c.(d.e)    → nav segment: parent→entity a→entity b→entity c → bridge: the dot chain d.e starting from c
..a.b.c.(x[0])    → same nav segment → bridge: find var x starting from c, then [0] to get the element
```

- `.(...)` accepts literals only (a string key / an int_64 index / null); expressions are not supported
- `.(...)` appears at the chain's tail only, an empty `()` is illegal
- before the bridge the nav segment only navigates entities, a non-entity → `NameError`

### Constraints

- `..` and `.` are never adjacent (`...` is illegal)
- `::` may not appear mid-chain
- `..` may be repeated (`....` = two levels up)
- a positional dot **does not enter container values automatically** — `[]` indexing is allowed only inside a `.(...)` bridge
- **`[]` inside a dot chain accepts literals only** (integer, string, null, including `-N` constant folding). A variable index such as `a[i]` is not allowed — when the index must be computed at run time, build the path by string concatenation first and access it through `@(path)` reflection
- across modules, a **variable**: an existing variable may be read and written, not created or deleted (the import module rule)
- across modules, a **container value**: after a bridge the container's keys are reachable
- `..` beyond the tree depth → `NavError`
- **Module namespace shielding**: an import/link `as` alias is an opaque namespace — a bare read or a compound assignment throws `TypeError`. link additionally shields dot writes. See §11.3 namespace shielding and §13.4 for the link delete rules

> `@` is only effective at the chain's head (see §10). For cross-module reflection use the combination: `var fn = ..cb; @(".." + fn)(args);`.

## 13.4 delete

`delete` is a right-associative prefix expression at the same level as `@` — both are consumed by the same suffix-parsing layer, so it binds **tighter** than `!` / `~` / `+` / `-` (`delete -x` is a syntax error). It returns `bool`. Its operand is a suffix expression, so `delete a[0]` is `delete (a[0])`.
It can be used in an expression context: `var r = delete x; r;` — `r` is `true` (the deletion happened) or `false` (a silent no-op).

### Variable / function / module deletion

```js
delete x;             // delete a variable (the current entity or scope frame)
delete func_name;     // delete a function definition (the current module)
delete alias;         // delete an import / link alias
```

- **is_root()**: a module-level delete goes to the entity's `m_store`; a non-root one goes to the current scope frame
- silent failure: a name that does not exist → returns `false`, no exception
- after a deletion the name can be declared again — once it is out of every namespace, `var`/`def`/`link` all create it normally
- **cross-module delete**: another module's variable cannot be deleted → throws `NameError`
- defensive use: `delete .init;` is safe before a declaration

### Container deletion

`delete var[key]` — the full index range is supported, and it silently returns `false`.

| Type | Index | Behaviour | Cost |
|------|------|------|------|
| map | key present | erase → `true` | O(1) |
| map | key absent | no-op → `false` | O(1) |
| vec | `0 ~ size-1` / `-1` | remove the element, shift the rest → `true` | O(n) |
| vec | out of bounds / empty vec | no-op → `false` | O(1) |
| lst | `0 ~ size-1` / `-1` | remove the node → `true` | O(n) |
| lst | out of bounds / empty lst | no-op → `false` | O(1) |
| non-container | any | TypeError | — |

> A `null` index is not legal (`delete a[null]` → `ConvError`), use `delete a[-1]` to drop the last element.

### link/area delete rules

An import/link `as` alias is an **opaque namespace** — its members are reachable only through `.`, and it cannot be read or copied as a value (`var x = alias` → `TypeError`). link additionally shields dot writes (`alias.x = v` → `TypeError`); dot writes on an import are allowed (they go into the import entity's `m_store`).

A link module's internal data is **completely shielded** from a script. An area instance/namespace (mounted under a link, holding the host object's anyptr) **may be deleted** — deleting it destroys the host object (anyptr destructor → the host object is deleted); **functions (callables) and data variables may not be deleted**. Once an instance/namespace has been deleted, a script touching that path again throws.

| Target | Syntax | Behaviour |
|------|------|------|
| area instance | `delete b.inst` / `delete @("b.inst")` | ✅ deletion allowed (anyptr destructor → host object destroyed) |
| area namespace | `delete b.fs` | ✅ deletion allowed (same as an instance) |
| link/area function | `delete ts.set` / `delete b.fs.open` | ❌ TypeError (a callable is not an object, deletion forbidden) |
| link data variable | `delete ts.data_x` | ❌ TypeError (link data is shielded) |
| area data variable | `delete b.at_f` | ❌ TypeError (not an object, not a callable) |
| positional dot | `delete .ts.data_x` | ❌ TypeError (same as an ordinary dot) |
| @ reflection | `delete @("ts.data_x")` | ❌ TypeError (@ matches the direct syntax) |

> **`@` and the direct syntax have exactly the same semantics**: `delete @("b.x")` ≡ `delete b.x`. `@` is only indirect addressing; it does not change what the operation means.
>
> **native interop**: none of the script-layer restrictions above apply to native code. C++ functions of different link modules can reach each other's data through `nload`/`remove`; the data shielding applies to the script layer only.


<div style="page-break-after: always;"></div>

---

# Appendix A: CLI usage

```bash
Scpt file.axc               # execute a file
Scpt -e '$print("hi");'      # execute inline code (statements need a semicolon)
Scpt -i                      # interactive REPL
Scpt -c file.axc            # compile to .axp
Scpt -o out.axp -c f.axc   # choose the compile output
Scpt -x file.axp            # execute the compiled product
Scpt -j file.axc            # print the compiled AST (extension functions show as EXCALL, the host registry is not checked)
Scpt -p /path -p /lib        # add a search path
Scpt --overflow-check        # enable integer overflow/shift checking
Scpt --max-stack N           # script frame depth limit (default 1024, 0=unlimited)
Scpt --max-vecfill N         # vec fill count limit (default 0 = unlimited)
Scpt -c --embed f.axc       # self-contained compile (§11.5): link/env are errors, imports must resolve
```

`--embed` takes effect only together with `-c` (any other mode is an error). An embed product is a single self-contained .axp: copy it away and it runs, with no dependence on the source tree.

**Suffix conventions**:

- `.axc` source, `.axp` compiled product. The engine decides the kind from the content and does not insist on the suffix.
- **Suffix guessing**: when an import/link path does not state a suffix, the engine first looks it up without one, and only then guesses:
  - `import "foo"` → first `foo`, then `foo.axp`, finally `foo.axc`
  - `link "foo"` → first `foo`, then `foo.so` (Linux) / `foo.dll` (Windows) / `foo.dylib` (macOS)
- **Cross-platform advice**: do not write a platform-specific suffix (such as `.so`) in a link path; let the engine guess, which keeps deployment portable.

<div style="page-break-after: always;"></div>

---

# Appendix B: C++ Host API

## 15. Extension functions `$xxx`

### 15.1 Overview

The extension mechanism gives scripts superset capabilities of the engine platform. The base engine is **zero extensions** — any `$xxx` call is a compile error. The host registers extension functions and predefined constants as needed through `engine::set_extend()` and `engine::set_define()`.

Two motivations:
1. **System/IO interaction**: script functions cannot do file reads and writes, command execution and so on, which until now all depended on `link "base"`. Extension functions lift that class of capability to engine-level builtins, with no link needed.
2. **Compiler-specific extensions**: capabilities specific to the script language itself (metaprogramming, compile-time tools, debugging interfaces and so on) are not suited to being exposed under an ordinary function name, and the `$` prefix namespace gives them a clear semantic boundary.

### 15.2 Syntax

Extension functions are registered by the engine host and use the special `$`-prefixed naming:

    $foo(arg1, arg2)

- `$` is a syntax marker, not part of the name (the host registers the bare name `foo`)
- compile-time check: an unregistered extension function is a compile error
- arguments and types are the function's own responsibility
- **Argument passing**: only a **bare variable** is passed by reference (it points at the variable's storage slot); an index, member, literal or expression is always **evaluated into a copy** and handed to the function. An in-place native function (such as `$vec_push`) that receives `m["a"]` is modifying a copy and **the change does not land back in the container** — to modify in place, use a path assignment: `m["a"][null] = 7`

### 15.3 Relation to the builtin functions

Extension functions are at the same level as the builtins `int()` `float()` `vec()` and the rest:
- not readable through `@` reflection (`@("$name")` without parentheses → TypeError; calling through `@("$name")(...)` does work)
- the call form is exactly the same

### 15.4 Naming restrictions

`$` is a reserved symbol and identifiers do not contain `$` — so the `$xxx` form naturally belongs to extension functions alone.

### 15.5 How to register

```cpp
auto eng = engine::create(cfg);
// register extension functions
eng->set_extend("fread", ext_fread);
eng->set_extend("fwrite", ext_fwrite);
eng->set_extend("exec", ext_exec);
// register predefined constants
eng->set_define("PI", 3.14159);
eng->set_define("VERSION", "1.0.0");
```

### 15.6 Suggested extension functions

The host may register the following extension functions as needed; the base engine does not provide them:

| Function | Arguments | Return | Description |
|------|------|--------|------|
| `$print(...)` | any | null | formatted output to stdout |
| `$input()` | none | string | read a line from stdin |
| `$fread(path)` | path: string | string | read the file's contents |
| `$fwrite(path, content)` | path, content: string | bool | write the file |
| `$exec(command)` | command: string | varmap | run a command, returns {output, excode, success} |
| `$tojs(v[, compact])` | v: any, compact?: bool | string | to a JSON string |
| `$fmjs(s)` | s: string | any | a JSON string to a variant |
| `$datetime()` | none | map | a snapshot of the current local time |

## Engine

```cpp
#include "ascript.h"
using namespace alx;

// default configuration
script::engine* eng = script::engine::create();

// custom configuration (every field optional, persist across reset)
script::engine_config cfg;
cfg.max_stack = 2048;
cfg.overflow_check = true;
eng = script::engine::create(cfg);

// execute source
auto res = eng->exec(bytes_view(bytes("var x = 1;")), "");
// execute a file
res = eng->exec("/path/to/script.axc");

// res: { value, elapsed_us, error }
if (res.error == script::error_type::NoError) {
    $print("%1\n", res.value.to<int_64>());
}

// settable at run time as well
eng->set_max_stack(0);           // 0 = unlimited (mind the C++ stack overflow risk)
eng->set_overflow_check(true);   // overflow checking

// defaults: max_stack=1024, max_vecfill=0 (unlimited)
//           overflow_check=false, parse_depth=1024

delete eng;
```

## Execution hook (hook / trap)

The host attaches one callback through `set_hook`: resource control (interrupting exec on a checkpoint budget, gating import/link loads) + breakpoint debugging (`trap()`) + run-time data inspection (the `wkdt` read-only execution view). **Inside the callback no engine method may be touched** (exec/compile/reset/set_hook and so on) — once one is called, hkdt and every query result are no longer guaranteed; the queries are pure reads with no re-entry.

```cpp
enum class hook_event { exec, import, link, trap, debug };
// exec:        instruction checkpoint (info = checkpoints so far; false → interrupt, InterruptedError is uncatchable)
// import/link: a load request (info = the resolved absolute path; false → refuse)
// trap:        a script breakpoint (info = {here: position map, args?}; false → interrupt)

struct hook_info {
    hook_event type;
    variant info;           // event payload (see above)
    std::string* desc;      // reason for a refusal/interrupt: write *desc, the engine reads the last write
    void* hkdt;             // host data passed through from set_hook (valid until the engine is touched)
    const void* wkdt;       // read-only execution view (valid during the callback only)
    uint_64 freq;           // checkpoint frequency: the engine fills in the current interval, the callback may change it (the only back channel)
};
using hook_fn = bool (*)(hook_info& _info);

eng->set_hook(fn, ud, interval);  // interval=0 → no exec events (trap/gating still apply); fn=nullptr → off
```

**trap breakpoints**: a script's `trap()` / `trap(x)` is explicit instrumentation, and every call fires the trap event (not limited by the interval); `info = {here: posmap, args?}` — `here` is always present ({row, col, ofst, file}), `args` only when an argument was given; **with no hook no argument is evaluated at all** (the same no-op semantics as §9.3b, zero side effects). No hook attached → no-op. A callback returning false → an uncatchable interrupt; **a callback throwing a C++ exception → likewise an interrupt** (desc gets "hook callback threw exception").

**The freq back channel (single-stepping)**: writing `freq` inside the callback changes the checkpoint frequency (m_insn has been zeroed, counting restarts from zero after the return) — `freq = 1` is single-step (the next instruction fires an exec checkpoint), `freq = 0` turns exec events off (trap is unaffected). freq is a **persistent setting the engine does not restore** (it survives a script exception until the host writes it again). Step/step-out actions are implemented on the host side: step out = `freq = 1` plus observing `wkfm_size()` grow shallower at each checkpoint (deciding on function-frame boundaries with `wkfm_func() != "?"`). **The engine provides no instruction position information** — an exec checkpoint has only a sequence number, and positions come from trap anchors.

**The wkdt execution view** (queried during the callback; semantically it trusts the host absolutely — arguments are never validated, and a wrong argument = UB; valid during the callback only):

```cpp
// ── frames (_i = 0 is the top level) ──
uint_64 wkfm_size() const;                            // stack depth, 0 = top level
std::string wkfm_func(uint_64 _i) const;              // reverse lookup by name for a named frame; eval frame → "eval"; block/loop frames and unknown lookups → "?" (=="?" means not a function frame)
const void* wkfm_eptr(uint_64 _i) const;              // the frame's entity handle
std::vector<std::string> wkfm_data_keys(uint_64 _i);  // frame locals (parameters+locals+named data), free slots excluded
const variant* wkfm_data_cptr(uint_64 _i, const char* _key) const;
// ── entity ──
const void* wken_of(const variant* _v) const;         // value → handle; null/non-ent → nullptr
const void* wken_root() const;                        // the root entity handle
std::vector<std::string> wken_data_keys(const void* _e);  // ent-level globals (frame data excluded; an ent with no frame = everything)
const variant* wken_data_cptr(const void* _e, const char* _key) const;
std::string wken_file(const void* _e) const;          // full path, empty → "::"
std::string wken_name(const void* _e) const;          // m_alias
const void* wken_pptr(const void* _e) const;          // parent entity, none → nullptr
// ── link ──
const void* wklk_of(const variant* _v) const;         // value → handle; null/non-link → nullptr
std::vector<std::string> wklk_data_keys(const void* _l);  // every named slot in the store (module-level functions and area objects included)
const variant* wklk_data_cptr(const void* _l, const char* _key) const;
std::vector<std::string> wklk_area_funs(const void* _l, const char* _area);  // the area's internal function names
const void* wklk_pptr(const void* _l) const;          // parent entity, none → nullptr
// ── value kind tests (dispatch on anyptr values in a store; test wkis_* first to pick the query path) ──
bool wkis_ent(const variant* _v) const;   // an import binding
bool wkis_link(const variant* _v) const;  // a link binding
bool wkis_func(const variant* _v) const;  // a function object
bool wkis_area(const variant* _v) const;  // an area host
```

**Example** (a trap breakpoint printing the position + frame chain + locals):

```cpp
static bool cli_hook(script::hook_info& info) {
    if (info.type != script::hook_event::trap) return true; // let the import/link gate through
    auto& m = info.info.to<varmap>();
    auto& here = m.value("here").to<varmap>();
    std::cout << "[trap] " << here.value("file").to<std::string>() << ":"
              << here.value("row").to<int_64>() << ":" << here.value("col").to<int_64>();
    if (m.contain("args")) std::cout << "  arg=" << cli_val(m.value("args"));
    std::cout << "\n";
    for (uint_64 i = info.wkfm_size(); i > 0; i--) {
        std::cout << "    at " << info.wkfm_func(i - 1) << "\n";
    }
    return true;
}
```

## Host function binding

Use a link module and register through `fwrap::bind` inside `alexis_script_load`:

```cpp
// link/myadd.cpp
#include "ascript.h"
using namespace alx::script;

static void my_add(fwrap& args) {
    int_64 a = args[0].to<int_64>();
    int_64 b = args[1].to<int_64>();
    args.freturn(variant(a + b));
}

extern "C" void alexis_script_load(fwrap& args) {
    args.bind("add", my_add);
}
```

```js
// script side
link "myadd" as m;
m.add(1, 2);  // 3
```

## engine interface semantics

**The engine_config struct (the `create(cfg)` argument)**:

```cpp
struct engine_config {
    // runtime
    bool overflow_check = false;
    size_t max_stack = 1024;    // 0 = unlimited
    size_t max_vecfill = 0;     // 0 = unlimited
    size_t parse_depth = 1024;

    // import search paths
    std::list<std::string> search_paths;

    // hook — fn/ud/interval; fn=nullptr -> off; interval=0 -> no exec events
    hook_fn hook_fn_ptr = nullptr;
    void* hook_ud = nullptr;
    uint_64 hook_interval = 0;

    // pipe — null = host default (stdio); the engine never calls them, it only stores the handles
    pipe_in pipe_in_ptr = nullptr;
    void* pipe_in_ud = nullptr;
    pipe_out pipe_out_ptr = nullptr;
    void* pipe_out_ud = nullptr;
};
```

Every field is optional and persists across `reset()`. fwrap can read the current configuration through `wkconfig()`.

**IO redirection**: `pipe_in`/`pipe_out` are function pointers the engine never calls; it only stores the handles for the user to keep and redirect with:

```cpp
using pipe_out = void (*)(const std::string* _out, void* _ud); // engine -> host output
using pipe_in = std::string (*)(void* _ud); // host -> engine input
```

The user can set the IO pipes through `set_pipe()`, to redirect the IO of extension functions such as `$print`/`$input`.

**exec / compile**: `exec(data, home_dir)` executes source or compiled bytes (recognised automatically); `compile(data, home_dir, cmps)` compiles to .axp (`cmps=true` compresses). The file-path overload extracts home_dir automatically. Both return `result{value, elapsed_us, error}` — for the `error` classes see the exception type table (§8.8).

**call — C++ calling a script function**: `call(name, args)` looks up a script-defined def function in the root scope and calls it. `args` is a `varvec` (`std::vector<variant>`), and the return is a `result` (the same as exec's). def functions only — a native function is C++ already, so C++ can call it directly without going through the engine.

```cpp
// definition stage: exec injects the functions
eng->exec(bytes_view(bytes(
    "def process(x) { return x * 2 + 1; }"
    "var counter = 0;"
    "def inc() { counter = counter + 1; return counter; }")),
    "");

// call stage: a single call from C++
auto r = eng->call("process", {variant(int_64(21))});
// r.value → 43, r.error → NoError

// repeated calls; the state survives across them
auto r1 = eng->call("inc", {});  // → 1
auto r2 = eng->call("inc", {});  // → 2

// with STL algorithms: a lambda bridges the sort predicate
eng->exec(bytes_view(bytes(
    "def sort_by_age(a, b) { return a[\"age\"] < b[\"age\"]; }")),
    "");
std::sort(data.begin(), data.end(), [&](const variant& a, const variant& b) {
    auto r = eng->call("sort_by_age", {a, b});
    return r.value.to<bool>();
});

// loops / batches: use exec and let the script drive the control flow
eng->exec(bytes_view(bytes(
    "for (x : data) { process(x); }")),
    "");
```

Design points:
- **root scope only**: the root store is the only one consulted, the same path as `load(name)`
- **a temporary call tree**: every call builds an `[O_CALL, name, [arg]...]` tree which `invoke_def` destroys automatically once it has run, so no AST accumulates
- **recursion safe**: a def may recursively call other defs (tail-recursion TCO included); verified
- **exception handling**: isomorphic to exec (`script_exception` → `std::exception` → `...`), and the exception path resets the walker
- **callable between two execs**: once an exec has finished, `state.current` points at the root and frames is empty

**Version gate**: before executing an .axp its compile-time version tag is **always checked** (this cannot be turned off):

- `etype` — the engine's major version identifier. It must match what `set_etype()` set, otherwise the product is refused (VersionError)
- `vtype` — the extension support level. Host-registered `$xxx` extensions are conventionally **only added, never removed**: an old .axp always runs on a new engine; it is refused only when the .axp asks for a support level beyond the current engine's

The version convention: extensions only added, never changed → a minor version step; deleting/modifying an extension, or a syntax or semantic change → an engine major version (`etype`) change, and old .axp files are no longer compatible.

**Lifetime and thread safety**: an engine is single-threaded (several engines may run in parallel across threads); module execution state (template/instance) is isolated per engine, and the only things shared across engines are read-only resources (compiled AST / dlopen handles), so import/link are concurrency-safe. `get_csys()` returns the **resource layer's** event signal reference — its lifetime ≥ any engine (the resource layer is destroyed only after the last engine), so once connected there is nothing to clean up per engine; the callback signature is `(uint_64 tid, const string& msg)`, with tid the full-width thread id (`alx::this_tid()`), which distinguishes event sources in a multi-engine setting. Event kinds (the instance layer is silent): `ast load/unload` (parse / cache deletion), `link load/unload` (dlopen/dlclose), `link error` (dlopen failure), `env` (the env() builtin).

**reset()**: clears the engine state (module instances are released and the next import loads them again — the hot-reload path); the settings in config (max_stack and so on) persist.

**The execution interrupt hook (set_hook — unified resource control)**:

```cpp
enum class hook_event { exec, import, link, trap, debug };

struct hook_info {
    hook_event type;   // event kind
    variant info;      // exec: checkpoint sequence number (one per interval); import/link: the resolved absolute path; trap: {here, args?}
    std::string* desc; // refusal reason: an entity the engine supplies, so a hook refusing writes *desc = "..." directly (the engine reads the last write)
    void* hkdt;        // host data passed to set_hook, forwarded on every event
    const void* wkdt;  // read-only execution view, valid during the callback only
    uint_64 freq;      // checkpoint frequency: the engine fills in the current interval, the callback may change it

    // read-only config — delegates to walker's engine_config
    const engine_config& wkconfig() const;
};
using hook_fn = bool (*)(hook_info&);  // false → any event is an interrupt (a hard firewall)

eng->set_hook(my_hook, &host_state, 1000);  // one exec event per ~1000 checkpoints; interval=0 → no exec events (gating still applies); fn=null → off
```

- **Deterministic timeout pattern**: the `info` of an exec event is the checkpoint sequence number (one per interval); comparing it against a budget and returning false when over → exec short-circuits at once and returns `InterruptedError` (`result.error`) — the same budget gives the same result, with no wall-clock jitter and no watchdog thread needed
- **Load gating (a hard firewall)**: import/link fires an event for every load request (`info` = the resolved absolute path; embedded modules pass through the gate as well). **The hook is a firewall: any event returning false is an interrupt** — a gate refusal and a timeout interrupt are both `InterruptedError` (`result.value = "execution interrupted — <desc>"`) and uncatchable; a script has no way whatsoever around the firewall (there is no "catchable refusal")
- **An exec interrupt is uncatchable**: a script's `try/catch` cannot swallow an interrupt (the flag short-circuits, and the catch body short-circuits the same way); when the interrupt lands mid-expression, a throw from the in-flight op is likewise caught by the exit path and reported as InterruptedError
- **The source is indistinguishable**: an interrupt may occur anywhere in this exec, or the host may have interrupted one module's init so that a later import fails with InterruptedError as well (a host retry can recover) — the script sees it identically either way, and there is no need or ability to tell them apart
- **Callback constraints**: no engine API may be called inside the hook (the same red line as the event callbacks: read-only observation + return a bool); the `_ud` lifetime is the host's responsibility (it must be valid while installed)
- each engine is independent; the budget resets per exec. The script side has no way to sense or trigger any of this — a script that wants to stop by itself just uses a top-level `return`

## Signals

```cpp
eng->on_cerr.connect([](const std::string& s) { fprintf(stderr, "%s", s.c_str()); });
eng->on_cmpl.connect([](const script::compile_error& e) {
    printf("%u:%u: %s\n", e.row, e.col, e.msg.c_str());
});
// event signal: the resource layer (shared across engines), callbacks carry a thread id; the reference outlives the engine
eng->get_csys().connect([](uint_64 tid, const std::string& s) {
    printf("[%u] %s\n", (unsigned) tid, s.c_str());
});
```

**Callback constraints (identical for all three signals)**: a callback may fire at any moment — including in the middle of an engine's **destruction/reset chain** (the unload event, for instance, is emitted on the destructor path of `delete eng`). Inside a callback **no engine API may be called** (exec/reset/compile and so on) — unless the host intends to destroy or reset that engine (a suicidal act, with no guarantees). A callback should do environment-independent work only: printing, logging, counting, notifying the outside world.

## The full engine interface table

> **Version differences**: the engine API may gain, lose or change methods as versions evolve; this document describes the current version. The actual header (`ascript.h`) is authoritative. The grouping below is by frequency of use.

### Core execution

| Method | Description |
|------|------|
| `exec(data, home_dir)` | execute source or compiled bytes (recognised automatically) → `result{value, elapsed_us, error}` |
| `exec(file_path)` | execute from a file path (home_dir extracted automatically) |
| `call(name, args)` | call a script-defined def function (looked up in the root scope); args is a `varvec`, returns a `result`. def functions only (a native function is called from C++ directly, with no need to go through the engine). Exception handling is isomorphic to exec |
| `load(name, auto_create)` | read and write a root scope variable. Returns a `variant*` (nullptr when absent; created automatically when auto_create=true) |
| `compile(data, home_dir, cmps, embed, hint)` | compile to .axp; `embed=true` compiles self-contained (link/env are errors, imports must resolve); `hint` (bytes_view) = a host JSON payload stored as given (compressed internally when `cmps=true`), empty leaves the key out of the product; the path version's hint is a JSON file path |
| `compile(path, cmps, embed, hint)` | compile from a file path (hint = a JSON file path the engine reads and then takes the same path with) |

### Configuration

| Method | Description |
|------|------|
| `config()` | the current `engine_config` reference (read-only) |
| `set_max_stack(n)` | frame depth cap (default 1024, 0=unlimited) |
| `set_parse_depth(n)` | parser nesting depth (default 1024) |
| `set_overflow_check(on)` | integer overflow / shift checking (default false) |
| `set_max_vecfill(n)` | vec fill count cap (default 0=unlimited) |
| `set_search_paths(paths)` | global search paths (`std::list<string>`, persist across reset) |

### Lifetime

| Method | Description |
|------|------|
| `create(cfg)` | create an engine instance, `cfg` optional (see `engine_config`) |
| `reset()` | reset the engine state (module instances are released, config settings persist, the next import loads again — the hot-reload path) |

### Extension functions ($xxx)

| Method | Description |
|------|------|
| `set_extend(name, fn)` | register an extension function. Returns bool — false when the name collides with a script keyword/reserved word |
| `del_extend(name)` | delete an extension function |
| `get_extend(name)` | get the handler (`native_func`) |
| `fid_extend(name)` | query whether an extension function exists (bool) |
| `set_define(name, value)` | register a static definition (read as `$name` with no parentheses). Returns bool — it shares the `$` namespace with the extension functions, so one name excludes the other |
| `del_define(name)` | delete a static definition |
| `get_define(name)` | get the definition's value (`variant`) |
| `fid_define(name)` | query whether a definition exists (bool) |

### Engine identity

| Method | Description |
|------|------|
| `set_etype(s)` / `etype()` | engine type (string), stamped in when an .axp is compiled and always checked before execution |
| `set_vtype(n)` / `vtype()` | engine version (uint_64), the extension support level, stamped in when an .axp is compiled |

### Control and hooks

| Method | Description |
|------|------|
| `set_hook(fn, ud, interval)` | the unified resource control hook (a hard firewall): one exec event per interval checkpoints; an import/link event per load request. **Any event returning false is an interrupt** (InterruptedError, uncatchable). fn=null → off; no engine API may be called inside the hook |
| `set_pipe(in, out, in_ud, out_ud)` | set the IO pipes (the engine never calls them, it only stores the handles for the user to keep and redirect with); null = host default (stdio) |
| `running()` | cross-thread query: true while an `exec()` is running (an atomic load); `call()` does not count |
| `set_interrupt()` | cross-thread interrupt: sets the interrupt flag, honoured at the next checkpoint (an atomic store) |

### Signals

| Signal | Callback signature | Description |
|------|----------|------|
| `on_cerr` | `void(const string& msg)` | run-time error messages (Uncaught / NativeError and so on) |
| `on_cmpl` | `void(const compile_error& e)` | compile errors (msg, row, col) |
| `get_csys()` | `signal<uint_64, const string&>&` | the event signal reference (shared by the resource layer, callbacks carry the thread id tid): `ast load/unload`, `link load/unload`, `link error`, `env`. Its lifetime ≥ any engine |

### Static utilities

| Method | Description |
|------|------|
| `unpack(data, out)` | unpack an .axp into a readable form (varmap): `ast` (text dump) / `modules` (embedded module key→dump) / `info` (a subset of the metadata) / `hint` (in clear) |
| `prtast(data)` | print the AST (recognises source/compiled/compressed automatically; `$xxx` shows as an EXCALL node) |
| `prtfmt(data)` | lexical-level formatting (no parse needed, so syntactically incomplete input works; 4-space indent, K&R braces, spaces around operators) |
| `prtinf(data)` | dump .axp metadata (etype/vtype/file/name/imports/links/time/info, without the AST) |
