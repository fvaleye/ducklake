## v5 defects, most serious first (claim, then evidence, then fix)

1. **The token breaks older 1.1-dev1 binaries in more places than conflict checks.**
   - Evidence (probe `challenge_v5/tok_probe.sql`, `,data_changed_table:1` added to snapshot 22):
     - `snapshots()` fails for the whole catalog, even with `WHERE snapshot_id < 5`. It parses every row (`ducklake_snapshots.cpp:96`).
     - `expire_snapshots` fails on the marked snapshot. It reuses the same parser (`ducklake_expire_snapshots.cpp:77`).
     - `challenge_v5/root/test/probe/tok_race.test` passed all 14 assertions: a commit to a different table fails on retry with `Unsupported change type` (`transaction_state.cpp:2582,2548`).
   - Review Focus #1 ("fail at attach") contradicts D3 option A (stay on 1.1-dev1), and so does the Global Constraint at line 33.
   - Fix: the column pivot, or a forced 1.1-dev2 bump.
2. **The D2 cutoff turns other writers' DML into "maintenance".**
   - datafusion-ducklake writes only `(snapshot_id, changes_made)` (`metadata_writer_duckdb.rs:1137`, `metadata_writer_postgres.rs:2580`). pg_ducklake writes named columns (`native_inline_writer.cpp:850`).
   - Both also reject the token: datafusion-ducklake at `snapshot_changes.rs:55`, and pg_ducklake calls DuckLake's own `ParseChangesMade` (`:263`).
   - Fix: a nullable column where NULL means unknown.
3. **M3 misses existing 1.1-dev1 catalogs.** They evolve in place through `MigrateV10Dev` (`ducklake_initializer.cpp:272-284`), which is neither the new-catalog path nor the 1.0 migration path, so the key would be missing. Fix: drop M3.
4. **M1's test cannot fail or pass as described.**
   - `snapshots()` is `ducklake_snapshot LEFT JOIN ducklake_snapshot_changes` (`metadata_manager.cpp:5010-5012`). Probe `m1`: a changes row inserted by hand shows 0 rows and raises no error.
   - That leftover row also breaks the next commit: primary-key collision, then a retry that hits `Unsupported change type`.
   - The precedent the plan cites actually UPDATEs snapshot 0 (`schema_path_changes.test:35`).
5. **M1 names the wrong struct.** `hpp:38-44` is `TransactionChangeInformation`. Parsing needs `SnapshotChangeInformation` (`:50-71`).
6. **M2's grep misses most affected tests.** It finds 4 test files, but 10 assert exact maps. Use `rg -l "(tables_inserted_into|tables_deleted_from|inlined_insert|inlined_delete|flushed_inlined)=\["` (it adds, for example, `data_inlining_flush.test` and `ducklake_snapshots.test`).
7. **The token conflicts with the spec's stated purpose.** The live spec page says the table exists "for easier conflict detection" (ducklake-web `3d71ee83`), yet v5's token is read by no conflict check. Maintainers rejected #1029 partly as a "double-source of truth".
8. **Minor fixes:**
   - U1 is not a prerequisite, because `Count()>0` already leaves net-zero snapshots unmarked.
   - `transaction.cpp:1641` is now 1640.
   - `WriteSnapshotChanges` already copies the state's `tables_deleted_from` (`transaction_state.cpp:391`). The marker must union it, not re-add it.

## Verdict: the column beats the token

| | Token | Column `data_changes` |
|---|---|---|
| (a) Older binary, DuckDB/SQLite metadata | `snapshots()`, expire and retries fail (probes) | Every commit fails: "has 6 columns but 5 values were supplied" (`col_probe.sql`, sqlite3). `snapshots()` and expire still work |
| (a) Older binary, Postgres metadata | Same failures | Keeps committing and writes NULL (psql probe, raw `postgres_execute` at `postgres_metadata_manager.cpp:338`) |
| (b) Third-party parsers | datafusion-ducklake and pg_ducklake fail | Unaffected (they select named columns) |
| (c) Downstream freshness query | Unaffected (token is appended) | Unaffected, and can filter `data_changes <> ''` without parsing |
| (d) Conflict detection | Must be ignored | Untouched (`STRING_AGG(changes_made)`, `:4567`) |

