# What this case plants

- **No CSV parser exists, and `$str_split(text, ",")` is wrong here**: five of the eight rows carry
  their comma inside a quoted owner field, so the naive split shifts every later column. The right
  way is a hand-written single-pass state machine (an in-quotes toggle, `""` = one literal quote).
- **The data arrives through the `data` channel** (the `csv` variable is pre-bound), the c1 pattern.
- The cost values are strings: `float()` first; the `0.0` seed in `$map_get` keeps the running sum
  a double (the c5 trap).
- The descending sort is hand-written again (c4/c5); the averages want `string(x, 2)`.
