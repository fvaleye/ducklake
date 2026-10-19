# DuckLake Data-Change Marker Implementation Plan (v6, 2026-10-14)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every DuckLake commit records, per table, which tables' data it logically inserted into or deleted from, as distinct from maintenance. This is the DuckLake equivalent of Delta's `dataChange` and Iceberg's `operation`, so readers stop guessing from physical change types.

**Architecture:**
- **Storage:** a nullable column `data_changes VARCHAR` on `ducklake_snapshot_changes`. It holds logical entries in the grammar DuckLake already parses (`inserted_into_table:<id>`, `deleted_from_table:<id>`).
- **Values:** `NULL` means "not recorded" (old rows, older or third-party writers); `''` means "recorded, no data change".
- **Computation:** at commit, in `DuckLakeTransaction::AddTableChanges`, the path both client and server-side commits use.
- **Reading:** `snapshots()` exposes it as a new `data_changes` MAP column.
- **Version:** added in the existing `1.1-dev1` column migration, like seven other columns before it.
- **Untouched:** conflict detection and the existing `changes_made` string.

**Tech Stack:** C++ DuckDB extension (clang-format 11.0.1), sqllogictest.

**Spec:** this file. Evidence was executed on builds of main (`e12454fc`, `900cf2f9`) unless marked otherwise. Probes live under `scratchpad/`.

## Evidence behind v6

