#!/usr/bin/env python3
# Copyright (c) 2026 AlexisVon
# SPDX-License-Identifier: Apache-2.0
"""Assert the capability tables (design.md §6).

What is checked:
  1. every `ex` line runs and produces exactly what it claims -- the docs and the
     tests are the same bytes, so they cannot drift apart;
  2. the demo runs;
  3. structural rules: the name in a declaration is an identifier, no bool flag in it;
  4. the installer: the command it would run names this binary, and the child it spawns
     does not inherit the run's address-space cap (stubbed, so no config is touched).

Each key of the doc is a declaration ("name(p: type): ret") or a notation rule (no
parens); a key starting with "_" is metadata. Value: the assertions for that key.

An `ex` is about whichever channel it exercises: the value the script returns
(`return ...;`) or what it printed (`$io_print(...)`). Either match passes.
`=> !msg` asserts the opposite: the code must fail, with msg on stderr.

Run via test/run.sh.
"""
import glob
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time

ALXRUN = sys.argv[1] if len(sys.argv) > 1 else "./bin/alxrun"
# The floors and the pins below assume the shipped config: a build with a module switch off drops
# its rows on purpose. The overflow switch owns two rows inside the *shared* notation table, so its
# value is read here -- and anything this cannot make sense of counts as ON, so a respelling lowers
# nothing: it fails loudly instead (the rows are there, the floor is the shipped one).
_flag = re.search(r"^#define\s+ALXRUN_OVERFLOW_CHECK\s+(\d+)", open("src/config.h").read(), re.M)
OVERFLOW_ON = _flag is None or _flag.group(1) != "0"
# The probes that set cwd (the fs wall's root is the process's own directory) need a path that
# survives the move; everything else keeps the spelling the caller gave.
ALXRUN_ABS = os.path.abspath(ALXRUN)
fails = []
probes = 0  # gate probes: the installer's client legs, the escape suite, the supervisor


def alxrun(*args):
    return subprocess.run([ALXRUN, *args], capture_output=True, text=True)


def run_code(code, data=None):
    """Run one script; returns (exit code, printed text, [ret] value or None, stderr)."""
    args = ["-e", code] + (["--data", data] if data else [])
    p = alxrun(*args)
    lines = p.stdout.split("\n")
    marks = [i for i, line in enumerate(lines) if line.startswith("[ret] ")]
    if marks:
        ret = lines[marks[-1]][len("[ret] "):]
        printed = "\n".join(lines[:marks[-1]])
    else:
        ret, printed = None, "\n".join(lines)
    return p.returncode, printed.rstrip("\n"), ret, p.stderr.strip()


doc = json.loads(alxrun("--capabilities", ".").stdout)  # "." is the whole doc; no argument is the clan index
forms = {k: v for k, v in doc.items() if not k.startswith("_")}
calls = [k for k in forms if "(" in k]

checked = 0
for key, exs in forms.items():
    for ex in exs:
        if " => " not in ex:
            fails.append(f"{key}: malformed ex: {ex!r}")
            continue
        code, want = ex.split(" => ", 1)
        rc, printed, ret, err = run_code(code)
        checked += 1
        if want.startswith("!"):  # "!msg": the code must fail with msg on stderr
            if rc == 0 or want[1:] not in err:
                fails.append(f"{key}: ex mismatch -- {code!r} want failure {want[1:]!r} "
                             f"got rc={rc} stderr={err!r}")
        elif rc != 0 or (ret != want and printed != want):
            fails.append(f"{key}: ex mismatch -- {code!r} want {want!r} "
                         f"got returned={ret!r} printed={printed!r} (rc={rc} {err})")

# structural: a bare bool in a signature is a hidden behaviour switch --
# split it into two named capabilities instead (design.md §6)
for key in calls:
    if " " in key.split("(", 1)[0]:
        continue  # a language form, not a capability name -- `def f(a, b) {}` (design.md §6)
    params = key.split("(", 1)[1].rsplit(")", 1)[0]  # the return type is not a flag
    if re.search(r":\s*bool\b", params):
        fails.append(f"{key}: bool flag in signature")
    if not re.fullmatch(r"[a-z][a-z0-9_]*", key.split("(", 1)[0]):
        fails.append(f"{key}: name is not [a-z][a-z0-9_]*")

# A row silently dropped from a table keeps every other assertion green -- the material just
# gets thinner (measured: an edit replaced six rows instead of adding to them). Floor the
# count so that cannot happen quietly; bump it when rows are added on purpose. With the
# overflow switch off the count is exact: a lowered floor there would hide rows that outlived
# their switch, which is the one thing the two must never do.
MIN_ASSERTIONS = 237 if OVERFLOW_ON else 235
if checked < MIN_ASSERTIONS:
    fails.append(f"only {checked} assertions -- rows were lost (floor {MIN_ASSERTIONS})")
if not OVERFLOW_ON and checked != MIN_ASSERTIONS:
    fails.append(f"{checked} assertions with ALXRUN_OVERFLOW_CHECK 0 -- want exactly "
                 f"{MIN_ASSERTIONS}: the two overflow rows leave with the switch")

# A family name is the handle for entries the substring pass cannot reach past -- the notation rules
# above all (measured: a model asked for `lang` and got an empty object back).
for clan in ("notation", "syntax", "type", "func", "[]"):
    if not json.loads(alxrun("--capabilities", clan).stdout):
        fails.append(f"--capabilities {clan!r}: clan routing came back empty")

# `lang` must carry the language layer whole: the notation rules have no prefix to search by,
# so a query for the language that returns only `int(v)` is a miss (measured).
index = json.loads(alxrun("--capabilities").stdout)  # no argument = the clan index
if "_clans" not in index or "notation" not in index["_clans"]:
    fails.append(f"--capabilities (no argument): expected the clan index, got {list(index)[:3]}")
if "type(v: any): string" not in json.loads(alxrun("--capabilities", "syntax").stdout):
    fails.append("--capabilities syntax: must carry notation rules and type()")

# A shape in hand must reach its rule: notation keys are prose, so they match on a substring.
# Asserted as "at least contains" -- the regex step may add more on top (a query can be both a
# handle and a pattern), so how much noise rides along is a separate question.
for shape, want in (("[", "[v:N]=fill"), ("[null]", "x[null]=size"),
                    ("[-1]", "x[-1]=back"), ("delete", "delete x"),
                    ("[]", "x[a,b]=slice"), ("]", "x[null]=size"), ("+", "x+y: same kind"),
                    ("def", "def f(a, b) {}"), ("var", "var x = any"),
                    ("notation", "x[null]=size"), ("string", "str_split(s: string, delim: string): vec"),
                    ("syntax", "type(v: any): string"), ("type", "int(v: any): int")):
    got = json.loads(alxrun("--capabilities", shape).stdout)
    if want not in got:
        fails.append(f"--capabilities {shape!r}: missing {want!r}")

