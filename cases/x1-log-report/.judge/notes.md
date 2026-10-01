# What this case plants

- **The status must be parsed, not substring-matched**: `/api/v500/list` carries "500" in its path,
  so a `$str_contains(line, "500")` filter counts that whole path as errors while 502/503 lines
  slip out of the count -- wrong in both directions at once, and it still looks plausible.
  `$regex_group` pulls path and status out of every line in one pass.
- **The write is pinned into the answer**: the returned map carries the sha256 of the report file,
  so a solution that skips the write cannot produce the recorded value.
- The aggregation and the descending sort are hand-written (maps have no sort; same as c4/c5).
- The rates want `string(x, 2)`.
- It is the first case to exercise the regex family, `$fs_write` and `$fs_sha256` together; the
  report lands in `bin/` (gitignored, and it exists once the binary is built).