| Question | Answer | Evidence |
|---|---|---|
| Is anyone doing this? | No upstream work. At least six consumers each guess, differently | No issue/PR/Discussion/branch upstream. Consumers: quack-on-demand `BranchChanges.scala`, moraine `operations.rs`, ducklake-cdc-extension (`docs/hazard-log.md` H-011), PostHog viaduck (regex), delta_export, and a downstream freshness query (`NOT LIKE '<prefix>%'`) |
| Are the signals right? | Yes, proven on a prototype | `scratchpad/marker/REPORT.md`: markers exactly at `t` 3, 4, 5, 14, 15, 22; `ti` 18, 19, 20; `tf` 24, 25, 27, and none at 16, 17, 21, 26, 28. MERGE, TRUNCATE, `add_data_files`, inlined UPDATE, flush plus DELETE in either order and CTAS (final id) are correct. `transaction/*` is unchanged on the default, SQLite and Postgres backends |
| Token in `changes_made` or new column? | Column | **Token:** older DuckLake throws in `snapshots()` for the whole catalog (`ducklake_snapshots.cpp:96`), in `expire_snapshots` (`ducklake_expire_snapshots.cpp:77`) and on commit retries (`challenge_v5/tok_race.test`, 14 assertions). datafusion-ducklake (`snapshot_changes.rs:55`) and pg_ducklake (`ParseChangesMade`) reject it. **Column:** third-party readers select named columns and are unaffected. Older writers on DuckDB or SQLite metadata fail loudly ("has 6 columns but 5 values were supplied", `challenge_v5/col_probe.sql`); on Postgres they keep committing and write `NULL`, which correctly means unknown |
| Does the token need a cutoff? | The column doesn't | `NULL` is the default for old rows and unaware writers, so the v5 `data_change_marker_since` key and Task M3 are dropped. M3 also missed existing 1.1-dev1 catalogs, which evolve through `MigrateV10Dev` (`ducklake_initializer.cpp:272-284`) |
| Is there precedent? | Yes | 1.1-dev1 took seven column additions and a table without a version bump (`ducklake_metadata_manager.cpp:447-454`; pdet on #1102: "keep it under 1.1-dev1"). `author`/`commit_message`/`commit_extra_info` were added to this same table and to `snapshots()` output (commit `3738c695`) |
| Spec status | Drifted | The live spec page lists `compacted_table` and omits five types DuckLake writes (#924, 2026-04-01). It describes the table as existing "for easier conflict detection" |
| Roadmap | 1.1 = Variant inlining + multi-DV Puffin; 2.0 lists incremental MVs "applying tracked changes" | ducklake-web `_posts/2026-04-13-ducklake-10.md:143-157`. Leading with 2.0 invites deferral (#1429), so lead with the additive 1.1-dev fit |

## Global Constraints

- `changes_made` and every conflict check stay byte-for-byte as today (`ducklake_transaction_state.cpp:284,291,2380`; `STRING_AGG(changes_made)` at `ducklake_metadata_manager.cpp:4567`).
- Write `data_changes` with `SQLString`, not `SQLStringOrNull` (`:537-541`), so `''` stays distinct from `NULL`.
- Write it only when `supports_v1_1_metadata` holds. A 1.0 catalog keeps five columns.
- Every new or updated test pins `DUCKLAKE_VERSION`, because CI's `ducklake_version.json` config defaults to 1.0 (`DuckLakeVersion.yml:70`).
- AGENTS.md style; `make format-fix`; the DuckDB, SQLite, Postgres and Quack backends.
- Upstream commits: a signed title line only. Never open a PR, issue or Discussion; draft text to files for Florian.

## Definition (logical entries written for table T)

| Entry | When (signals in `AddTableChanges`, `ducklake_transaction.cpp:1066-1110`) |
|---|---|
| `inserted_into_table:T` | A new data file without `begin_snapshot` (INSERT, `add_data_files`); or `new_inlined_data && new_inlined_data->Count() > 0` |
| `deleted_from_table:T` | A `new_delete_files` entry with `source == REGULAR`; a REGULAR delete on a flushed file; `new_inlined_data_deletes` or `new_inlined_file_deletes`; or T in the state's `tables_deleted_from` (whole-file drops; union it, since `WriteSnapshotChanges` already copies it at `ducklake_transaction_state.cpp:391`) |
| Neither | Compaction (exclusive with DML, `:421-423`), flushes (outputs have `begin_snapshot`, deletes have `source == FLUSH`), DDL, renames, zero-row writes, net-zero inserts |
| UPDATE | Both entries, even for `SET a = a` |
| DROP TABLE | Nothing, unless DML ran first in the same transaction; this matches today's `inlined_insert` behavior, and table ids are never reused |

The kind is recorded, so Iceberg's operations are derivable per table: inserts only means `append`, deletes only means `delete`, both means `overwrite`, and `''` with physical changes means `replace`.

## Review Focus

1. Migrating an existing 1.1-dev1 catalog adds the column with `NULL` on every old row. Owned by C1.
2. A DDL-only commit writes `''`, not `NULL`. Owned by C2.
3. The 1.0 catalog path writes and reads no column. Owned by C1 and C2.
4. Server-side commit (Quack) writes the same entries; it reaches the same `WriteSnapshotChanges` (`ducklake_server_side_commit.cpp:113-127`). Owned by C2.
5. Concurrent commits with retries; SQLite reports `database is locked`, so these cases go in their own file. Owned by C2.

---

### Track 0 (independent of the marker, start now)

- [ ] **Downstream consumer:** replace its `NOT LIKE` prefix filter with the change-feed query validated in v4 (positional aliases, `rewrite_delete` filter, max change id, then time lookup). Today it counts DDL and flushes of inlined deletes, is not per table, and misses legacy `compacted_table`.
- [ ] **U1 (bug fix PR, goodwill, not a gate):** an inlined insert deleted in the same transaction creates no snapshot. 8 lines; verified in `scratchpad/explore_netzero/` and `challenge_v3/u1.sh`. Signed title: `Skip the snapshot of an inlined insert that is deleted in the same transaction`.
- [ ] **Candidate bug:** `RESET PARTITIONED BY` followed by `INSERT` in one transaction throws `INTERNAL Error: File should either not be partitioned, or have partition values` (found by the marker prototype; reproduce on main before anything else).

### Task S0: draft the ducklake-web spec correction (alongside D0)

- [ ] Write `scratchpad/plans/ducklake_web_snapshot_changes_spec.md`. Document every type DuckLake writes today (`inlined_insert`, `inlined_delete`, `inline_flush`, `merge_adjacent`, `rewrite_delete`), mark `compacted_table` as written only by older catalogs, and label which types are maintenance. Reference #924.

### Task D0 (gate): Discussion draft

- [ ] **Step 1:** Write `scratchpad/plans/ducklake_data_changes_discussion.md`, leading with the additive 1.1-dev fit:
  - **Problem:** consumers must classify physical change types. Six do, inconsistently (links). The flush of inlined deletes is recorded as `deleted_from_table`, contradicting the spec's "the snapshot deleted data".
  - **Proposal:** the `data_changes` column, with the Definition and the NULL/'' semantics.
  - **Alternatives:** the token (with the compat evidence above), a separate table, a per-snapshot `operation`, and reclassifying existing types (breaks conflict detection: `explore_flush/race_replace`, 169 rows instead of 99).
  - **Precedent:** Delta (`PROTOCOL.md:566,577,624`), Iceberg (`format/spec.md:972-976`), Hudi `read.streaming.skip_compaction`, and the Paimon `COMPACT` commit kind.
  - **Asks:** the column name; NULL vs '' semantics; reusing the grammar; staying on 1.1-dev1 or bumping.
  - **Related:** the 2.0 incremental-MV goal, as a later beneficiary.
- [ ] **Step 2:** Florian posts it after U1 is open, and after the next release (#1703). No C-task code before agreement.

### Task C1: schema and migration

**Files:** `src/storage/ducklake_metadata_manager.cpp:447-479` (append `{"ducklake_snapshot_changes", "data_changes", "VARCHAR"}` to `V1_1_DEV1_ADDED_COLUMNS`, which brings the attach-time probe and dev migration along); `src/metadata_manager/ducklake_metadata_manager_v1_1.cpp` (add a `GetSnapshotChangesTableStatement` override with the column); the snapshot-0 insert at `ducklake_metadata_manager.cpp:223` (version-aware, writes `''`); `GetAllSnapshots` reads the column when `supports_v1_1_metadata`.

- [ ] **Step 1: Write the failing test** `test/sql/functions/ducklake_data_changes_migration.test`:
  - a new 1.1 catalog has the column, and snapshot 0 is `''`;
  - a catalog created with `DUCKLAKE_VERSION '1.0'` has five columns and still commits;
  - the same catalog after `AUTOMATIC_MIGRATION` has `NULL` on old rows;
  - an existing 1.1-dev1 catalog without the column (create it with the base binary, or drop the column by hand) gains it with `NULL` on old rows.
- [ ] **Step 2: Confirm the failure.** Run `build/release/test/unittest test/sql/functions/ducklake_data_changes_migration.test`. Expected: FAIL on the missing column.
- [ ] **Step 3: Implement** as listed under Files.
- [ ] **Step 4: Verify** under the default, SQLite and Postgres configs, plus `'test/sql/migration/*'`.
- [ ] **Step 5: Commit** with the signed title `Add a data_changes column to ducklake_snapshot_changes`.

### Task C2: compute and write the entries

**Files:**
- `src/include/storage/ducklake_transaction_changes.hpp`: two sets in `TransactionChangeInformation` (`tables_data_inserted`, `tables_data_deleted`), and the matching sets in `SnapshotChangeInformation` (`:50-71`).
- `src/storage/ducklake_transaction.cpp:1066-1110`: the Definition signals.
- `src/storage/ducklake_transaction_state.cpp:388-430`: build the string, union the state's `tables_deleted_from`, and pass `supports_v1_1_metadata` (one parameter).
- `ducklake_metadata_manager.cpp:4544`: `WriteSnapshotChangesSql` gets an explicit column list.
- Positional fixtures: `test/sql/functions/ducklake_snapshots_chunked.test:24`, `test/sql/quack/server_side_commit_atomicity.test:32`.

- [ ] **Step 1: Write the failing tests.**
  - `ducklake_data_changes.test`, pinned to 1.1: replay `probes4/setup.sql` and assert `data_changes` per snapshot (inserted/deleted entries exactly at the proven snapshots). Add: a DDL-only commit is `''`; flush plus INSERT in one transaction (entry only for the inserted table); `UPDATE a = a` (both entries); CTAS (final id); MERGE; TRUNCATE.
  - `ducklake_data_changes_concurrency.test`: two connections inserting into different tables.
- [ ] **Step 2: Confirm the failure.** Run both tests. Expected: FAIL, because the column is NULL.
- [ ] **Step 3: Implement** the signals and the write.
- [ ] **Step 4: Verify.**
  - Run the default, SQLite, Postgres, Quack, `no_inline.json` and `deletion_vectors.json` configs.
  - Run `'test/sql/functions/*,test/sql/transaction/*,test/sql/data_inlining/*,test/sql/deletion_inlining/*,test/sql/compaction/*,test/sql/table_changes/*,test/sql/issues/*'`.
  - `changes_made` output must be byte-identical to main on the probe replay.
- [ ] **Step 5: Commit** with the signed title `Record the logical data changes of each snapshot`.

### Task C3: expose `data_changes` in `snapshots()`

**Files:** `src/functions/ducklake_snapshots.cpp` (`GetSnapshotTypes` and `GetSnapshotValues`: new `data_changes MAP(VARCHAR, VARCHAR[])` column, NULL when unknown, `{}` when `''`, parsed with the existing `ParseChangesMade`); `src/functions/ducklake_expire_snapshots.cpp:22` (shares `GetSnapshotTypes`).

- [ ] **Step 1: Write the failing test.** `SELECT snapshot_id, data_changes FROM dl.snapshots()` on the probe replay. Snapshot 26 (flush of inlined deletes) is `{}`, while `changes` still shows `tables_deleted_from=[5]`.
- [ ] **Step 2: Implement**, then list the tests that break on the extra output column with `rg -l "(tables_inserted_into|tables_deleted_from|inlined_insert|inlined_delete|flushed_inlined)=\[" test/sql` plus `SELECT *` users of `snapshots()`, and update them.
- [ ] **Step 3: Commit** with the signed title `Show the logical data changes in ducklake_snapshots`.

### Task C4 (optional): opt-in change feed without maintenance

- [ ] Add `data_changes_only => true` to `ducklake_table_changes`/`_insertions`/`_deletions`. It keeps rows only from snapshots whose `data_changes` names the table, and keeps everything where `data_changes IS NULL` (unknown). The default is unchanged, so #1571/#1589 hold. Test: `probes3/rw.sql` shows no rows at the rewrite snapshot with the option, and the update pairs without it.

### Consumers

- **dbt adapter freshness:** until the column ships in the consumer's pinned DuckLake, use the v4 change-feed query (`t` 22, `tf` 27, `ti` 20; 3 with shadowed columns; 4 after a gap). Afterwards: `SELECT max(snapshot_time) FROM ducklake_snapshots('<cat>') WHERE list_contains(map_keys(data_changes), 'tables_inserted_into') …`, or a per-table `list_contains` on the map values, with `NULL` mapped to the D2 "unknown" error.
- **Downstream consumer:** Track 0 now; `data_changes` later.
