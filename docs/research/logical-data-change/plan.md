# DuckLake Data-Change Marker Implementation Plan (v8, 2026-10-19)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Readers can tell, per snapshot and per table, whether a commit logically inserted or deleted data, or only did maintenance (flushes, compaction). Today they guess from physical change types.

**What changed since v6:** DuckLake 1.1 was finalized on main (#1799, merged 2026-10-17; publish tentatively 2026-10-21). The 1.1-dev1 route is gone, so a schema change now needs the next spec version. The plan therefore offers maintainers two options, and option B code waits for their agreement. Evidence: [`challenge-v6.md`](challenge-v6.md).

**What changed since v7 (second challenge and POCs, `scratchpad/challenge-v7/`, `scratchpad/poc/`):**
- **Option B POC works** (`poc/option-b.diff`, opt-in `1.2-dev1`, 23 files, +465/−27, of which source is 17 files, +184/−23). `data_changes` matches the Definition on all 44 v6 scenarios. With the version kept opt-in, only 3 existing tests change (error text). Making `1.2-dev1` the default breaks 5.
- **Option A holds:** on the same 44 scenarios it never under-reports. It over-reports 4 snapshots: three flushes of inlined deletes (`deleted_from_table:T` alone, or with `inline_flush:T` when the rows were inlined) and U1. `CHECKPOINT` runs on its own connection and there is no auto-flush, so neither can hide DML.
- **C1 belongs to the maintainers.** Every catalog version so far was opened by pdet or Mytherin (#252, #506, #784, #966, #1017, #1799), and an outside attempt at `1.1-dev2` (#1102) was told to stay on dev1. Ask which dev version to target, and rebase the column and migration onto theirs.
- **Gate and migration:** `SupportsV1_1Metadata()` tests `>= V1_1`, so B needs a separate gate for the new version. `MigrateV11` must be guarded by `catalog_version < target_version`.

**Option A, no schema change:** the spec docs classify every `changes_made` token as data, maintenance or DDL (Task S0), and readers derive. It never under-reports. It over-reports exactly one case, the flush of inlined deletes, and U1 until that bug is fixed.

**Option B, a column in the next spec version:**
- **Storage:** a nullable column `data_changes VARCHAR` on `ducklake_snapshot_changes`. It holds logical entries in the grammar DuckLake already parses (`inserted_into_table:<id>`, `deleted_from_table:<id>`).
- **Values:** `NULL` means "not recorded" (old rows, older or third-party writers); `''` means "recorded, no data change".
- **Computation:** at commit, in `DuckLakeTransaction::AddTableChanges`, the path both client and server-side commits use.
- **Reading:** `snapshots()` exposes it as a new `data_changes` MAP column.
- **Version:** a new catalog version after `V1_1`, named by the maintainers (`1.2-dev1` below is a placeholder), with a `MigrateV11` step.
- **Untouched:** conflict detection and the existing `changes_made` string.

**Tech Stack:** C++ DuckDB extension (clang-format 11.0.1), sqllogictest.

**Spec:** this file. Evidence was executed on upstream main `1a4d6ae2` (`challenge-v6.md`) unless marked otherwise.

## Evidence behind v7

| Question | Answer | Evidence |
|---|---|---|
| Can the 1.1-dev1 column list still take it? | No | `ducklake_version.hpp:15-28` ends at `V1_1 = 9` = `DUCKLAKE_LATEST_VERSION`. Commit `3a4b4845` removed the in-place dev migration; `V1_1_ADDED_COLUMNS` (`ducklake_metadata_manager.cpp:462`) is now part of the released 1.1. A 1.1 catalog runs no migration (`ducklake_initializer.cpp:279-282`) |
| What do released 1.1 binaries do with a catalog at the new version? | Refuse at ATTACH, readers included | `DuckLakeVersionFromString` throws `Unsupported ducklake_version '1.2-dev1'` (`ducklake_version.cpp:17-24`), with no options, `AUTOMATIC_MIGRATION`, `READ_ONLY` or a pinned `'1.1'` (`challenge-v7/probes/out/compat_duck.txt`, `poc/option-b-out/compat.txt`). ducklake-sdk, brikk, pg_ducklake's native writer and moraine also refuse an unknown version. Only datafusion-ducklake does not check it, and it writes named columns, so its snapshots get `NULL` |
| Are the signals right on main? | Yes | About 45 scenarios on `1a4d6ae2` (`challenge-v6.md`): DML, MERGE, TRUNCATE, CTAS, `add_data_files`, every 1.1 maintenance function, flush plus DML in either order, mixed flush on A with DML on B. The only maintenance that writes a delete file is the flush of inlined deletes (puffin, `FLUSH`). `DeleteFileSource` is still `REGULAR`/`FLUSH` (`ducklake_data_file.hpp:31-34`); 1.1 adds no change type |
| What does option B add over option A? | The flush of inlined deletes | A flush of inlined deletes writes `deleted_from_table:T` alone (probe `s03_flush_del`, snapshot 3), the same tokens as a real DELETE. The catalog cannot tell them apart later: `ducklake_delete_file` has no source column, and a deletion vector's `begin_snapshot` can predate its commit (`x4_dv_twice`) |
| Who classifies today? | Three consumers, and they disagree | quack-on-demand counts `rewrite_delete` as a data touch, moraine as compaction, viaduck as a delete. delta_export always writes `dataChange: true`. ducklake-cdc H-011 (vocabulary coverage) and pg_ducklake (conflict checks only) are not classification evidence. No upstream work as of 2026-10-19 |
| Token in `changes_made` or column? | Column | Unchanged from v5. Also: moraine fails closed on unknown tokens (`has_unknown`), datafusion-ducklake errors on them (`snapshot_changes.rs:55`) |
| Precedent? | Fresh, nullable, optional | Delta commit-level `commitInfo.dataChange` (#7524, merged 2026-08-26; RFC #7503: optional, readers fall back to per-file flags). Paimon nullable `operation` beside `CommitKind` (#8236, merged 2026-06-23). Iceberg `operation` is required and a missing one reads as `overwrite`. The v6 "no bump" precedent is gone: `author`/`commit_message`/`commit_extra_info` came with `MigrateV02`, a version bump (`ducklake_metadata_manager.cpp:387-397`) |
| Maintainer stance | Discussion first, maybe 2.0 | #1429 closed 2026-10-13 ("a feature and must be discussed beforehand, possibly for DuckLake 2.0"), #1626, #1703 ("discuss after the next release"), #1029 ("double-source of truth") |
| Spec status | Drifted | The live spec page lists `compacted_table` and omits five types DuckLake writes (ducklake-web#37, ducklake-web#242; duckdb/ducklake#924 is about temporal partition values, not this). It describes the table as existing "for easier conflict detection" |

## Global Constraints

- `changes_made` and every conflict check stay byte-for-byte as today (`STRING_AGG(changes_made)` at `ducklake_metadata_manager.cpp:4647`). `tables_deleted_from` keeps counting `FLUSH` delete files (`deleted_data = !table_changes.new_delete_files.empty()`, `ducklake_transaction.cpp:1077`); the new sets are separate.
- Write `data_changes` with `SQLString`, not `SQLStringOrNull` (`ducklake_metadata_manager.cpp:4617-4622`), so `''` stays distinct from `NULL`.
- Write it only on a catalog at the new version. Both version reads need it: `context.supports_v1_1_metadata` (`ducklake_transaction.cpp:1658`) and `ReadSupportsV1_1Metadata` (`ducklake_server_side_commit.cpp:545-552`). 1.0 and 1.1 catalogs keep five columns.
- Every insert into `ducklake_snapshot_changes` names its columns, including the snapshot-0 insert (`ducklake_metadata_manager.cpp:238`).
- Normative rules, from prior art: write `''` only when certain and over-report otherwise; `data_changes` is never read by conflict detection (delta-rs #4851); each entry maps to a token of the same commit for the same table, with `inserted_into_table` coming from `inserted_into_table` or `inlined_insert`, and `deleted_from_table` from `deleted_from_table` or `inlined_delete` (checked on all 44 POC scenarios).
- Every new or updated test pins `DUCKLAKE_VERSION`, because CI's `test/configs/ducklake_version.json` still sets 1.0 (`DuckLakeVersion.yml:70`).
- AGENTS.md style; `make format-fix`; the DuckDB, SQLite, Postgres and Quack backends.
- Upstream commits: a signed title line only. Never open a PR, issue or Discussion; draft text to files for Florian.

## Definition (logical entries written for table T)

| Entry | When (signals in `AddTableChanges`, `ducklake_transaction.cpp:1073-1115`) |
|---|---|
| `inserted_into_table:T` | A new in-memory data file without `begin_snapshot` (INSERT, `add_data_files`; in the catalog every file has one, probe `s02`); or `new_inlined_data && new_inlined_data->Count() > 0` |
| `deleted_from_table:T` | A `new_delete_files` entry with `source == REGULAR`; a REGULAR delete on a flushed file; `new_inlined_data_deletes` or `new_inlined_file_deletes`; or T in the state's `tables_deleted_from`, which `WriteSnapshotChanges` copies at `ducklake_transaction_state.cpp:392` (whole-file drops; union it) |
| Neither | Compaction (exclusive with DML, `ducklake_transaction_state.cpp:423`), flushes (outputs have `begin_snapshot`, deletes have `source == FLUSH`), DDL, renames, zero-row writes, net-zero inserts |
| UPDATE | Both entries, even for `SET a = a` |
| DROP TABLE | Nothing, unless DML ran first in the same transaction; this matches today's tokens (`s01_dml_drop`), and table ids are never reused |

The kind is recorded, so Iceberg's operations are derivable per table: inserts only means `append`, deletes only means `delete`, both means `overwrite`, and `''` with physical changes means `replace`.

Option A applies the same Definition to the tokens:

| Tokens for T | Class | Known error |
|---|---|---|
| `inserted_into_table`, `inlined_insert` | insert | `inlined_insert` on a net-zero transaction (U1) |
| `deleted_from_table`, `inlined_delete` | delete | `deleted_from_table` from the flush of inlined deletes |
| `inline_flush`, `merge_adjacent`, `rewrite_delete`, `compacted_table` (older catalogs) | maintenance | none |
| `created_*`, `dropped_*`, `altered_*` | DDL | none |

## Review Focus (option B)

1. Migrating a 1.1 catalog to the new version adds the column with `NULL` on every old row. Owned by C1.
2. A DDL-only commit writes `''`, not `NULL`. Owned by C2.
3. 1.0 and 1.1 catalogs write and read no column. Owned by C1 and C2.
4. Server-side commit (Quack) writes the same entries: it reaches the same `WriteSnapshotChanges` (`ducklake_server_side_commit.cpp:113-127`) but reads the version separately (`:545-552`). Owned by C2.
5. Concurrent commits with retries; SQLite reports `database is locked`, so these cases go in their own file. Owned by C2.
6. `data_changes` entries are a subset of `changes_made` on every commit. Owned by C2.

---

### Track 0 (independent of the marker, start now)

- [ ] **U1 (bug fix PR):** an inlined insert deleted in the same transaction still creates a snapshot on `1a4d6ae2` (`s05_u1`: snapshot 2 `{inlined_insert=[1]}`). The fix is the 8 lines in `LocalTableChanges::DeleteFromLocalInlinedData` (`ducklake_transaction.cpp:314`) from `inlined-net-zero.md`. It also removes one of option A's two errors. Signed title: `Skip the snapshot of an inlined insert that is deleted in the same transaction`.
- [ ] **RESET PARTITIONED BY bug (bug fix PR):** `BEGIN; ALTER TABLE t RESET PARTITIONED BY; INSERT INTO t ...; COMMIT` throws `INTERNAL Error: ... File should either not be partitioned, or have partition values` from `WriteNewDataFilesWithAppender` on `1a4d6ae2`, and invalidates the database (`s06b_fresh`). INSERT then RESET in one transaction works, and so do separate commits. Not reported (searched "should either not be partitioned", "RESET PARTITIONED BY"). Steps: a minimal sqllogictest under `test/sql/partitioning/`, find the root cause, fix, and pick the signed title once the cause is known.
- [ ] **Retry bug (diagnosed, no patch yet):**
  - What happens: any internal error at commit is retried 10 times, then reported as "Exceeded the maximum retry count … consider increasing it".
  - Why: `RetryOnError` (`ducklake_transaction.cpp:1385`) matches "unique", and the loop passes it `error.Message()` (`ducklake_transaction_state.cpp:2707`). For internal errors that message includes the stack trace (`duckdb/src/common/error_data.cpp:74-79`), whose `unique_ptr` frames match.
  - Likely fix: pass `error.RawMessage()`.
  - Open: a test needs another way to trigger an internal error at commit once the RESET bug is fixed.
- [ ] **POC patches ready** (`scratchpad/poc/`). Each applies on its own to `1a4d6ae2`, and still applies on `origin/main` (12 commits later, files untouched). Each test fails without its fix, and data_inlining, deletion_inlining, transaction, partitioning, table_changes and issues show 0 regressions.
  - `track0-u1.diff`: +9 source lines, 129-line test.
  - `track0-reset-partition.diff`: +2/−1 in `GetPartitionId` (`ducklake_insert.cpp:49`), 142-line test. The cause: a reset leaves a partition spec with no fields but keeps its id.
  - Still to do: clang-format 11.0.1, and the Postgres, SQLite and Quack configs.
- [ ] **Downstream consumer:** keep its query. It gates a schema-introspection cache, so DDL must count, and over-counting only costs a refresh; a row-level change feed would miss CREATE and ALTER. Only correct its comment claiming user changes and maintenance never share a snapshot: `s07_mixed` shows `{inlined_insert=[2], flushed_inlined=[1]}`. Its prefix filter stays safe because `WriteSnapshotChanges` emits user tokens before maintenance tokens (`ducklake_transaction_state.cpp:414-426`).

### Task S0: ducklake-web spec docs PR (option A, and a prerequisite for B)

- [ ] Write `scratchpad/plans/ducklake_web_snapshot_changes_spec.md`. Document every type DuckLake writes today (`inlined_insert`, `inlined_delete`, `inline_flush`, `merge_adjacent`, `rewrite_delete`), mark `compacted_table` as written only by older catalogs, add the option A classification table, and state that the flush of inlined deletes is recorded as `deleted_from_table`. This describes current behavior, so it is a docs fix, not a spec change. Open it after the 1.1 docs swap: past releases copied a preview tree and then swapped it in (ducklake-web #297, #299, #317), so a PR against `stable` now would be lost. Document both vocabularies (`tables_inserted_into` in `snapshots()` against the stored `inserted_into_table`, ducklake-web#242) and both flush spellings (`flushed_inlined` in 0.3 and earlier, `inline_flush` since). Reference ducklake-web#37 and #242.

### Task D0 (gate): Discussion draft

- [ ] **Step 1: Size the gap.** On a catalog where deletes are inlined, count how many `deleted_from_table` snapshots come from flushes rather than DELETEs. The catalog cannot identify them afterwards, and `CHECKPOINT` also flushes (`ducklake_checkpoint.cpp:21`), so record `max(snapshot_id)` around every flush and `CHECKPOINT` call and report the count as a lower bound. This number decides whether option A is enough.
- [ ] **Step 2:** Write `scratchpad/plans/ducklake_data_changes_discussion.md`:
  - **Problem:** consumers classify physical change types, and the three that do disagree (links). The flush of inlined deletes cannot be told apart from a DELETE.
  - **Option A:** the S0 classification, no schema change; exact except the flush of inlined deletes (over-reported), with the Step 1 number.
  - **Option B:** the `data_changes` column in the next spec version, with the Definition, the NULL/'' semantics and the normative rules.
  - **Compatibility of B:** released 1.1 binaries refuse to attach a catalog at the new version, so they fail loudly before reading or writing. `NULL` then comes only from rows written before the migration and from writers that skip the version check (datafusion-ducklake).
  - **Rejected alternatives:** the token (breaks older DuckLake, moraine, datafusion-ducklake), a separate table, a per-snapshot `operation`, and reclassifying existing types (breaks conflict detection: v5 `explore_flush/race_replace`, 169 rows instead of 99).
  - **Precedent:** Delta commit-level `dataChange` (#7524, RFC #7503), Paimon `operation` (#8236), Iceberg `operation`, and Hudi's skip flags as the cost of inferring from physical actions.
  - **Asks:** A or B; the column name; NULL vs '' semantics; which version carries B.
  - **Related:** the 2.0 incremental-MV goal, as a later beneficiary.
- [ ] **Step 3:** Florian posts it after 1.1 is published and after U1 is open. No C-task code before agreement on option B.

### Task C1 (option B): next catalog version and the column

**Files:**
- `src/include/common/ducklake_version.hpp:15-28` and `src/common/ducklake_version.cpp:17-30`: the new enum value, its string, `DUCKLAKE_LATEST_VERSION`, and `ParseWritableDuckLakeVersion`. If the maintainers open the next version themselves, rebase on theirs instead.
- `src/storage/ducklake_initializer.cpp:279-294`: a `MigrateV11` step from `V1_1`; `:342-349`: `ResolveTargetVersion` accepts the new version; `:352-373`: a `SetVersionedMetadataManager` branch.
- `src/include/storage/ducklake_metadata_manager.hpp:282` and `ducklake_metadata_manager.cpp:285`: a new virtual `GetSnapshotChangesTableStatement()` beside `GetSchemaTableStatement()`; the base keeps five columns.
- A versioned manager beside `src/metadata_manager/ducklake_metadata_manager_v1_1.cpp`, overriding that statement and `GetVersionString()`, instantiated for the four backends like `:63-66`.
- `MigrateV11`: `ADD COLUMN {IF_NOT_EXISTS} data_changes VARCHAR DEFAULT NULL` and the version update through `ExecuteMigration`, following `:387-397`.
- The snapshot-0 insert (`:238`) names its columns and writes `''` at the new version.
- A catalog gate beside `SupportsV1_1Metadata()` (`ducklake_catalog.hpp:312`), and `GetAllSnapshots` (`ducklake_metadata_manager.cpp:5094`) reads the column only behind it.

- [ ] **Step 1: Write the failing test** `test/sql/functions/ducklake_data_changes_migration.test`:
  - a new catalog at the new version has the column, and snapshot 0 is `''`;
  - catalogs created with `DUCKLAKE_VERSION '1.0'` and `'1.1'` have five columns and still commit;
  - each of them after `AUTOMATIC_MIGRATION` has the column with `NULL` on old rows.
- [ ] **Step 2: Confirm the failure.** Run `build/release/test/unittest test/sql/functions/ducklake_data_changes_migration.test`. Expected: FAIL on the missing column.
- [ ] **Step 3: Implement** as listed under Files.
- [ ] **Step 4: Verify** under the default, SQLite and Postgres configs, plus `'test/sql/migration/*'`.
- [ ] **Step 5: Commit** with the signed title `Add a data_changes column to ducklake_snapshot_changes`.

### Task C2 (option B): compute and write the entries

**Files:**
- `src/include/storage/ducklake_transaction_changes.hpp`: two sets in `TransactionChangeInformation` (`:22`), `tables_data_inserted` and `tables_data_deleted`, and the matching sets in `SnapshotChangeInformation` (`:50`).
- `src/storage/ducklake_transaction.cpp:1073-1115`: the Definition signals, leaving `deleted_data` as it is.
- `src/storage/ducklake_transaction_state.cpp:388-430`: build the string, union the state's `tables_deleted_from` (`:392`), and pass the new-version flag (one parameter).
- `ducklake_metadata_manager.cpp:4624-4630`: `WriteSnapshotChangesSql` names its columns.
- Positional fixtures: `test/sql/functions/ducklake_snapshots_chunked.test:24`, `test/sql/quack/server_side_commit_atomicity.test:32`, `benchmark/micro/fixtures/snap100k.benchmark.in:15`.

- [ ] **Step 1: Write the failing tests.**
  - `ducklake_data_changes.test`, pinned to the new version: the `challenge-v6.md` scenarios, asserting `data_changes` per snapshot. At least: INSERT and DELETE (file and inlined), UPDATE `SET a = a` (both entries), MERGE, TRUNCATE, CTAS (final id), `add_data_files`, a DDL-only commit (`''`), flush of inlined inserts, of deletes and of both (`''`), flush plus INSERT and flush plus DELETE in both orders, mixed flush on A with DML on B, merge adjacent and rewrite (`''`). After each commit, assert the subset rule against `changes_made`.
  - `ducklake_data_changes_concurrency.test`: two connections inserting into different tables.
- [ ] **Step 2: Confirm the failure.** Run both tests. Expected: FAIL, because the column is NULL.
- [ ] **Step 3: Implement** the signals and the write.
- [ ] **Step 4: Verify.**
  - Run the default, SQLite, Postgres, Quack, `no_inline.json` and `deletion_vectors.json` configs.
  - Run `'test/sql/functions/*,test/sql/transaction/*,test/sql/data_inlining/*,test/sql/deletion_inlining/*,test/sql/compaction/*,test/sql/table_changes/*,test/sql/issues/*'`.
  - `changes_made` output must be byte-identical to main on the same scenarios.
- [ ] **Step 5: Commit** with the signed title `Record the logical data changes of each snapshot`.

### Task C3 (option B): expose `data_changes` in `snapshots()`

**Files:** `src/functions/ducklake_snapshots.cpp` (`GetSnapshotTypes` `:57` and `GetSnapshotValues` `:90`: a new `data_changes MAP(VARCHAR, VARCHAR[])` column, NULL when unknown, `{}` when `''`, parsed with `SnapshotChangeInformation::ParseChangesMade` from `ducklake_transaction_changes.cpp:121`); `src/functions/ducklake_expire_snapshots.cpp:22,77` (shares both).

- [ ] **Step 1: Write the failing test.** `SELECT snapshot_id, data_changes FROM dl.snapshots()` on the `s03_flush_del` scenario: the flush snapshot is `{}`, while `changes` still shows `tables_deleted_from=[T]`.
- [ ] **Step 2: Implement**, then list the tests that break on the extra output column with `rg -l "(tables_inserted_into|tables_deleted_from|inlined_insert|inlined_delete|flushed_inlined)=\[" test/sql` plus `SELECT *` users of `snapshots()`, and update them.
- [ ] **Step 3: Commit** with the signed title `Show the logical data changes in ducklake_snapshots`.

### Task C4 (optional, after D0): change feed without maintenance

- [ ] Add `data_changes_only => true` to `ducklake_table_changes`/`_insertions`/`_deletions`. It keeps rows only from snapshots whose `data_changes` names the table, and keeps everything where `data_changes IS NULL` (unknown). The default is unchanged, so #1571/#1589 hold. Keep it out of the first ask. The feed still emits rows at rewrite snapshots (the v4 query needed a `rewrite_delete` filter), so re-measure that on main before proposing it.

### Consumers

- **dbt adapter:** altertable-ai/dbt-altertable#45 now reads a backend freshness function, so it needs neither option; the line below is obsolete.
- ~~**dbt adapter freshness:**~~ until option B ships in the consumer's pinned DuckLake, use the v4 change-feed query (`t` 22, `tf` 27, `ti` 20; 3 with shadowed columns; 4 after a gap). #1723 (merged 2026-10-15) fixed the shadowed-column case in `ducklake_table_changes`. Afterwards: `SELECT max(snapshot_time) FROM ducklake_snapshots('<cat>') WHERE list_contains(map_keys(data_changes), 'tables_inserted_into') …`, or a per-table `list_contains` on the map values, with `NULL` mapped to the D2 "unknown" error.
- **Downstream consumer:** Track 0 only. It needs neither option.