# The query is literal: a clan name answers with that family whole, anything else is a substring of
# the key, matched as written. No regex -- `[null]` read as a character class answered with 90 of 100
# keys (measured), and a query whose meaning depends on which metacharacters it happens to carry is
# not one anyone can predict.
if any("(" in k for k in json.loads(alxrun("--capabilities", "[null]").stdout)):
    fails.append("--capabilities '[null]': a shape query answers with rules, never functions")
if json.loads(alxrun("--capabilities", "[nul]").stdout):
    fails.append("--capabilities '[nul]': a substring that matches nothing must return nothing")
if json.loads(alxrun("--capabilities", r"\[null\]").stdout):
    fails.append("--capabilities '\\[null\\]': a backslash is a byte, not an escape -- no key carries one")
# "." is the one sentinel, and it is the whole doc: metadata, rules and functions together.
whole = json.loads(alxrun("--capabilities", ".").stdout)
if "_clan" not in whole or "x[null]=size" not in whole or not any("(" in k for k in whole):
    fails.append("--capabilities '.': must return the whole doc")

# A family name must be a word no key carries literally, or the substring pass rides along and
# dilutes it (measured: `string` answered with 40 keys for a 20-key family). `syntax` is the one
# composite name -- it must be exactly the union of its parts.
labelled = whole.get("_clan", {})
family = {n: {k for k, l in labelled.items() if n in l} for n in ("[]", "func", "notation", "type")}
for name, fam in family.items():
    got = set(json.loads(alxrun("--capabilities", name).stdout))
    if got != fam:
        fails.append(f"--capabilities {name!r}: family is {len(fam)} keys, answered with {len(got)}")
lang = family["notation"] | family["type"] | family["func"]
if set(json.loads(alxrun("--capabilities", "syntax").stdout)) != lang:
    fails.append("--capabilities 'syntax': must be exactly notation ∪ type ∪ func")
# A substring reaches inside a name: `split` has to find both split forms.
if not {"str_split(s: string, delim: string): vec",
        "regex_split(pattern: string, s: string): vec"}.issubset(json.loads(alxrun("--capabilities", "split").stdout)):
    fails.append("--capabilities 'split': a substring must reach inside a name")
for handle in ("size", "bool"):
    if not any("(" in k for k in json.loads(alxrun("--capabilities", handle).stdout)):
        fails.append(f"--capabilities {handle!r}: expected function keys in the union")

# The installer writes into the user's client config, so only its dry run is tested: the
# command it would run has to point at this very binary, in the scope the flag asks for.
for scope, flag in (("local", []), ("user", ["-g"])):
    p = alxrun("install", *flag, "--dry-run")
    want = f"claude mcp add alxrun -s {scope} -- "
    if p.returncode != 0 or want not in p.stdout or os.path.realpath(ALXRUN) not in p.stdout:
        fails.append(f"install --dry-run ({scope}): unexpected plan rc={p.returncode} {p.stdout!r}")

if alxrun("install", "-n").returncode != 101:
    fails.append("install -n: declining must exit 101")

# Both tools the model actually calls get an exact rule (a wildcard re-prompts instead), and
# `capabilities` is a read-only query -- prompting for it is friction with nothing behind it.
if alxrun("install", "-g", "--dry-run").stdout.count("mcp__alxrun__") != 2:
    fails.append("install -g --dry-run: must plan both allow rules (run + capabilities)")

# The Codex leg exists only when its CLI does: a stub on PATH stands in for the client, and a
# PATH without it must plan no codex step at all. Each run sees no `claude` either, so the plan
# is exactly [claude add] or [claude add, codex add].
if os.name == "posix":
    probes += 1
    codex_dir, codex_home = tempfile.mkdtemp(), tempfile.mkdtemp()
    codex = os.path.join(codex_dir, "codex")
    with open(codex, "w") as f:
        f.write("#!/bin/sh\nexit 1\n")  # `mcp get` fails: not registered
    os.chmod(codex, 0o755)
    env = dict(os.environ, PATH=codex_dir, HOME=codex_home)
    p = subprocess.run([ALXRUN, "install", "--dry-run"], capture_output=True, text=True, env=env)
    if (p.returncode != 0 or "codex mcp add alxrun -- " not in p.stdout
            or os.path.realpath(ALXRUN) not in p.stdout):
        fails.append(f"install --dry-run (codex present): expected the codex leg, got {p.stdout!r}")
    probes += 1
    empty_dir, empty_home = tempfile.mkdtemp(), tempfile.mkdtemp()
    env = dict(os.environ, PATH=empty_dir, HOME=empty_home)
    p = subprocess.run([ALXRUN, "install", "--dry-run"], capture_output=True, text=True, env=env)
    if p.returncode != 0 or "codex" in p.stdout:
        fails.append(f"install --dry-run (no clients): must not plan codex, got {p.stdout!r}")
    for d in (codex_dir, codex_home, empty_dir, empty_home):
        shutil.rmtree(d, ignore_errors=True)

# The brief must spell a call the way the doc's own assertions do: registered capabilities
# carry `$`, engine builtins are bare (`float(v)`). A wrong signpost there cost a model a
# ParseError plus a full doc lookup (measured), so derive the rule and pin it.
brief = alxrun("--brief").stdout
for entry in re.findall(r"\S+\([^)]*\)", brief):
    head = entry.split("(", 1)[0]
    name = head.lstrip("$")
    key = next((k for k in calls if k.split("(", 1)[0] == name), None)
    if key is None:
        continue
    spelled = "$" + name in " ".join(forms[key])
    if spelled != head.startswith("$"):
        fails.append(f"brief spells {entry!r} but the doc's assertions call it "
                     f"{'with' if spelled else 'without'} a $")

