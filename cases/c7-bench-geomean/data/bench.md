# Script Benchmark -- definition (excerpt)

> An excerpt: the definition part only (test environment / method / usage weights); the result
> table and the maintenance notes are not included in this file.

## Test environment

- **Compiler**: g++ C++17 `-O3 -march=native` (the library and the bench must use the same compiler + flags)
- **CPU**: i3-1315U (Raptor Lake, 2P+4E)
- **OS**: Arch Linux

## Method

- **Interpreter only**: `compile()` once (untimed), `reset()` (untimed), sample `exec(compiled bytes)`
  -- no lexing/parsing, and no engine state re-initialization
- **Two-level round sampling**: one round = each of the 12 cases measured once (5 samples per case
  within the unit, IQR); isolation between rounds; M rounds in all, then IQR over each case's M
  round values; GEOMEAN computed last
- **Validation**: the script's top-level expression value is checked against the native result or
  known constants; a mismatch reports MISMATCH
- **Comparison basis**: each cell holds the two raw microsecond values of `script/native`; **no
  ratio is recorded**
- **Weighted GEOMEAN -- a (all workloads)**: `exp(Σ wᵢ·ln rᵢ / Σ wᵢ)`, all 12 cases participate
  (bridge included). Positioning: ex is the heavy-compute / multithreaded / IO body; bridge weight
  40% (ex_echo 30 + ex_call0 10), glue 45%, control flow / numerics 15%
- **Weighted GEOMEAN -- b (lightweight compute scripts)**: the same formula, non-ex cases only
  (the ex cases weigh 0). Positioning: pure script tasks that do not depend on ex (small tools /
  algorithm scripts) -- func_call 20%, int_loop 15%, loop control 20%, recursion 15%, data/text 22%
- **a/b format**: GEOMEAN is printed as `a/b` (all-workload weighted / pure-script weighted); **both
  weight sets must total 100**

**Usage weights (a = all workloads / b = pure script)**:

| Case | a | b | | Case | a | b |
|------|---|---|-|------|---|---|
| ex_echo | 30% | 0% | | vec_access | 5% | 7% |
| ex_call0 | 10% | 0% | | foreach | 5% | 10% |
| func_call | 15% | 20% | | int_loop | 5% | 15% |
| map_access | 10% | 10% | | while_count | 5% | 10% |
| string_concat | 5% | 5% | | float_arith | 5% | 8% |
| fib(28) | 3% | 8% | | fib_tail | 2% | 7% |
