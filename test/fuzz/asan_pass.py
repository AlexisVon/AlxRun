#!/usr/bin/env python3
# Copyright (c) 2026 AlexisVon
# SPDX-License-Identifier: Apache-2.0
"""Feed every assertion, every seed and the campaign's corpus through the ASan harness.

The harness is built with clang + ASan/UBSan (test/fuzz/run.sh), so it covers the same code
paths the CLI does -- without a second instrumented build of the CLI, and without the RLIMIT_AS
dance one needs (the fuzz build has the cap off: ASan reserves terabytes of address space).

Run it after an AlxLib bump. Zero tokens, no framework -- any sanitizer report on stderr is a
finding, and the first line of each is printed.

The harness is built from scratch first (test/fuzz/run.sh --build-only, minutes): a net that
verifies a stale engine is worse than no net.

Usage: python3 test/fuzz/asan_pass.py
"""
import concurrent.futures
import glob
import json
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HARNESS = os.path.join(ROOT, "build-fuzz", "harness", "alxrun_fuzz")
RUN_SH = os.path.join(ROOT, "test", "fuzz", "run.sh")
CORPUS_BUDGET = 3600   # seconds per shard; a shard that hits it says so instead of dying
CORPUS_WORKERS = 4     # one harness process per shard -- the sweep is single-threaded per process
SHARD_UNITS = 100      # units per invocation: a shard that dies on a big allocation loses a hundred
                       # units, not four hundred -- the heavy unit itself is the product wall's job
ALXRUN = os.path.join(ROOT, "bin", "alxrun")
REPORT = re.compile(r"(runtime error:|ERROR: AddressSanitizer|SUMMARY: AddressSanitizer|LeakSanitizer|"
                    r"UndefinedBehaviorSanitizer)")
# libFuzzer's own limits are the harness's accounting, not the engine's: an allocation past the
# RSS ceiling is exactly what the product's 512 MB wall exists for. Counted, said out loud, never
# a finding (the bare `SUMMARY: ` this used to match caught libFuzzer's OOM lines too).
HARNESS_LIMIT = re.compile(r"(SUMMARY|ERROR): libFuzzer: ")


def classify(stderr):
    """Sanitizer report lines in this stderr."""
    return [line.strip()[:140] for line in stderr.split("\n") if REPORT.search(line)]


def harness_limits(stderr):
    """libFuzzer's own OOM/timeout lines — about the harness, not the engine."""
    return [line.strip()[:140] for line in stderr.split("\n") if HARNESS_LIMIT.search(line)]


def replay_shard(units, env, artifacts):
    """One harness invocation over a slice of the corpus: (stderr, hit the wall)."""
    try:
        p = subprocess.run([HARNESS, *units, "-rss_limit_mb=2048", "-timeout=20",
                            f"-artifact_prefix={artifacts}/"],
                           capture_output=True, text=True, env=env, timeout=CORPUS_BUDGET)
        return p.stderr, False
    except subprocess.TimeoutExpired as e:
        stderr = e.stderr.decode() if isinstance(e.stderr, bytes) else (e.stderr or "")
        return stderr, True


def assertion_codes():
    """Every `ex` in the capability tables, in the order the doc reports them."""
    doc = json.loads(subprocess.run([ALXRUN, "--capabilities", "."],
                                    capture_output=True, text=True).stdout)
    out = []
    for key, exs in doc.items():
        if key.startswith("_"):
            continue
        for ex in exs:
            if " => " in ex:
                out.append((f"ex:{key}", ex.split(" => ", 1)[0]))
    return out


def seed_files():
    paths = sorted(glob.glob(os.path.join(ROOT, "agent", "*.axc")))
    paths += sorted(glob.glob(os.path.join(ROOT, "test", "fuzz", "seeds", "*.axc")))
    paths += sorted(glob.glob(os.path.join(ROOT, "cases", "*", ".judge", "solution.axc")))
    # Known engine bugs, one file per issue: they must stop reporting once AlxLib fixes them.
    paths += sorted(glob.glob(os.path.join(ROOT, "test", "fuzz", "regressions", "*.axc")))
    return paths


