# What this case plants

- The data is a **JSON string** (not a ready structure) → `$json_parse` comes first.
- **Sorting by the total** has no ready API either (same as c4: `$vec_sort` does not look at map
  values) → the comparison is hand-written.
- The amounts are decimals: the `0.0` in `$map_get(..., 0.0)` is what makes the sum a double --
  writing `0` turns `string(x, 2)` into a **binary output**.
