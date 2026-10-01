# What this case plants

- **There is no `$str_pad`** → the alignment is hand-written (a loop appending spaces).
- The boundary: a key already >=12 wide is not padded, the value follows immediately.
- The data mixes strings and number-looking values (`30`); whether to convert is up to the task
  (this one does not ask for it).