# A child spawned outside a run must not inherit the address-space cap: the client is a JS
# runtime and aborts under it, which is how `install` used to die before the cap was scoped to
# `kernel::run`. The stub checks its own `ulimit -v` -- local, no client, no config touched.
if os.name == "posix":
    stub_dir, home_dir = tempfile.mkdtemp(), tempfile.mkdtemp()
    stub = os.path.join(stub_dir, "claude")
    with open(stub, "w") as f:
        f.write('#!/bin/sh\n[ "$(ulimit -v)" = "unlimited" ] || { echo "VA cap: $(ulimit -v)" >&2; exit 9; }\n')
    os.chmod(stub, 0o755)
    env = dict(os.environ, PATH=stub_dir, HOME=home_dir)
    p = subprocess.run([ALXRUN, "install", "-y"], capture_output=True, text=True, env=env)
    if p.returncode != 0:
        fails.append(f"install: the spawned child inherited a VA cap (rc={p.returncode} {p.stderr.strip()!r})")
    shutil.rmtree(stub_dir, ignore_errors=True)
    shutil.rmtree(home_dir, ignore_errors=True)

# ---- the escape suite (doc/escape.md §D) ------------------------------------
# One assertion per door the engine leaves a script: refused here, or provably inert.
# A refusal has to come from the gate ("not available here", rc=1) -- an import that
# dies because the file is missing proves nothing, so every probe names a module that
# exists. This is the capability side only; memory safety is the sanitizer run's account.
probe_dir = tempfile.mkdtemp()
probe_mod = os.path.join(probe_dir, "gate_probe.axc")
env_dir = os.path.join(probe_dir, "envdir")
os.makedirs(env_dir)
with open(probe_mod, "w") as f:
    f.write("return 1;\n")
shutil.copy(probe_mod, os.path.join(env_dir, "evil.axc"))

# `env` re-points the resolver but the gate sits *after* resolution, and `eval` inherits
# the hook -- neither moves the door nor slips past it. The eval sources use escaped double
# quotes: `'...'` is a code-point literal since 1.2.0, so the old single-quoted form stopped parsing.
for code, why in (('link "/usr/lib/libc.so.6" as c; return 1;', "link"),
                  (f'import "{probe_mod}" as m; return 1;', "import"),
                  (f'env(["{env_dir}"]); import "evil.axc" as m; return 1;', "env then import"),
                  (f'eval("import \\"{probe_mod}\\" as m; return 1;");', "eval then import"),
                  ('eval("link \\"/usr/lib/libc.so.6\\" as c; return 1;");', "eval then link")):
    probes += 1
    rc, _, _, err = run_code(code)
    if rc != 1 or "not available here" not in err:
        fails.append(f"gate let {why} through: rc={rc} err={err[:80]!r}")

# `eval` and `@` stay because they are another way to write code, not another way to get
# any: they run against the same registry, and both refuse what is not there. Each
# refusal is paired with a working shallow case -- a probe that fails for an unrelated
# reason (nesting limit reached early, a typo) would otherwise pass for the wrong one.
def nest(n, tail, pre=""):
    """`pre`, then a string of 2**n nested parens, then `tail` (which uses `o` / `c`)."""
    return (pre + 'var o = "("; var c = ")"; var i = 0; '
            f'while (i < {n}) {{ o = o + o; c = c + c; i = i + 1; }} ' + tail)

eval_tail, at_tail = ('return eval("return " + o + "1" + c + ";");', 'return @(o + "x" + c);')
for code, want, why in ((nest(15, eval_tail), None, "eval"),
                        (nest(15, at_tail, pre="var x = 7; "), None, "@"),
                        (nest(6, eval_tail), "1", "eval (shallow)"),
                        (nest(6, at_tail, pre="var x = 7; "), "7", "@ (shallow)")):
    probes += 1
    rc, _, ret, err = run_code(code)
    if want is None:
        # Depth 32768 is the case the engine once got wrong (parse_depth = 0 walked the host
        # stack): refused is the only pass -- a crash, a hang, or a successful parse all fail.
        if rc is None or rc <= 0:
            fails.append(f"{why} at depth 32768 must be refused: rc={rc} err={err[:80]!r}")
    elif rc != 0 or ret != want:
        fails.append(f"{why} must still work at depth 64: rc={rc} ret={ret!r} err={err[:80]!r}")

# Nothing a script writes produces an `anyptr` (the engine's opaque C++ handle), and a
# registered capability is call syntax -- never a value. The control below shows the name
# is reachable, so these refusals are about *reading* one out, not about it being absent.
for code, want in (('var x = anyptr; return 1;', "Undefined: anyptr"),
                   ('var f = $fs_read; return f("/etc/hostname");', "cannot be read as value"),
                   ('return @("$fs_read");', "cannot be read as value"),
                   ('delete type; return 1;', "cannot be used as a variable or value"),
                   ('return iload("x");', "Undefined function: iload")):
    probes += 1
    rc, _, _, err = run_code(code)
    if rc == 0 or want not in err:
        fails.append(f"must be refused ({want!r}): {code!r} rc={rc} err={err[:80]!r}")

probes += 1
rc, _, ret, err = run_code('return @("$str_upper")("a");')
if rc != 0 or ret != '"A"':
    fails.append(f"@ must still call a registered capability: rc={rc} ret={ret!r} err={err[:80]!r}")

# `env` / `trap` / `here` are kept and inert: env only re-points the resolver the gate
# sits behind, and AlxRun connects no trap channel.
for code, want in (('env(["/nonexistent"]); return $str_upper("a");', '"A"'),
                   ('return trap();', "null"), ('return trap(1, 2);', "null"),
                   ('return type(here());', '"map"')):
    probes += 1
    rc, _, ret, err = run_code(code)
    if rc != 0 or ret != want:
        fails.append(f"must be inert, want {want!r}: {code!r} rc={rc} ret={ret!r} err={err[:80]!r}")
shutil.rmtree(probe_dir, ignore_errors=True)

# A runaway script must be cut by a budget, not by the client's patience -- one wall now (the
# watchdog's clock), and the message names the window that ran out.
big = 'var s = "x"; for (var i = 0; i < 26; i = i + 1) { s = s + s; }' # 64 MiB, ~25 instructions
for probe, limit, budget in (('while (true) { }', 20, "time budget"),
                             ('var s = ""; while (true) { s = s + "xxxxxxxxxxxxxxxxxxxx"; }',
                              40, "time budget"),
                             ('return eval("while (true) { }");', 20, "time budget"),
                             (big + 'var n = 0; for (var k = 0; k < 10; k = k + 1) '
                                    '{ n = n + $str_size($str_replace(s, "x", "y")); } return n;',
                              12, "time budget")):
    probes += 1
    t0 = time.monotonic()
    try:
        p = subprocess.run([ALXRUN, "-e", probe], capture_output=True, text=True, timeout=limit)
        rc, err = p.returncode, p.stderr
    except subprocess.TimeoutExpired:
        rc, err = None, ""
    spent = time.monotonic() - t0
    if rc != 1 or budget not in err or spent > limit:
        fails.append(f"budget let a runaway script through: rc={rc}, {spent:.1f}s, "
                     f"want {budget!r} -- {probe[:40]!r}")