Precedent for the column:
- 1.1-dev1 already took 7 column additions and a table without a version bump (`:447-454`), with the same positional-insert exposure (`:4279/:4312`; inferred, not run on an old binary).
- `author`, `commit_message` and `commit_extra_info` were added to this same table and to `snapshots()` output (`:372-374`, commit `3738c695`).

**Q3:** the column needs no cutoff, because NULL is the default for old rows and for other writers. Expose it as a new `snapshots()` output column `data_changes MAP` parsed with the existing `ParseChangesMade`: NULL means unknown and `{}` means recorded with no data change.
- `expire_snapshots` gets the column too (`ducklake_expire_snapshots.cpp:22`).
- Write it with `SQLString`, not `SQLStringOrNull` (`:537-541`), or '' collapses into NULL.
- Putting it as a key inside `changes` would churn the 10 exact-map tests, and `PushIDChangeList` skips empty sets.

**Q4 ranking:**
1. Column on `snapshot_changes`: exact, small spec footprint, strong precedent.
2. Token: exact, but breaks the ecosystem and needs the cutoff.
3. Separate table: old binaries silently skip it, so absence is ambiguous, and the spec footprint is larger.
4. `operation` column on `ducklake_snapshot`: inexact for mixed snapshots (probes4/mixed), and its insert is positional too (`:4534`).
5. `commit_extra_info`: rejected, it is user-owned and downstream consumers read it.

Reclassifying tokens and deriving from metadata stay rejected, per v5's evidence.

**Q5:**
- U1 first is fine as goodwill, but it is not a gate.
- Draft the ducklake-web spec fix alongside D0, not as a gate. The live page still lists `compacted_table` and omits 5 tokens DuckLake writes.
- The roadmap does put incremental MVs in 2.0 (`2026-04-13-ducklake-10.md:157`). Leading with that invites a 2.0 deferral, as happened to #1429. Lead instead with the 1.1 fit: 1.1-dev1 already takes additive changes beyond its roadmap.

## v6 task changes

- **Remove:** M1 (token parse), M3 (cutoff), the D1/D2 questions, and Review Focus #1 as written.
- **Add C1 (schema):**
  - Append to `V1_1_DEV1_ADDED_COLUMNS`, which brings the attach-time check and dev migration along.
  - Add a `GetSnapshotChangesTableStatement` override in `ducklake_metadata_manager_v1_1.cpp`.
  - Make the snapshot-0 insert (`:223`) version-aware and write ''.
  - Read the column in `GetAllSnapshots`, gated by `supports_v1_1_metadata`.
- **Change M2:**
  - Give `WriteSnapshotChangesSql` an explicit column list.
  - Compute logical inserted/deleted sets in `AddTableChanges`, and union the state's `tables_deleted_from`.
  - Update the two positional fixtures: `ducklake_snapshots_chunked.test:24` and `quack/server_side_commit_atomicity.test:32`.
  - Tests: old rows are NULL after migrating both a 1.0 and an existing 1.1-dev1 catalog; a DDL-only commit writes ''; a 1.0 catalog keeps 5 columns.
- **Add C2:** the `snapshots()` output column.
- **M4:** use `data_changes IS NOT NULL` instead of the cutoff.
- **D0 asks:** the column name, NULL vs '' semantics, reusing the token grammar, and staying on 1.1-dev1 vs bumping (old DuckDB/SQLite dev binaries fail on commit).

## Not verified

- No real older binary was run; the current binary stood in for "old".
- Quack's handling of short positional inserts.
- How the brikk Trino writer inserts rows.
- How many tests a new output column breaks (most expire calls use `statement ok`).
- Whether maintainers will accept a spec change.

Probe catalogs and scripts are in `scratchpad/challenge_v5/` (`col`, `tok`, `m1`, `root`, `pg`).
