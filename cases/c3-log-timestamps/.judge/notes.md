# What this case plants

- **`$regex_match` matches the whole string** (not a substring): using it for "the line contains
  ERROR" returns a **silent empty result** (no error) → the right way is `$regex_find` /
  `$regex_find_all`.
- The task gives a relative path; an absolute path in the script is a **blemish** (the script stops
  being portable) but does not change the answer.
- `$fs_read` reads the whole file; `$str_split(text, "\n")` leaves a trailing empty string.
