# DuckLake Data-Change Marker Implementation Plan (v10, 2026-10-20)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Readers can tell, per snapshot and per table, whether a commit logically inserted or deleted data, or only did maintenance (flushes, compaction). Today they guess from physical change types.

**Decision (2026-10-20):** Florian chose option B, a column in the next catalog version. Option A stays as the spec docs fix (S0) and as the reader fallback for `NULL` rows.

## Upstream gate

Option B changes the catalog specification, so upstream treats it as a feature. The rule was restated twice on 2026-10-12/13:

- PeterVanHolland on #1429 and #1626: "Features are developed in collaboration with the DuckDB Labs team to keep them compatible with the roadmap. Please open a discussion first. Once it is approved, the feature can be developed."
- pdet closed #1429 "as it is a feature and must be discussed beforehand, possibly for DuckLake 2.0", said of #1626 "this is prolly sth for 2.0", and closed #1703 with "discuss after the next release".
- Catalog versions belong to the maintainers. Every one was opened by pdet or Mytherin (#252, #506, #784, #966, #1017, #1799), and pdet told an outside PR to stay on the existing dev version ("you can just keep it under `1.1-dev1`", #1102, 2026-06-23).
- Spec questions go to ducklake-web issues (pdet on discussion #934, 2026-04-24).

What this means for the plan:

1. **No C-task PR before a maintainer approves the design in a Discussion.** The POC stays a branch on the fork, linked from the Discussion as evidence. A working PoC has drawn a positive reply before (pdet on discussion #1644: "This sounds awesome!").
2. **Post after 1.1 is published** (tentatively 2026-10-21), per #1703.
3. **Ask which version carries it,** and expect 2.0 as an answer. `1.2-dev1` below is a placeholder.
4. **Keep the ask small and generic.** pdet, Mytherin or guillesd replied in 55 of 181 Ideas and General discussions (all of them, fetched 2026-10-20). When pdet engaged on a design (#627), he came back after "a quick internal discussion" with a more generic shape (`min_merge_size`/`max_merge_size`). Say which parts are negotiable: the name, the encoding, the version.
5. **Pre-empt the recurring review asks** once code is allowed (`~/.claude/skills/ducklake-dev/references/maintainers.md`): tests enumerated and split by scenario, SQL in the metadata manager per backend, every behaviour change named (`snapshots()` gaining a column is one), throw on invalid state.

## What changed since v9

- **Track 0 is done.**
  - U1 merged as #1807, and the RESET insert fix as #1808 (both by pdet, 2026-10-19).
  - The RESET plus `ducklake_add_data_files` case was fixed upstream by `6fd1906e` (Peter Van Holland, 2026-10-19).
  - The retry fix is rebased on `ed31fddd` with a new trigger, an internal error in a Quack server-side commit (`test/sql/quack/server_side_commit_internal_error.test`), since the RESET trigger is gone. It is on `fvaleye/fl/classify-commit-retries-without-stack-traces`, with its description drafted, for Florian to open. The v9 ordering constraint is obsolete.
- **Option A has one known error left:** the flush of inlined deletes. U1 is fixed.
- **D0 Step 1 is sized (locally).** The method and numbers are in Evidence below. In an Altertable-like workload, 8 of the 10 snapshot and table pairs tagged `deleted_from_table` were flushes that changed no row.
- **The acceptance oracle is row content, not the change feed.** By upstream design, `ducklake_rewrite_data_files` makes `table_changes` emit identical pre-image and post-image rows for every surviving row (#1571 expected output, #1589 merged 2026-10-09). The oracle is `EXCEPT ALL` both ways between `AT (VERSION => S-1)` and `S`.
- **Precedent corrected:**
  - Delta's base protocol keeps `commitInfo.dataChange` optional, with readers falling back to per-file flags.
  - RFC #7503 (merged 2026-09-28) makes it required under `adaptiveMetadata`, because "a reader tailing the log cannot recover the answer" from physical actions. That is DuckLake's flush case.
  - #7524 only filled the `commitLarge` path.
  - #7658 reads the field behind an off-by-default config and records telemetry when it disagrees with the file actions.
- **Roadmap corrected:** "Materialized views and incremental maintenance" is under "Future Work / Looking for Funding" on ducklake.select/roadmap, not a 2.0 goal.
- **Naming:** the docs call `table_changes` the "Data Change Feed" (`/docs/stable/duckdb/advanced_features/data_change_feed`), and that feed reports rewrites as updates. A `data_changes` column with the opposite meaning invites confusion, so the name becomes an open ask.
- **The POC needs a rebase:** `option-b.diff` no longer applies to `ed31fddd` (`ducklake_metadata_manager.cpp`, `ducklake_server_side_commit.cpp`, `ducklake_transaction.cpp`).

## Option B

- **Storage:** a nullable column `data_changes VARCHAR` on `ducklake_snapshot_changes` (name to be agreed). It holds logical entries in the grammar DuckLake already parses (`inserted_into_table:<id>`, `deleted_from_table:<id>`).
- **Values:** `NULL` means "not recorded" (old rows, older or third-party writers); `''` means "recorded, no data change".
- **Reader fallback:** for a `NULL` row, classify `changes_made` with the option A table. It never under-reports, so the fallback is safe.
- **Computation:** at commit, in `DuckLakeTransaction::AddTableChanges`, the path both client and server-side commits use.
- **Reading:** `snapshots()` exposes it as a new `data_changes` MAP column.
- **Version:** a new catalog version after `V1_1`, chosen by the maintainers, with a `MigrateV11` step.
- **Untouched:** conflict detection and the existing `changes_made` string.

**Tech Stack:** C++ DuckDB extension (clang-format 11.0.1), sqllogictest.

## Evidence

| Question | Answer | Evidence |
|---|---|---|
| How often does option A over-report? | 8 of 81 pairs; 8 of the 10 `deleted_from_table` pairs | Local run on `ed31fddd`, identical on DuckDB, SQLite and Postgres catalogs. Workload: inlining limit 10, small and large inserts, small deletes, MERGE upserts, an append-only table, flush every 3 rounds, `merge_adjacent_files` every 4, `rewrite_data_files` every 6, 12 rounds. Option A agrees on all 65 data pairs and all 8 maintenance pairs (`merge_adjacent`, `rewrite_delete`, an insert-only flush). It over-reports 7 `deleted_from_table+inline_flush` flushes and 1 `deleted_from_table` flush. It under-reports nothing. Synthetic, not production. Scripts: `.claude/logical-data-change/oracle/` |
| Can a token rule fix A? | No | A flush of only inlined deletes records `deleted_from_table:T` alone (`ducklake_transaction.cpp:1085-1115`: `inline_flush` only for data files with a begin snapshot). In the run, snapshot 76 deleted 100 rows and snapshot 78 changed none, and both are `deleted_from_table:1`. The catalog cannot tell them apart later: `ducklake_delete_file` has no source column, and a deletion vector's `begin_snapshot` can predate its commit (`x4_dv_twice`) |
| Does option B work? | Yes, on `1a4d6ae2` | POC `option-b.diff`, 23 files, +470/−30 formatted. The whole suite passes under the default, Postgres, SQLite, Quack, `no_inline`, `deletion_vectors` and `ducklake_version` configs. `data_changes` matches the Definition on all 44 v6 scenarios. Details in "POC record" below |
| What do released 1.1 binaries do with a catalog at the new version? | Refuse at ATTACH, readers included | `DuckLakeVersionFromString` throws `Unsupported ducklake_version '1.2-dev1'` (`ducklake_version.cpp:17-24`), with no options, `AUTOMATIC_MIGRATION`, `READ_ONLY` or a pinned `'1.1'`. ducklake-sdk, brikk, pg_ducklake's native writer and moraine also refuse an unknown version. Only datafusion-ducklake does not check it, and it writes named columns, so its snapshots get `NULL` |
| Who classifies today? | Three consumers, and they disagree | quack-on-demand counts `rewrite_delete` as a data touch, moraine as compaction, viaduck as a delete. delta_export always writes `dataChange: true`. No upstream work or Discussion on this as of 2026-10-20 |
| Token in `changes_made` or column? | Column | moraine fails closed on unknown tokens (`has_unknown`), datafusion-ducklake errors on them (`snapshot_changes.rs:55`), and older DuckLake does not know them |
| Precedent? | Commit-level flag, required where physical actions cannot answer | Delta `commitInfo.dataChange` (RFC #7503, #7524, #7658, see above). Paimon nullable `operation` beside `CommitKind` (#8236). Iceberg's required snapshot `operation`. A version bump carried the previous snapshot metadata too: `author`/`commit_message`/`commit_extra_info` came with `MigrateV02` (`ducklake_metadata_manager.cpp:387-397`) |
| Spec status | Drifted | The live spec page lists `compacted_table` and omits five types DuckLake writes (ducklake-web#37, ducklake-web#242, both open). It describes the table as existing "for easier conflict detection" |

## Global Constraints

- `changes_made` and every conflict check stay byte-for-byte as today (`STRING_AGG(changes_made)` at `ducklake_metadata_manager.cpp:4647`). `tables_deleted_from` keeps counting `FLUSH` delete files; the new sets are separate.
- Write `data_changes` with `SQLString`, not `SQLStringOrNull` (`ducklake_metadata_manager.cpp:4617-4622`), so `''` stays distinct from `NULL`.
- Write it only on a catalog at the new version. Both version reads need it: `context.supports_v1_1_metadata` (`ducklake_transaction.cpp:1658`) and `ReadSupportsV1_1Metadata` (`ducklake_server_side_commit.cpp:545-552`). 1.0 and 1.1 catalogs keep five columns.
- Every insert into `ducklake_snapshot_changes` names its columns, including the snapshot-0 insert (`ducklake_metadata_manager.cpp:238`).
- Normative rules, from prior art: write `''` only when certain and over-report otherwise; `data_changes` is never read by conflict detection (delta-rs #4851); each entry maps to a token of the same commit for the same table, with `inserted_into_table` coming from `inserted_into_table` or `inlined_insert`, and `deleted_from_table` from `deleted_from_table` or `inlined_delete`. This subset rule is what lets a reader check the column against `changes_made`, as Delta's #7658 telemetry does against file actions.
- Every new or updated test pins `DUCKLAKE_VERSION`, because CI's `test/configs/ducklake_version.json` still sets 1.0 (`DuckLakeVersion.yml:70`).
- AGENTS.md style; `make format-fix`; the DuckDB, SQLite, Postgres and Quack backends.
- Upstream commits: a signed title line only. Never open a PR, issue or Discussion; draft text to files for Florian. No C-task PR before Discussion approval.

## Definition (logical entries written for table T)

| Entry | When (signals in `AddTableChanges`, `ducklake_transaction.cpp:1085-1115`) |
|---|---|
| `inserted_into_table:T` | A new in-memory data file without `begin_snapshot` (INSERT, `add_data_files`; in the catalog every file has one, probe `s02`); or `new_inlined_data && new_inlined_data->Count() > 0` |
| `deleted_from_table:T` | A `new_delete_files` entry with `source == REGULAR`; a REGULAR delete on a flushed file; `new_inlined_data_deletes` or `new_inlined_file_deletes`; or T in the state's `tables_deleted_from`, which `WriteSnapshotChanges` copies (whole-file drops; union it) |
| Neither | Compaction (exclusive with DML), flushes (outputs have `begin_snapshot`, deletes have `source == FLUSH`), DDL, renames, zero-row writes, net-zero inserts |
| UPDATE | Both entries, even for `SET a = a` |
| DROP TABLE | Nothing, unless DML ran first in the same transaction; this matches today's tokens (`s01_dml_drop`), and table ids are never reused |

The kind is recorded, so Iceberg's operations are derivable per table: inserts only means `append`, deletes only means `delete`, both means `overwrite`, and `''` with physical changes means `replace`.

The content oracle agrees with the Definition except for `UPDATE SET a = a`, which changes no row content but records both entries. Acceptance workloads leave it out.

Option A applies the same Definition to the tokens:

| Tokens for T | Class | Known error |
|---|---|---|
| `inserted_into_table`, `inlined_insert` | insert | none since #1807 |
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

## POC record (`.claude/logical-data-change/poc/`, on `1a4d6ae2`)

- With the version kept opt-in, only 3 existing tests change (error text). Making `1.2-dev1` the default breaks 5. Its 3 new tests fail without it.
- `''` survives a round trip through Postgres and SQLite as distinct from `NULL`.
- 1.0 and 1.1 migrate to `1.2-dev1` with `NULL` on old rows, including with `METADATA_SCHEMA`. A pinned 1.1 target stops at 1.1, thanks to the repeated `>= target` check before `MigrateV11`.
- Retries recompute `data_changes` with the final table ids, even when a concurrent CREATE remaps a CTAS. 200 tables in one commit list all ids, and `expire_snapshots` carries the column.
- Two processes migrating one Postgres catalog at once never failed in 27 overlapping trials, the same as the 1.0 → 1.1 control (25 trials). The migration is one metadata transaction with no lock; a loser falls into the upstream attach retry (`ducklake_initializer.cpp:134-163`). SQLite was not run.
- **Open design points for the Discussion:**
  - While `1.2-dev1` is not `DUCKLAKE_LATEST_VERSION`, `AUTOMATIC_MIGRATION` on a `1.2-dev1` catalog fails with "Cannot downgrade" (`ResolveTargetVersion`). This disappears once the maintainers make the new version the latest.
  - The POC adds `data_changes` to `snapshots()` on 1.0 and 1.1 catalogs too (always `NULL`), which changes `SELECT *` for everyone.
  - A 1.1 Quack server lets a new client migrate the catalog, then refuses every data-only commit with `Unsupported ducklake_version '1.2-dev1'` (`ducklake_server_side_commit.cpp:99`, `:543-552`), while DDL commits still run client-side. Nothing is half-written, but writes stop until the server is upgraded.
- The migration test must scope its `information_schema.columns` counts by schema, or extra metadata schemas in a Postgres database break it.

---

### Task S0: ducklake-web spec docs fix (option A, and a prerequisite for B)

- [ ] Write `scratchpad/plans/ducklake_web_snapshot_changes_spec.md`. Document every type DuckLake writes today (`inlined_insert`, `inlined_delete`, `inline_flush`, `merge_adjacent`, `rewrite_delete`), mark `compacted_table` as written only by older catalogs, add the option A classification table, and state that the flush of inlined deletes is recorded as `deleted_from_table`. This describes current behavior, so it is a docs fix, not a spec change, and it does not wait for the Discussion. Target it after the 1.1 docs swap: past releases copied a preview tree and then swapped it in (ducklake-web #297, #299, #317), so a PR against `stable` now would be lost. Document both vocabularies (`tables_inserted_into` in `snapshots()` against the stored `inserted_into_table`) and both flush spellings (`flushed_inlined` in 0.3 and earlier, `inline_flush` since). Reference ducklake-web#37 and #242. Florian opens it.

### Task D0 (gate): Discussion draft

- [x] **Step 1: Size the gap.** Done locally (Evidence, first row). A production count stays open: the catalog cannot identify flush deletes afterwards, so it needs `max(snapshot_id)` recorded around every flush and `CHECKPOINT` (`ducklake_checkpoint.cpp:21`).
- [ ] **Step 2: Rebase the POC** onto the latest main, rerun its tests and the oracle workload, and push it to a fork branch. No PR.
- [ ] **Step 3:** Write `scratchpad/plans/ducklake_data_changes_discussion.md`, Ideas category, in the shape of #1630 (problem, design, alignment with other formats):
  - **Problem:** `changes_made` exists for conflict detection, and consumers classifying it disagree (links). A flush of inlined deletes records the same tokens as a DELETE, so no reader can tell them apart; give the measured 8 of 10.
  - **Proposal:** the column, the Definition, the `NULL`/`''` semantics, the normative rules, and the fallback to the option A table for `NULL`.
  - **Compatibility:** released 1.1 binaries refuse to attach a catalog at the new version, so they fail loudly before reading or writing. A 1.1 Quack server refuses data-only commits after the migration.
  - **Rejected alternatives:** a new token (breaks older DuckLake, moraine, datafusion-ducklake); recording `inline_flush` on deletion flushes, or reclassifying existing types (both change tokens that conflict detection reads, `ducklake_transaction_state.cpp:284-328`; v5 `explore_flush/race_replace` gave 169 rows instead of 99); a separate table; a per-snapshot `operation`; filtering `table_changes` (it reports row images, and rewrites by design).
  - **Precedent:** Delta (RFC #7503 required under `adaptiveMetadata` for the same reason, #7658 telemetry), Paimon `operation` (#8236), Iceberg `operation`.
  - **Asks:** whether a logical change field is welcome, and in which version (next 1.x or 2.0); the name, given the "Data Change Feed"; `NULL` vs `''`; whether `snapshots()` shows the column on old catalogs; the Quack upgrade order.
  - **Evidence link:** the rebased POC branch.
- [ ] **Step 4:** Florian posts it after 1.1 is published. No C-task PR before a maintainer approves.

### Task C1 (option B, after approval): next catalog version and the column

**Files:**
- `src/include/common/ducklake_version.hpp` and `src/common/ducklake_version.cpp`: the new enum value, its string, `DUCKLAKE_LATEST_VERSION`, and `ParseWritableDuckLakeVersion`. If the maintainers open the next version themselves, rebase on theirs.
- `src/storage/ducklake_initializer.cpp`: a `MigrateV11` step from `V1_1`, guarded by `catalog_version < target_version`; `ResolveTargetVersion` accepts the new version; a `SetVersionedMetadataManager` branch.
- `src/include/storage/ducklake_metadata_manager.hpp` and `ducklake_metadata_manager.cpp`: a new virtual `GetSnapshotChangesTableStatement()` beside `GetSchemaTableStatement()`; the base keeps five columns.
- A versioned manager beside `src/metadata_manager/ducklake_metadata_manager_v1_1.cpp`, overriding that statement and `GetVersionString()`, instantiated for the four backends.
- `MigrateV11`: `ADD COLUMN {IF_NOT_EXISTS} data_changes VARCHAR DEFAULT NULL` and the version update through `ExecuteMigration`.
- The snapshot-0 insert names its columns and writes `''` at the new version.
- A catalog gate beside `SupportsV1_1Metadata()`, which tests `>= V1_1`; `GetAllSnapshots` reads the column only behind it.

- [ ] **Step 1: Write the failing test** `test/sql/functions/ducklake_data_changes_migration.test`:
  - a new catalog at the new version has the column, and snapshot 0 is `''`;
  - catalogs created with `DUCKLAKE_VERSION '1.0'` and `'1.1'` have five columns and still commit;
  - each of them after `AUTOMATIC_MIGRATION` has the column with `NULL` on old rows.
- [ ] **Step 2: Confirm the failure.** Run `build/release/test/unittest test/sql/functions/ducklake_data_changes_migration.test`. Expected: FAIL on the missing column.
- [ ] **Step 3: Implement** as listed under Files.
- [ ] **Step 4: Verify** under the default, SQLite and Postgres configs, plus `'test/sql/migration/*'`.
- [ ] **Step 5: Commit** with the signed title `Add a data_changes column to ducklake_snapshot_changes`.

### Task C2 (option B, after approval): compute and write the entries

**Files:**
- `src/include/storage/ducklake_transaction_changes.hpp`: two sets in `TransactionChangeInformation`, `tables_data_inserted` and `tables_data_deleted`, and the matching sets in `SnapshotChangeInformation`.
- `src/storage/ducklake_transaction.cpp` (`AddTableChanges`): the Definition signals, leaving `deleted_data` as it is.
- `src/storage/ducklake_transaction_state.cpp` (`WriteSnapshotChanges`): build the string, union the state's `tables_deleted_from`, and pass the new-version flag (one parameter).
- `ducklake_metadata_manager.cpp` (`WriteSnapshotChangesSql`): name its columns.
- Positional fixtures: `test/sql/functions/ducklake_snapshots_chunked.test`, `test/sql/quack/server_side_commit_atomicity.test`, `benchmark/micro/fixtures/snap100k.benchmark.in`.

- [ ] **Step 1: Write the failing tests**, one file per scenario group, pinned to the new version:
  - DML: INSERT and DELETE (file and inlined), UPDATE `SET a = a` (both entries), MERGE, TRUNCATE, CTAS (final id), `add_data_files`.
  - Maintenance: flush of inlined inserts, of deletes and of both (`''`), merge adjacent and rewrite (`''`), a DDL-only commit (`''`).
  - Mixed: flush plus INSERT and flush plus DELETE in both orders, mixed flush on A with DML on B.
  - After each commit, assert the subset rule against `changes_made`.
  - `ducklake_data_changes_concurrency.test`: two connections inserting into different tables.
- [ ] **Step 2: Confirm the failure.** Expected: FAIL, because the column is `NULL`.
- [ ] **Step 3: Implement** the signals and the write.
- [ ] **Step 4: Verify.**
  - Run the default, SQLite, Postgres, Quack, `no_inline.json` and `deletion_vectors.json` configs.
  - Run `'test/sql/functions/*,test/sql/transaction/*,test/sql/data_inlining/*,test/sql/deletion_inlining/*,test/sql/compaction/*,test/sql/table_changes/*,test/sql/issues/*'`.
  - `changes_made` output must be byte-identical to main on the same scenarios.
  - The oracle workload on all three catalogs: `data_changes` has zero over-reports and zero under-reports against the content diff.
- [ ] **Step 5: Commit** with the signed title `Record the logical data changes of each snapshot`.

### Task C3 (option B, after approval): expose `data_changes` in `snapshots()`

**Files:** `src/functions/ducklake_snapshots.cpp` (`GetSnapshotTypes` and `GetSnapshotValues`: a new `data_changes MAP(VARCHAR, VARCHAR[])` column, `NULL` when unknown, `{}` when `''`, parsed with `SnapshotChangeInformation::ParseChangesMade`); `src/functions/ducklake_expire_snapshots.cpp` (shares both).

- [ ] **Step 1: Write the failing test.** `SELECT snapshot_id, data_changes FROM dl.snapshots()` on the flush of inlined deletes: the flush snapshot is `{}`, while `changes` still shows `tables_deleted_from=[T]`.
- [ ] **Step 2: Implement**, then list the tests that break on the extra output column with `rg -l "(tables_inserted_into|tables_deleted_from|inlined_insert|inlined_delete|flushed_inlined)=\[" test/sql` plus `SELECT *` users of `snapshots()`, and update them. Name this behaviour change in the PR description.
- [ ] **Step 3: Commit** with the signed title `Show the logical data changes in ducklake_snapshots`.

### Task C4 (optional, later): change feed without maintenance

- [ ] Add `data_changes_only => true` to `ducklake_table_changes`/`_insertions`/`_deletions`. It keeps rows only from snapshots whose `data_changes` names the table, and keeps everything where `data_changes IS NULL`. The default is unchanged, so #1571/#1589 hold. Keep it out of the first ask.

### Consumers

- **Altertable:** backend #4413 (2026-09-28) removed the global `inlining = 0`, so catalogs now inline under DuckLake's default limit of 10, and compaction calls `ducklake_flush_inlined_data`. Flushes of inlined deletes therefore now reach Altertable's consumers as `deleted_from_table`. This was verified from the code, not measured in production.
- **dbt adapter:** altertable-ai/dbt-altertable#45 reads a backend freshness function, so it needs neither option. Once B ships, a freshness reader would filter `snapshots()` on the column per table and use the option A table for `NULL`.
- **Downstream schema cache:** keeps its query. DDL must count, and over-counting only costs a refresh. Only correct its comment claiming user changes and maintenance never share a snapshot: `s07_mixed` shows `{inlined_insert=[2], flushed_inlined=[1]}`.
