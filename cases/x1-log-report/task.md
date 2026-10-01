cases/x1-log-report/data/access.log is an nginx-style access log, one request per line; the status
code is the three-digit number right after the quoted request.

Produce an error report over the whole log:

- for every path that served at least one 5xx status: its 5xx count (`errors`) and its error rate
  -- errors / that path's total requests, as a percentage rounded to two decimals;
- the report is the top 3 paths by `errors`, descending (ties broken by path, ascending), one line
  per path, written `path errors rate%`;
- write those three lines to `bin/x1-report.md` (each line followed by a newline), then return a
  map: {"rows": [the three lines], "report_sha256": the sha256 of the file you wrote}.
