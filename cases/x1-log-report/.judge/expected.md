["/api/orders 9 15.00%","/api/search 7 23.33%","/api/users 5 12.50%"]
report_sha256 400a4121fea31d9081ba9cdde3fa5dd6977ebb5dea5a52977f62bfa2aae1fef1

(top 3 by 5xx count, descending: /api/orders 9/60 = 15.00%, /api/search 7/30 = 23.33%,
/api/users 5/40 = 12.50%. The digest is over the three lines written to `bin/x1-report.md`, each
followed by a newline; any shape carrying these three lines and that digest counts. Beware
`/api/v500/list` -- its name contains "500" but it served a single 5xx.)
