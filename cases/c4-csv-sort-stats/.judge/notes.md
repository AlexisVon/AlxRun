# What this case plants

- **There is no ready API for sorting by a column**: `$vec_sort` on nested vecs is **silently a
  no-op** (returned unchanged, no error) -- a model that calls `$vec_sort(rows)` gets "unsorted,
  no error".
- **String sorting is lexicographic**: what CSV yields are strings, and "99.95" vs "97.10" sorts
  97.10 first lexicographically → forgetting `float()` sorts wrong and still looks plausible.
- The trailing empty string (`$str_split(text, "\n")` leaves a `""` at the end) and the header row
  must be skipped.
- The mean wants `string(x, 2)`; **if x is an int, the second argument is a base, not a decimal
  count** (a trap).