# $regex_* is the one family with a checkpoint INSIDE the call (design.md §2): every match is bounded by
# PCRE2's step limit, and a long find_all / replace_all reaches the wall through the callout. Three shapes,
# all measured: the pattern that used to run 165 s past a 5 s window is pruned before its first step; the
# shape the prune cannot reach is a named ResourceError, not a silent no-match; one native call that needs
# ~8 s is cut at the window instead of outliving it.
probes += 1
t0 = time.monotonic()
rc, _, ret, err = run_code('return $regex_find("(a+)+b", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");')
spent = time.monotonic() - t0
if rc != 0 or ret != "null" or spent > 1.0:
    fails.append(f"regex: the 165 s shape must be pruned, not run -- rc={rc} ret={ret!r} "
                 f"{spent:.2f}s {err[:60]!r}")

probes += 1  # what the prune cannot reach: the step limit is the bound, and it is a named refusal
t0 = time.monotonic()
rc, _, _, err = run_code('return $regex_match("^(a+)+$", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa!");')
spent = time.monotonic() - t0
if rc == 0 or "[ResourceError]" not in err or "step limit" not in err or spent > 3.0:
    fails.append(f"regex: the step limit must be a named ResourceError -- rc={rc} {spent:.2f}s "
                 f"{err[:60]!r}")

probes += 1  # one native call, no script checkpoint anywhere in it
t0 = time.monotonic()
p = subprocess.run([ALXRUN, "-e", 'var s = "x"; for (var i = 0; i < 27; i = i + 1) { s = s + s; } '
                                  'return $str_size($regex_replace_all("x", s, "y"));'],
                   capture_output=True, text=True, timeout=15)
spent = time.monotonic() - t0
if p.returncode != 1 or "time budget exceeded" not in p.stderr or spent > 6.5:
    fails.append(f"regex: the wall must land inside one call -- rc={p.returncode} {spent:.1f}s "
                 f"err={p.stderr[:60]!r}")

# One memory cap, flat, for the whole process (issue.md I-4): whichever step hits it -- the
# script's own allocations or the copy back -- the run ends as a named ResourceError, and never
# as a signal. n=27 is the shape that used to abort (rc=134) when that copy failed inside the
# engine's noexcept variant; the CLI face and the MCP child must agree here (design.md §2).
for n in (27, 28):
    probes += 1
    p = alxrun("-e", f'var s = "x"; for (var i = 0; i < {n}; i = i + 1) {{ s = s + s; }} return s;')
    if p.returncode != 2 or "memory budget exceeded" not in p.stderr:
        fails.append(f"memory wall at n={n}: rc={p.returncode} want 15 + 'memory budget exceeded' "
                     f"-- err={p.stderr[:60]!r}")