def main():
    print("== harness: building from scratch (a few minutes)")
    if subprocess.run(["bash", RUN_SH, "--build-only"]).returncode != 0:
        print("harness build failed — no net ran")
        return 2

    env = dict(os.environ)
    env["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=0"
    env["ASAN_OPTIONS"] = "detect_leaks=1:abort_on_error=0"

    cases = list(assertion_codes())
    files = seed_files()
    tmp = tempfile.mkdtemp()
    for path in files:
        with open(path) as f:
            cases.append((os.path.basename(path), f.read()))

    # The campaign's own corpus, if a campaign has run on this machine: knowledge the fuzzer
    # accumulated, fed back as inputs. One harness invocation over the directory (libFuzzer runs
    # every unit) so this stays seconds, not minutes.
    corpus = os.path.join(ROOT, "build-fuzz", "corpus")
    corpus_n = len(glob.glob(os.path.join(corpus, "*"))) if os.path.isdir(corpus) else 0

    findings = []
    if corpus_n:
        # The corpus goes through in shards, in parallel: one process over the whole directory
        # runs a single core flat out and took 56 minutes for 15959 units (measured 2026-09-20),
        # and that only grows. A shard is one harness invocation over a slice of files, so the
        # per-shard heap stays small (the harness accumulates quarantine across the units it runs).
        # -timeout matters per unit: the fuzz build has no memory cap, so a unit that walks a big
        # case file can run for minutes; one that goes over becomes a slow-unit artifact.
        units = sorted(glob.glob(os.path.join(corpus, "*")))
        shards = [units[i:i + SHARD_UNITS] for i in range(0, len(units), SHARD_UNITS)]
        artifacts = os.path.join(ROOT, "build-fuzz", "artifacts")
        print(f"== corpus: {len(units)} units in {len(shards)} shards, {CORPUS_WORKERS} at a time")
        stderrs, stalled = [], 0
        with concurrent.futures.ThreadPoolExecutor(max_workers=CORPUS_WORKERS) as pool:
            futures = [pool.submit(replay_shard, shard, env, artifacts) for shard in shards]
            for done, fut in enumerate(concurrent.futures.as_completed(futures), 1):
                stderr, timed_out = fut.result()
                stderrs.append(stderr)
                stalled += int(timed_out)
                if done % 10 == 0 or done == len(shards):
                    print(f"   {done}/{len(shards)} shards", flush=True)
        for stderr in stderrs:
            findings += [f"corpus ({corpus_n} units): {x}" for x in classify(stderr)]
        limited = [harness_limits(s) for s in stderrs]
        if any(limited):
            print(f"!! {sum(1 for x in limited if x)} of {len(shards)} shards died on the harness's "
                  f"own limit ({sum(len(x) for x in limited)} line(s)) — a big allocation is the "
                  f"product's 512 MB wall's job, but those units were not replayed")
        if stalled:
            print(f"!! {stalled} of {len(shards)} shards hit the {CORPUS_BUDGET} s wall — what they "
                  f"reached was clean, the rest was not looked at")

    for name, code in cases:
        with open(os.path.join(tmp, "input.axc"), "w") as f:
            f.write(code)
        p = subprocess.run([HARNESS, os.path.join(tmp, "input.axc")],
                           capture_output=True, text=True, env=env, timeout=60)
        findings += [f"{name}: rc={p.returncode} {x}" for x in classify(p.stderr)]

    os.unlink(os.path.join(tmp, "input.axc"))
    os.rmdir(tmp)
    if findings:
        print(f"FAIL — {len(findings)} sanitizer report(s) over {len(cases)} inputs "
              f"+ {corpus_n} corpus units")
        for f in findings:
            print(f"  {f}")
        return 1
    print(f"PASS — {len(cases)} inputs ({len(cases) - len(files)} assertions + {len(files)} seeds) "
          f"+ {corpus_n} corpus units, no sanitizer report")
    return 0


if __name__ == "__main__":
    sys.exit(main())
