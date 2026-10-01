`csv` holds a raw CSV text: a header row, then data rows. A field may be wrapped in double quotes;
a comma inside a quoted field belongs to the field, and a doubled quote ("") inside a quoted field
is one literal quote character.

For every tier (the `tier` column) compute the row count, the total `cost` and the average `cost`;
return one line per tier, `tier count total avg`, total and average rounded to two decimals,
ordered by total, descending.