probes += 1  # printing grows the sink inside the wall: the engine catches it, no signal
p = subprocess.run([ALXRUN, "-e", 'var s = "x"; for (var i = 0; i < 27; i = i + 1) { s = s + s; } '
                                  '$io_print(s);'],
                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
if p.returncode != 2 or "memory budget exceeded" not in p.stderr:
    fails.append(f"big print: rc={p.returncode} err={p.stderr[:60]!r}")

# Data comes in over the wire -- the one copy the run's cap cannot cover -- so the door bounds it
# (mcp.cpp kMaxRequest): over it nothing is parsed, under it the bind lands. A client used to be
# able to abort the whole server here (no catch in mcp.cpp), so liveness is part of the assertion.
init = ('{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25",'
        '"capabilities":{},"clientInfo":{"name":"t","version":"1"}}}')
for mb, refused in ((80, True), (60, False)):
    probes += 1
    call = json.dumps({"jsonrpc": "2.0", "id": 2, "method": "tools/call",
                       "params": {"name": "run", "arguments": {"code": "return $str_size(rows);",
                                                               "data": {"rows": "x" * (mb << 20)}}}})
    p = subprocess.run([ALXRUN, "mcp"], input=init + "\n" + call + '\n{"jsonrpc":"2.0","id":3,"method":"ping"}\n',
                       capture_output=True, text=True, timeout=120)
    alive = '"id":3' in p.stdout
    if p.returncode != 0 or not alive or ("request too large" in p.stdout) != refused:
        fails.append(f"{mb} MB request: rc={p.returncode}, alive={alive}, refused={refused}, "
                     f"err={p.stderr[:60]!r}")

# The supervisor's whole point (design.md §4): the engine's death must not take the session with
# it. Kill the child mid-run and the session has to answer for the dead call (with its id, from
# the child's ack) and for the next one.
def mcp_session(lines, timeout=40):
    """Run one MCP session over stdio with the given request lines."""
    p = subprocess.Popen([ALXRUN, "mcp"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, text=True)
    p.stdin.write("\n".join(json.dumps(l) for l in lines) + "\n")
    p.stdin.flush()
    return p


def child_pids(pid):
    out = []
    for task in glob.glob(f"/proc/{pid}/task/*/children"):
        try:
            out += [int(x) for x in open(task).read().split()]
        except OSError:
            pass
    return out


probes += 1
init = {"jsonrpc": "2.0", "id": 10, "method": "initialize",
        "params": {"protocolVersion": "2025-11-25", "capabilities": {},
                   "clientInfo": {"name": "t", "version": "1"}}}
slow = {"jsonrpc": "2.0", "id": 11, "method": "tools/call",
        "params": {"name": "run", "arguments": {"code": "var i=0; while (i<40000000) { i=i+1; } return i;"}}}
p = mcp_session([init, slow])
time.sleep(0.3)  # let the initialize child finish: the one left is the run being crashed
victim = None
for _ in range(2000):  # the child appears as soon as the call is dispatched; the window is wide
                       # because a busy machine (a fuzz campaign) can starve the supervisor

    kids = child_pids(p.pid)
    if kids:
        victim = kids[0]
        break
    time.sleep(0.01)
if victim:
    os.kill(victim, signal.SIGABRT)
p.stdin.write(json.dumps({"jsonrpc": "2.0", "id": 12, "method": "ping"}) + "\n")
p.stdin.close()
out = p.stdout.read()
p.wait(timeout=30)
if not victim:
    fails.append("supervisor: no child process was seen for a tools/call")
elif (p.returncode != 0 or '"id":11' not in out or '"id":12' not in out
      or '"isError":true' not in out):
    fails.append(f"supervisor: a killed child must not take the session down -- rc={p.returncode}, "
                 f"out={out[:160]!r}")

# Concurrency is a build dimension (ALXRUN_MAX_CHILDREN), and the tool description promises the
# number to the model. The probe reads that promise back and tests the behaviour against it, so
# the two cannot drift: N calls share one call's time, N+1 calls take two rounds.
probes += 1
work = 'var i=0; while (i<20000000) { i=i+1; } return i;' # ~2 s, well inside every budget


def promised_concurrency():
    p = mcp_session([init, {"jsonrpc": "2.0", "id": 39, "method": "tools/list", "params": {}}])
    p.stdin.close()
    out = p.stdout.read()
    p.wait(timeout=30)
    m = re.search(r"Up to (\d+) calls run at once", out)
    return int(m.group(1)) if m else 0


def timed_calls(n):
    lines = [init] + [{"jsonrpc": "2.0", "id": 40 + k, "method": "tools/call",
                       "params": {"name": "run", "arguments": {"code": work}}} for k in range(n)]
    t0 = time.monotonic()
    p = mcp_session(lines)
    p.stdin.close()
    out = p.stdout.read()
    p.wait(timeout=60)
    answered = sum(1 for k in range(n) if f'"id":{40 + k}' in out)
    return time.monotonic() - t0, answered


cap = promised_concurrency()
one, answered1 = timed_calls(1)
two, answered2 = timed_calls(2)
three, answered3 = timed_calls(3)
if cap < 1 or answered1 != 1 or answered2 != 2 or answered3 != 3:
    fails.append(f"concurrency: cap promised as {cap}, replies answered {answered1}/{answered2}/"
                 f"{answered3}")
elif cap >= 2 and two > one * 1.5:
    fails.append(f"concurrency: {cap} promised, but two calls took {two:.1f}s vs one {one:.1f}s "
                 f"-- they ran serially")
elif cap == 1 and two < one * 1.4:
    fails.append(f"concurrency: 1 promised, but two calls took {two:.1f}s vs one {one:.1f}s")
if cap == 2 and three < two * 1.4:
    fails.append(f"concurrency: past the cap (2) three calls took {three:.1f}s vs two {two:.1f}s "
                 f"-- nothing queued")

# Replies leave as they finish, not in request order (JSON-RPC correlates by id -- that is what
# the id is for). A slow call followed by a fast one must answer fast-first, each with its own id.
probes += 1
quiet = 'var i=0; while (i<2000000) { i=i+1; } return 0;'
p = mcp_session([init,
                 {"jsonrpc": "2.0", "id": 60, "method": "tools/call",
                  "params": {"name": "run", "arguments": {"code": work}}},
                 {"jsonrpc": "2.0", "id": 61, "method": "tools/call",
                  "params": {"name": "run", "arguments": {"code": quiet}}}])
p.stdin.close()
out = p.stdout.read()
p.wait(timeout=60)
order = [l.split('"id":')[1].split(",")[0] for l in out.splitlines() if '"id":' in l]
if order[-2:] != ["61", "60"]:
    fails.append(f"concurrency: replies must leave as they finish (id order was {order})")

# The client may cancel a request it no longer wants (Esc in the harness). The parent must stop
# exactly that child, answer its id with the JSON-RPC cancellation error -- at once, not after the
# others -- and let everything else run to the end.
probes += 1
p = mcp_session([init,
                 {"jsonrpc": "2.0", "id": 80, "method": "tools/call",
                  "params": {"name": "run", "arguments": {"code": work}}},
                 {"jsonrpc": "2.0", "id": 81, "method": "tools/call",
                  "params": {"name": "run", "arguments": {"code": work}}}])
time.sleep(0.4)  # a cancellation lands mid-run
p.stdin.write(json.dumps({"jsonrpc": "2.0", "method": "notifications/cancelled",
                          "params": {"requestId": 80, "reason": "user"}}) + "\n")
p.stdin.flush()
p.stdin.write(json.dumps({"jsonrpc": "2.0", "id": 82, "method": "ping"}) + "\n")
p.stdin.flush()
p.stdin.close()
out = p.stdout.read()
p.wait(timeout=60)
answered = [l for l in out.splitlines() if '"id":' in l]
order = [l.split('"id":')[1].split(",")[0] for l in answered]
if not any('"id":80' in l and "-32800" in l for l in answered):
    fails.append(f"cancel: id 80 must come back as -32800 (got {out[:120]!r})")
if not any('"id":81' in l and '"isError":false' in l for l in answered):
    fails.append("cancel: only the cancelled request may stop (81 must finish)")
if "80" in order and "81" in order and order.index("80") > order.index("81"):
    fails.append(f"cancel: the answer to the cancelled id must leave first (order {order})")

# A value with no JSON face used to come back as a *silent* null -- indistinguishable from
# "the script returned nothing" (issue.md I-5). The engine refuses it at the source now: a callable
# cannot be read as a value at all, and a result holding a host handle is refused at the exec exit
# (`exec result: <type> cannot be returned`, which the kernel lifts out of the value). Named either
# way; the pinned id is the engine's own classification (10 = TypeError).
probes += 1
for code, nested in (('def f() { return 1; } return @("f");', False),
                     ('def f() { return 1; } return [1, @("f")];', True)):
    rc, _, ret, err = run_code(code)
    if rc != 10 or "cannot be read as a value" not in err or ret is not None:
        fails.append(f"unserializable value{'(nested)' if nested else ''}: rc={rc} ret={ret!r} "
                     f"err={err[:70]!r} -- want a named refusal")

# $fs_read only takes regular files (issue.md I-3): a FIFO must be refused *before* any read --
# opening one blocks until a writer appears, and a blocked syscall is a wall no budget can cut.
# The FIFO is made inside the run's own cwd: the fs rules cover that by default, so this probe is
# about the type gate and not about the wall.
probes += 1
fifo_dir = tempfile.mkdtemp(prefix="alxrun-fifo-")
fifo = os.path.join(fifo_dir, "p.fifo")
os.mkfifo(fifo)
t0 = time.monotonic()
try:
    p = subprocess.run([ALXRUN_ABS, "-e", 'return $fs_read("p.fifo");'], capture_output=True,
                       text=True, timeout=10, cwd=fifo_dir)
    rc, err, spent = p.returncode, p.stderr, time.monotonic() - t0
except subprocess.TimeoutExpired:
    rc, err, spent = None, "", time.monotonic() - t0
os.unlink(fifo)
os.rmdir(fifo_dir)
if rc == 0 or "[RuntimeError]" not in err or "not a regular file" not in err or spent > 3:
    fails.append(f"fs_read on a FIFO: rc={rc}, {spent:.1f}s, err={err[:70]!r} "
                 f"-- want a named refusal at once (RuntimeError + 'not a regular file')")

# ---------------------------------------------------------------------------
# The fs wall (design.md §6). The rules are compiled in (src/config.h) and their default root is
# the process's own cwd, so every probe here builds a directory to stand in and runs the binary
# with cwd set -- never from the repo, which is what the assertions above rely on.
# ---------------------------------------------------------------------------
wall_root = tempfile.mkdtemp(prefix="alxrun-wall-")
sib = wall_root + "-evil"  # a sibling whose name is a prefix of the root, not a directory under it
os.mkdir(sib)
with open(os.path.join(wall_root, "inside.txt"), "w") as f:
    f.write("hello")
os.makedirs(os.path.join(wall_root, ".git"))
with open(os.path.join(wall_root, ".git", "config"), "w") as f:
    f.write("[core]\n")
with open(sib + "/sneak.txt", "w") as f:
    f.write("no\n")
outside = tempfile.mkdtemp(prefix="alxrun-outside-")
with open(os.path.join(outside, "secret.txt"), "w") as f:
    f.write("no\n")


def in_wall(code, cwd=wall_root):
    p = subprocess.run([ALXRUN_ABS, "-e", code], capture_output=True, text=True, timeout=20, cwd=cwd)
    return p.returncode, p.stdout, p.stderr


def wall_case(label, code, want_ok, want_in_err=""):
    global probes
    probes += 1
    rc, out, err = in_wall(code)
    hit = want_in_err in err
    if (rc == 0) != want_ok or not hit:
        fails.append(f"wall {label}: rc={rc} err={err[:90]!r} -- want {'pass' if want_ok else want_in_err}")


wall_case("reads inside the root", 'return $fs_read("inside.txt");', True)
wall_case("deny inside the root", 'return $fs_read(".git/config");', False, "denied by the host rules")
wall_case("sibling name is not the root", f'return $fs_read("{sib}/sneak.txt");', False,
          "outside the allowed read roots")
wall_case("parent escape", f'return $fs_read("{outside}/secret.txt");', False,
          "outside the allowed read roots")
# The refusal names the rule that judged it: the wall is compiled in, so the model has no other way
# to learn it -- and the resolved root is only knowable here, from this process's own cwd.
wall_case("refusal names the root",
          f'return $fs_read("{outside}/secret.txt");', False,
          "outside the allowed read roots (" + os.path.realpath(wall_root) + ")")
# The write door: create, then replace, both inside the root; the escape and the symlink are refused.
wall_case("write creates", 'return $fs_write("made.txt", "hi");', True)
wall_case("write replaces", 'return $fs_write("made.txt", "more");', True)
wall_case("write escape", f'return $fs_write("{outside}/landed.txt", "x");', False,
          "outside the allowed write roots")
wall_case("write deny", 'return $fs_write(".env", "x");', False, "denied by the host rules")
os.symlink(os.path.join(outside, "secret.txt"), os.path.join(wall_root, "link.txt"))
os.symlink(outside, os.path.join(wall_root, "linkdir"))
wall_case("write through a symlink", 'return $fs_write("link.txt", "x");', False,
          "is a symbolic link")
wall_case("write through a symlinked dir", 'return $fs_write("linkdir/landed.txt", "x");', False,
          "outside the allowed write roots")
wall_case("read through a symlink", 'return $fs_read("link.txt");', False,
          "outside the allowed read roots")
probes += 1
rc, out, err = in_wall('return $fs_read("made.txt");')
if rc != 0 or "more" not in out:
    fails.append(f"wall write content: rc={rc} out={out[:60]!r} -- want the replaced bytes read back")
# Nothing may have landed on either side of the boundary: the refusals happen before open().
probes += 1
if os.path.exists(os.path.join(outside, "landed.txt")) or os.path.exists(sib + "/landed.txt"):
    fails.append("wall: a refused write left a file outside the root")
if not os.path.exists(os.path.join(wall_root, "made.txt")):
    fails.append("wall: an allowed write did not land inside the root")
# Root "/" is a rule too, and it is what still proves the procfs case: st_size is 0 and the content
# is not (that is why $fs_read streams to EOF).
probes += 1
rc, out, err = in_wall('return $str_size($fs_read("/proc/self/status")) > 0;', cwd="/")
if rc != 0 or "true" not in out:
    fails.append(f"wall root /: rc={rc} out={out[:60]!r} err={err[:60]!r} -- want /proc/self/status read")

# ---------------------------------------------------------------------------
# Stream handles: what the whole-file pair cannot do, plus the release the engine gives for free.
# The handle is closed when the script drops it -- overwritten, deleted, or the run ending (measured
# in AScript's binary first, then here) -- so a loop that opens 2000 of them must not run out of
# descriptors (RLIMIT_NOFILE is 1024): that it completes at all *is* the assertion.
# ---------------------------------------------------------------------------
probes += 1
rc, out, err = in_wall('var h = $fs_istream("inside.txt"); var i = 0;'
                       ' while (i < 2000) { h = $fs_istream("inside.txt"); i = i + 1; } return i;')
if rc != 0 or "2000" not in out:
    fails.append(f"handles: overwriting 2000 read handles: rc={rc} out={out[:60]!r} err={err[:80]!r} "
                 f"-- the dropped handle must close (EMFILE otherwise)")
probes += 1
rc, out, err = in_wall('var i = 0;'
                       ' while (i < 2000) { var h = $fs_istream("inside.txt"); delete h; i = i + 1; }'
                       ' return i;')
if rc != 0 or "2000" not in out:
    fails.append(f"handles: deleting 2000 read handles: rc={rc} out={out[:60]!r} err={err[:80]!r} "
                 f"-- delete must close too")
# A product written in pieces, then read back in a slice: the two things $fs_write/$fs_read cannot
# do (they take the whole file at once, and both hands have to fit under the memory wall).
probes += 1
rc, out, err = in_wall('var h = $fs_ostream("pieced.txt"); var i = 0;'
                       ' while (i < 4) { $io_write(h, "0123456789"); i = i + 1; } return $io_write(h, "!");')
if rc != 0 or "1" not in out:
    fails.append(f"handles: writing in pieces: rc={rc} out={out[:60]!r} err={err[:80]!r}")
probes += 1
rc, out, err = in_wall('var h = $fs_istream("pieced.txt");'
                       ' return $io_read(h, 3, 4) + "/" + string($str_size($io_read(h)));')
if rc != 0 or '"3456/41"' not in out:
    fails.append(f"handles: slicing a stream: rc={rc} out={out[:70]!r} err={err[:80]!r} "
                 f"-- want the slice 3456 and 41 bytes left")
probes += 1
rc, out, err = in_wall('return $io_read($fs_istream("inside.txt"), 99);')
if rc != 0 or '""' not in out:
    fails.append(f"handles: reading past EOF: rc={rc} out={out[:60]!r} -- a short read is empty, not an error")
probes += 1
rc, out, err = in_wall('return $io_write($fs_istream("inside.txt"), "x");')
if rc == 0 or "handle type mismatch" not in err:
    fails.append(f"handles: writing through a read handle: rc={rc} err={err[:70]!r} -- want a named refusal")
# A handle copied by value is another name for the same stream (the binding is refcounted): it reads
# the same bytes, and it still reads after the original is dropped. Before refcounting the copy was a
# dead handle -- the failure surfaced later as a bogus "handle type mismatch".
probes += 1
rc, out, err = in_wall('var a = $fs_istream("inside.txt"); var b = a; delete a;'
                       ' return $io_read(b, 0, 4) == $io_read($fs_istream("inside.txt"), 0, 4);')
if rc != 0 or "true" not in out:
    fails.append(f"handles: a copied handle: rc={rc} out={out[:60]!r} err={err[:80]!r} "
                 f"-- the copy must share the stream and outlive the original")
# A handle has no JSON, so a run may not return one: the engine refuses it at the exec exit and the
# kernel surfaces that text -- checked here because this is the first capability that can hand a
# host object out.
probes += 1
rc, out, err = in_wall('return $fs_istream("inside.txt");')
if rc == 0 or "cannot be returned" not in err:
    fails.append(f"handles: returning a handle: rc={rc} err={err[:70]!r} -- want a named refusal")
for d in (wall_root, sib, outside):
    for root, dirs, files in os.walk(d, topdown=False):
        for name in files + [d for d in dirs]:
            pth = os.path.join(root, name)
            if os.path.islink(pth) or os.path.isfile(pth):
                os.unlink(pth)
            elif os.path.isdir(pth):
                os.rmdir(pth)
    os.rmdir(d)

# A child that cannot stop itself is the parent's wall clock's job: the parent kills it and the
# session carries on. Frozen with SIGSTOP, which is exactly that shape -- the engine never gets to
# see an interrupt, because nothing of it is running. (Until I-3 this used a FIFO read; that is a
# named refusal now, which is the better outcome.)
# The child prints as it goes, so the same probe covers the other half: what it printed before it
# froze is in the block, and the parent hands the tail of it back (design.md §4) -- a killed run
# still tells the model how far it got.
probes += 1
talker = 'var i=0; while (true) { $io_print("tick " + string(i) + "\\n"); i=i+1; }'
p = mcp_session([init,
                 {"jsonrpc": "2.0", "id": 21, "method": "tools/call",
                  "params": {"name": "run", "arguments": {"code": talker}}}])
time.sleep(0.3)  # let the initialize child finish: the one left is the run being frozen
victim = None
for _ in range(2000):
    kids = child_pids(p.pid)
    if kids:
        victim = kids[0]
        break
    time.sleep(0.01)
t0 = time.monotonic()
if victim:
    os.kill(victim, signal.SIGSTOP)
p.stdin.write(json.dumps({"jsonrpc": "2.0", "id": 22, "method": "ping"}) + "\n")
p.stdin.close()
out = p.stdout.read()
p.wait(timeout=60)
spent = time.monotonic() - t0
if not victim:
    fails.append("deadline: no child process was seen to freeze")
else:
    if "time budget exceeded" not in out or '"id":22' not in out or spent > 25:
        fails.append(f"deadline: a frozen child must be killed at the deadline -- spent {spent:.1f}s, "
                     f"out={out[:160]!r}")
    # The failure is a tool result, not a protocol error, and it carries the output tail.
    if '"isError":true' not in out:
        fails.append(f"deadline: a killed run must answer as an isError tool result -- out={out[:160]!r}")
    if "the run's last output" not in out or "tick" not in out:
        fails.append(f"deadline: the tail the child printed must come back with the verdict -- "
                     f"out={out[:160]!r}")
    # The whole ring, not a window of it: the reply never writes over the log (block.h), so a run
    # that died ten seconds into printing hands back everything the ring holds.
    if len(out) < 100_000:
        fails.append(f"deadline: the log must come back whole -- the reply is only {len(out)} bytes")

# The reply region: the value is streamed into the block as it is serialized (design.md §4), so a
# value the block can hold comes back whole and one it cannot is refused by name -- never truncated,
# and never after the run has paid for materializing it.
probes += 1
big = 'var s="x"; for (var i=0;i<22;i=i+1) { s=s+s; } return s;'  # 4 MB
p = mcp_session([init, {"jsonrpc": "2.0", "id": 31, "method": "tools/call",
                        "params": {"name": "run", "arguments": {"code": big}}}])
p.stdin.close()
out = p.stdout.read()
p.wait(timeout=60)
if '"isError":false' not in out or len(out) < 4_000_000:
    fails.append(f"reply: a 4 MB value must come back whole -- out is {len(out)} bytes: {out[:120]!r}")

probes += 1
over = ('$io_print("before the flood\\n"); var s="x"; for (var i=0;i<27;i=i+1) { s=s+s; } return s;')
t0 = time.monotonic()
p = mcp_session([init, {"jsonrpc": "2.0", "id": 32, "method": "tools/call",
                        "params": {"name": "run", "arguments": {"code": over}}}])
p.stdin.close()
out = p.stdout.read()
p.wait(timeout=60)
spent = time.monotonic() - t0
if '"isError":true' not in out or "does not fit the block" not in out or spent > 20:
    fails.append(f"reply: a value over the block must be a named failure -- {spent:.1f}s, "
                 f"out={out[:160]!r}")
elif len(out) > 1_000_000:
    fails.append(f"reply: nothing of an over-sized value may be carried -- out is {len(out)} bytes")
elif '"out":"before the flood' not in out:
    fails.append(f"reply: the log must ride along with the failure -- out={out[:200]!r}")

probes += 1
chatty = ('var i=0; while (i<20000) { $io_print("line " + string(i) + "\\n"); i=i+1; } return i;')
p = mcp_session([init, {"jsonrpc": "2.0", "id": 33, "method": "tools/call",
                        "params": {"name": "run", "arguments": {"code": chatty}}}])
p.stdin.close()
out = p.stdout.read()
p.wait(timeout=60)
if '"isError":false' not in out or '"out":"' not in out or len(out) < 200_000:
    fails.append(f"reply: the log must come back whole beside a value -- out is {len(out)} bytes")

# The time window is the caller's: ask for one and the run really gets it (design.md §2). This job
# takes ~7 s -- it dies on the 5 s default, so finishing at all is the proof the knob is wired.
probes += 1
long_job = 'var i=0; while (i<70000000) { i=i+1; } return i;'
t0 = time.monotonic()
p = mcp_session([init, {"jsonrpc": "2.0", "id": 34, "method": "tools/call",
                        "params": {"name": "run",
                                   "arguments": {"code": long_job, "timeout": 20}}}])
p.stdin.close()
out = p.stdout.read()
p.wait(timeout=90)
spent = time.monotonic() - t0
if '"isError":false' not in out or '"result":70000000' not in out or spent > 45:
    fails.append(f"timeout: a 20 s window must let a 7 s job finish -- {spent:.1f}s, out={out[:120]!r}")

# Out of range is refused by name, before any child exists: the range is the deployment's.
probes += 1
for bad in (31, 0, 0.5, "abc"):
    want = "must be 1 to 30"
    p = mcp_session([init, {"jsonrpc": "2.0", "id": 35, "method": "tools/call",
                            "params": {"name": "run", "arguments": {"code": "return 1;", "timeout": bad}}}])
    p.stdin.close()
    out = p.stdout.read()
    p.wait(timeout=30)
    if '"isError":true' not in out or want not in out:
        fails.append(f"timeout: {bad!r} must be refused by name -- out={out[:160]!r}")

# The CLI's own window: `--timeout` must shorten a run (this one takes ~1.2 s and dies at 1) and
# an unusable value must come back in the reserved exit band, not as a run's error id.
probes += 1
t0 = time.monotonic()
p = subprocess.run([ALXRUN, "-e", "var i=0; while (i<30000000) { i=i+1; } return i;", "--timeout", "1"],
                   capture_output=True, text=True, timeout=30)
spent = time.monotonic() - t0
if p.returncode != 1 or "time budget exceeded" not in p.stderr or "1 s timeout" not in p.stderr:
    fails.append(f"cli timeout: rc={p.returncode}, {spent:.1f}s, err={p.stderr[:80]!r} "
                 f"-- want the run cut at its own 1 s window")
for bad in ("0", "31", "abc"):
    p = subprocess.run([ALXRUN, "-e", "return 1;", "--timeout", bad], capture_output=True, text=True,
                       timeout=30)
    if p.returncode != 100 or "must be" not in p.stderr:
        fails.append(f"cli timeout: {bad!r} must be a usage refusal (100) -- rc={p.returncode}, "
                     f"err={p.stderr[:80]!r}")

# The resident material (design.md §6) is what the model reads before writing its first line: it
# has a budget, and the facts a real task measured wrong have to stay in it. Both are pinned, so
# a later trim cannot spend the budget on nothing.
probes += 1
resident = ""
p = mcp_session([init, {"jsonrpc": "2.0", "id": 36, "method": "tools/list", "params": {}}])
p.stdin.close()
out = p.stdout.read()
p.wait(timeout=30)
for line in out.splitlines():
    msg = json.loads(line)
    for tool in (msg.get("result") or {}).get("tools", []):
        if tool.get("name") == "run":
            resident = tool.get("description", "")
if not resident:
    fails.append("resident: tools/list must carry the run description")
else:
    if len(resident) > 3072:
        fails.append(f"resident: the run description is {len(resident)} chars -- over its 3072 budget")
    pins = ["string(int,n)=base", "$vec_sort: nested left alone", "$regex_match: the whole",
            "$json_str for containers", "inf/nan->null, int>2^53->float",
            "byte-wise $str_*, [], regex", "in-place caps need a bare variable",
            "x[a,b]=v same kind"]
    if OVERFLOW_ON:
        pins.append("int overflow: throws")
    elif "int overflow" in resident:
        fails.append("resident: the overflow fact outlived its switch")
    for fact in pins:
        if fact not in resident:
            fails.append(f"resident: {fact!r} fell out of the material")
    # The wall is compiled in, so the material has to say what it resolved to: a placeholder left
    # in the text is one the model cannot turn into a directory.
    if "fs rules: " not in resident:
        fails.append("resident: the fs rules line fell out of the material")
    elif "{cwd}" in resident or "{tmp}" in resident or "{home}" in resident:
        fails.append("resident: the fs rules line carries an unexpanded placeholder")

# Same rot-guard as the doc tables: a probe row deleted by accident thins the suite in
# silence, and this is the suite whose silence would be read as "the doors are shut".
MIN_PROBES = 43
if probes < MIN_PROBES:
    fails.append(f"only {probes} gate probes ran -- rows were lost (floor {MIN_PROBES})")

# The demo is the resident material, so it uses the pre-bound data channel --
# run it the way the host would (design.md §7).
demo = alxrun("--demo").stdout
demo_data = alxrun("--demo-data").stdout.strip()
rc, _, ret, err = run_code(demo, demo_data)
if rc != 0 or ret is None:
    fails.append(f"demo did not run: rc={rc} err={err}")

# The cases' reference solutions must still produce what was recorded. That pair is what judging
# leans on (a model's answer goes to a model -- its shape varies legitimately; our own artifacts go
# to nobody but this file), and it rots silently when engine semantics move. Zero tokens, which is
# exactly why it lives here and not in a paid Claude session.
cases = 0
for case in sorted(os.listdir("cases")):
    judge = os.path.join("cases", case, ".judge")
    sol, rec = os.path.join(judge, "solution.axc"), os.path.join(judge, "solution.out")
    if not (os.path.exists(sol) and os.path.exists(rec)):
        continue
    data = os.path.join(judge, "data.json")
    args = [sol] + (["--data", open(data).read()] if os.path.exists(data) else [])
    p = alxrun(*args)
    got = next((l[len("[ret] "):] for l in reversed(p.stdout.split("\n")) if l.startswith("[ret] ")), None)
    want = open(rec).read().strip()
    cases += 1
    if p.returncode != 0 or got != want:
        fails.append(f"cases/{case}: solution.axc no longer produces solution.out "
                     f"(rc={p.returncode} got={got!r})")

if fails:
    print(f"FAIL -- {len(fails)} problem(s), {checked} examples checked, {cases} cases")
    for f in fails:
        print(f"  {f}")
    sys.exit(1)

print(f"PASS -- {checked} examples asserted, {len(forms)} forms, {cases} cases, "
      f"{probes} gate probes, demo runs")
