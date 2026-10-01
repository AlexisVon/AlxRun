# What this case plants

- **The data channel**: the readings sit in the task text (like something a user pasted). The right
  way is to hand them to `run`'s `data` argument (pre-bound) rather than inlining them in the code --
  that is what §7 measures, and the task deliberately gives no hint.
- Two decimals: the right way is the builtin `string(d, prec)`; it is **not in the resident list**
  (the list shows only the first form, `string(v)`), so a weak tier falls back to
  `$math_floor(x*100)/100`, whose first try returns a dirty float (`92.94000000000001`) and costs
  one more call.
