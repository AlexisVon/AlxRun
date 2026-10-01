`data/bench.md` is the maintainers' definition for the script bench, and `data/runs.csv` holds its
result rows: each row is `date, commit`, followed by 12 cells holding the two microsecond values of
`script/native` (**not a ratio**), in this column order:

```
ex_call0 ex_echo func_call int_loop fib(28) fib_tail while_count float_arith vec_access foreach map_access string_concat
```

Following the definition in `bench.md` (how each rᵢ is derived, which cases count as ex -- all of it
lives in that document), compute the **unweighted** geometric mean of every row, one output line per
row:

`date commit | all12=... noex10=...`

- all12 = the equal-weight geometric mean of the 12 rᵢ; noex10 = the equal-weight geometric mean
  after dropping the two ex cases
- both rounded to one decimal
