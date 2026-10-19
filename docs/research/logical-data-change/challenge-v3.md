**v3 adversarial review: ranked defects, each backed by an executed probe.** Probes are in `scratchpad/challenge_v3/` (`run.sh`, `fresh.sql` = the A1 query shape, `p1..p14.sql`, `u1.sh`). Binary: `ducklake-table-data-change-helper/build/release/duckdb`.

**1. A1 returns the user's values for tables with a `snapshot_id` or `rowid` column (severe, silent).**
- Attacked: A1 Step 3, "`ducklake_table_changes(...) AS c(change_snapshot_id)` … proven".
- p7: table `"o'q"(snapshot_id, …)` with value 1, inserted at snapshot 5. The query reports `last_snapshot_id 1`.
- p14: `u(snapshot_id, rowid)` with insert at 2 and delete at 3. The plan shape returns `1`.
- Cause: the macro joins on `i.snapshot_id`/`i.rowid` (`src/functions/ducklake_table_changes.cpp:9-21`), and DuckDB binds the user's columns before virtual columns. The upstream fix is open PR #1723 (opened 2026-10-13), not merged.
- The port source already avoids this. It uses insertions and deletions with positional aliases (in the dbt adapter's freshness query), and p14 shows that form returns 2 and 3 correctly.
- **Change:** keep the port's shape, or gate A1 on #1723 landing in the DuckLake version the consumer runs.

**2. A gap in history makes A1 report an older change (silent).**
- Attacked: Step 1, "`min(snapshot_id)`", plus the inner join to `ducklake_snapshots`.
- p6: insert at 2, insert at 3, delete at 4, then `expire_snapshots(versions => [3,4])`. The query reports 2; the truth is 4. The feed still returns rows at 3 and 4, and the inner join drops them because those snapshots are gone.
- The port handles gaps with its baseline logic (`catalog.sql:55-62`).
- **Change:** LEFT JOIN, take `max(change_snapshot_id)`, and raise D2 when that snapshot is no longer retained.

**3. A1 contradicts the dbt adapter's own tests, and one bad source breaks the whole batch.**
- `pr45.diff:288` expects an empty table to report its creation time. A1 raises an error instead (p2 `s4`: 0 rows).
- `pr45.diff:302-321` expects a non-DuckLake catalog to read as year 1 without breaking the batch. `ducklake_snapshots()` throws on such a catalog (`base_metadata_function.cpp:85-87`).
- p11: a view as a source fails the whole UNION ALL ("is a View, not a table"). A missing table is unverified (the script stopped at the view error).
- The port guards against all three with `is_ducklake` and a `duckdb_tables()` join (`catalog.sql:48,70-84`).
- **Change:** keep those guards, and define what an empty or never-changed table should report before writing D2.

**4. The A0 fallback gate is wrong.**
- Inlining defaults to 10 rows (`ducklake_catalog.cpp:1247`). Every table therefore gets a `ducklake_inlined_data_<id>_<ver>`, even when its insert went to a file (p9), so the fallback covers almost nothing.
- When a table does have no inlined-data row, inlined deletes still land in `ducklake_inlined_delete_<id>`, which is not listed (p10: created with the session default at 0, then a delete with the default at 10). File endpoints give 2; the truth is 3.
- `metaonly.sql:3-5` also parses `changes_made` with `split_part`, which breaks the plan's own no-parsing constraint.
- **Change:** gate on both inlined-table families and use the `changes` map, or drop the fallback.

**5. U3's pushdown description is wrong, and it won't help Postgres yet.**
- With `filter_pushdown`, DuckDB moves the filters into the scan and drops them from the plan (`duckdb/src/optimizer/pushdown/pushdown_get.cpp:152-166`). It only re-adds a filter for columns where `supports_pushdown_type` returns false (`plan_get.cpp:114-166`).
- So "keep every other filter for DuckDB" needs `supports_pushdown_type`. Every filter kind on a pushed column (IN, OR, IS NULL, expression) must then be applied by the function.
- On Postgres, metadata SQL runs through postgres_scanner with `pg_experimental_filter_pushdown=false` (`ducklake_transaction.cpp:859-864`). The WHERE still reads whole tables, so the 35 MiB figure doesn't carry over.
- Open PR #1755 shows the scanner drops TIMESTAMPTZ offsets. Any rendered time filter must use `FormatTimestampISO8601`, as expire does (`ducklake_expire_snapshots.cpp:63-70`).
- Open PR #1756 edits `ducklake_snapshots.cpp` and `GetAllSnapshots`, so it will conflict.
- A1's step 2 has no constant filter on `snapshots()`, so U3 doesn't speed up the dbt adapter's query.
- What is safe: moving the work from bind to init. `SetAlwaysRequireRebind` already re-binds on every execution (`base_metadata_function.cpp:69-72`), and the return types don't depend on data.

**6. Merge odds.**
- U1 is the most mergeable item. I found no crash and no wrong snapshot on the patched build across 7 cases (`u1.sh`): MERGE delete plus insert, DELETE with a self-subquery, local and committed rows deleted in one statement, a net-zero transaction followed by ADD COLUMN and an insert, CREATE TABLE in the same transaction, and UPDATE twice then DELETE. Main shows the extra snapshots.
- The double-call path isn't reachable through MERGE: DuckLake rejects a DELETE+UPDATE MERGE ("only supports a single UPDATE/DELETE action"). `FlushDelete` runs once per file in Finalize (`ducklake_delete.cpp:592-601`).
- No Track U item is a spec change.
- The Discussion: #1429 was closed as "a feature and must be discussed beforehand, possibly for DuckLake 2.0", #1703 as "discuss after the next release", and #1029 got "not the direction we want". Questions 2 and 3 will likely be deferred. Question 1 is already answered by #1589. Issue #199 (closed) is earlier work on "last table version" and should be cited.

**What survived (A1 gave the correct answer):**
- Partial deletes across snapshots: s1 → 6.
- Delete-all on a file table → 9.
- Zero-row DELETE and TRUNCATE create no snapshot.
- `ducklake_add_data_files` with rows: correct. With 0 rows: no snapshot.
- Rename then insert → 20.
- ADD COLUMN with DEFAULT then UPDATE → 25.
- Flush and DELETE in one transaction, in both orders: correct.
- Flush of inlined deletes produces no feed rows.
- Rewrite then merge → 4.
- Compaction plus DML in one transaction is rejected (`ducklake_transaction_state.cpp:421-423`), so the per-snapshot rewrite filter is safe.
- After `older_than` expiry, the oldest retained id is accepted as the start, even for tables created before it. Start 0 then errors with "No snapshot found at version 0".

**Cost (A0):** p12 scans all 20M rows twice for one table (0.19 s locally). Cost grows with table size, not with how many changes there were, so A0 should state the table sizes it measures.

**Verdict:** v3 is not the best path as written. Defects 1–3 are regressions against the macro that A1 says to port. The one change that would help most: port `catalog.sql:47-126` as it is (insertions and deletions with positional aliases, the `is_ducklake` and `duckdb_tables()` guards, gap handling), then add the plan's new tests on top. Drop the rewritten `table_changes` shape. After that, put U1 first and hold U3 until #1755 and #1756 settle.

Not checked: Postgres, SQLite and Quack backends (I only ran the DuckDB file backend). Also not checked: the step-1/step-2 race with a concurrent expiry (which would fail the batch rather than give a wrong answer), whether the consumer sets `METADATA_SCHEMA` and what its retention is (both matter for how often D2 fires), and Value::ToString rendering for TIMESTAMPTZ.
