# What this case plants

- **It is the only sample in the "own use" tier**: it replays the "read a file and dig the
  definition out yourself" situation -- the task states no definition; how rᵢ is derived and which
  cases are ex all live in `data/bench.md` (the real session read exactly such a file).
- **The task wants only the two unweighted numbers** (corrected to the truth on 2026-09-18): the
  real task was "compute the unweighted geomean"; the weighted value is a **ready column** of the
  original table (GEOMEAN a/b) that needs no computing. This fixture's result table is cut; the
  weight table in `bench.md` **stays** -- the real definition had it, and here it is a distractor.
- ⚠️ **The weight table can screen "not ex", but only via the b column**: in the a column ex holds
  30/10 (non-zero), so screening `w > 0` with a drops nothing and `noex10` would equal `all12`
  (history: the original session's run1 got it wrong exactly this way, run2 fixed it). The per-case
  a|b table and the percentages in the b prose (func_call 20%, int_loop 15%, loop control 20%,
  recursion 15%, data/text 22%) check each other -- **b is an independent column, not a zeroed a**.
- **Each cell is the two raw values `S/N`, not a ratio** -- the header says "no ratio recorded".
  Using S directly as the ratio gets all six rows wrong while looking plausible.
- **Geometric mean = `exp(mean(ln rᵢ))`**: writing the arithmetic mean is the most natural mistake;
  in the engine that is `$math_ln` + `$math_exp`.
- **The trailing blank line**: `$str_split(text, "\n")` leaves one `""` at the end (stripping with
  `$str_trim(text, " \n")` first is safest).
- One decimal: `string(x, 1)`.
- **Judging tolerance ±1**: the GEOMEAN column was computed from the original µs values, while the
  cells transcribed into the table are already rounded to `8891/91`, so its precision cannot be
  reproduced (first row: 194/297 in the table vs 193.0/293.4 recomputed from the 12 cells) -- hence
  the judge works on input → output, not on the historical column.
