`cfg` is a key=value config text, one entry per line:

region=us-east
owner=platform
retention_days=30
alert_to=oncall@example.com

Parse it and output a two-column aligned table: the key left-aligned, padded with spaces to a
width of 12, then the value.
