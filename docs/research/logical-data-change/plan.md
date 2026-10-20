# DuckLake Data-Change Marker Implementation Plan (v11, 2026-10-20)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Readers can tell, per snapshot and per table, whether a commit logically inserted or deleted data, or only did maintenance (flushes, compaction). Today they guess from physical change types.

**Decision (2026-10-20):** Florian chose option B, a column in the next catalog version. Option A stays as the reader fallback for `NULL` rows and as a docs fix (S0). A second direction, B2, stores the same value in a separate table without a version bump, so that 1.1 binaries keep working; its POC decides whether it goes into the ask as an alternative.

## Upstream gate

Option B changes the catalog specification, so upstream treats it as a feature:

- pdet's bot (PeterVanHolland, bio "@pdet's robot friend") posted the same template on #1355, #1429, #1626 (2026-10-12) and #1471 (2026-10-13). On #1429: "Features are developed in collaboration with the DuckDB Labs team to keep them compatible with the roadmap. Please open a discussion first. Once it is approved, the feature can be developed."
- pdet closed #1429 "as it is a feature and must be discussed beforehand, possibly for DuckLake 2.0", said of #1626 "this is prolly sth for 2.0", and closed #1703 with "discuss after the next release".
- pdet on #1815 (2026-10-20), about one SQLite index: "index creation is a spec change for a given catalog, with trade-offs that require a more in-depth analysis, let's maybe create a discussion first". So B2's separate table is a spec change too, version bump or not.
- Catalog versions belong to the maintainers. Every one was opened by pdet or Mytherin (#252, #506, #784, #966, #1017, #1799), and pdet told an outside PR to stay on the existing dev version ("you can just keep it under `1.1-dev1`", #1102, 2026-06-23).

Public Discussions rarely get an answer:

- 33 of 181 Ideas and General discussions have a reply from pdet, Mytherin or guillesd other than the closing note pdet posted on 25 of them on 2026-10-16. Of the 28 created since 2026-05-01, none has (GraphQL dump of all 264 discussions, 2026-10-20).
- The discussions the bot sent contributors to (#913, #1454, #1630) have no maintainer reply.
- The schema change that did land from outside is Altertable's own. redox opened Discussion #1077 (no maintainer reply), then PR #1102, which added the `ducklake_view_column_tag` table and a migration. Its body ends "Internal Ref https://github.com/duckdblabs/altertable/issues/7", and pdet merged it on 2026-07-20.

What this means for the plan:

1. **Route:** raise the proposal through Altertable's DuckDB Labs channel, the one #1102 referenced, and post a short public Discussion as the record. Whether that channel is active, and whether to use it, is Florian's call (`duckdblabs/altertable` is private, so this was not checked). Timebox the Discussion.
2. **No C-task PR before a maintainer approves the design.** The POC stays a branch on the fork, linked as evidence.
3. **Post after 1.1 is published** (2026-10-21, ducklake-web#437), per #1703.
4. **Ask which version carries it,** and expect 2.0 as an answer. `1.2-dev1` below is a placeholder.
5. **Keep it short, with one ask.** The posts that drew a design reply were short with one proposal: #627 (1,368 characters; pdet came back after "a quick internal discussion" with a more generic `min_merge_size`/`max_merge_size`) and #1077 (1,353 characters, led to #1102). The unanswered #1630 is 2,145. Say which parts are negotiable: the name, the encoding, the version.
6. **Pre-empt the recurring review asks** once code is allowed (`~/.claude/skills/ducklake-dev/references/maintainers.md`): tests enumerated and split by scenario, SQL in the metadata manager per backend, every behaviour change named (`snapshots()` gaining a column is one), throw on invalid state. Expect the bot to probe for races, as it did on #1429.

## What changed since v10 (adversarial review and probes, `scratchpad/v10-review/`, `scratchpad/probe-flushdel/`)

- **Route:** the Discussion-only route is likely to meet silence (numbers above); #1102 is the precedent to follow.
- **S0 shrinks:** pdet's ducklake-web#437 ("Update documentation for DuckLake 1.1", opened 2026-10-20) already documents every change type and marks `compacted_table` as written only by older versions. It copies `docs/stable` to `docs/1.0` and edits `stable` in place, so there is no preview tree to wait for.
- **Altertable claim corrected:** backend #4413 kept `SET GLOBAL ducklake_default_data_inlining_row_limit = 0` (`settings.rs:83`) and made inlining opt-in per catalog (`databases.data_inlining_row_limit`). The over-report reaches only catalogs that opt in.
- **Reader fallback fixed:** datafusion-ducklake writes `changes_made` as `NULL` first and appends later (`metadata_writer_postgres_single.rs:521-536`), so a missing or `NULL` `changes_made` must count as a change.
- **A fourth classifier:** datafusion-ducklake `src/change_detection.rs` (2026-10-16) documents the same over-report (lines 26-32) and counts `rewrite_data_files` as a change (lines 40-42).
- **One more collision:** `BEGIN; CALL ducklake_flush_inlined_data(...); DELETE ...; COMMIT` records `deleted_from_table:T,inline_flush:T` for a real 1-row delete, the same tokens as the 7 flushes that changed nothing (`probe-flushdel`, snapshot 7, DuckDB and SQLite).
- **Not reproduced:** datafusion-ducklake's claim (`change_detection.rs:38-40`) that a DELETE committed with a flush records only `inline_flush`. Four orders on DuckDB and SQLite catalogs all recorded the delete (`probe-flushdel`). Postgres and Quack not run.
- **Bot fork:** PeterVanHolland/ducklake has 59 open PRs that reach upstream in batches (#1817). Relevant: PVH#114 rewrites the same `RetryOnError` line as the retry fix; PVH#58 validates metadata column names, order and types per catalog version on attach (it ignores tables it does not expect, so B2's table passes); PVH#122 reworks the stats read for the tables a commit changes; PVH#118 retries an attach whose migration races another.
- **Precedent, line numbers, migration shape and file lists** corrected below.

Carried from v10: Track 0 is done. U1 (#1807) and the RESET insert fix (#1808) were authored by Florian and merged by pdet on 2026-10-19; `6fd1906e` fixed RESET with `ducklake_add_data_files`. The retry fix is on `fvaleye/fl/classify-commit-retries-without-stack-traces` for Florian to open. The acceptance oracle is row content, not `table_changes`, which reports a rewrite as identical update images by design (#1571, #1589). Materialized views are "Future Work / Looking for Funding" on the roadmap.

## Option B

- **Storage:** a nullable column `data_changes VARCHAR`, last in `ducklake_snapshot_changes` (name to be agreed). It holds logical entries in the grammar DuckLake already parses (`inserted_into_table:<id>`, `deleted_from_table:<id>`).
- **Values:** `NULL` means "not recorded" (old rows, older or third-party writers); `''` means "recorded, no data change".
- **Reader fallback:** for a `NULL` value, classify `changes_made` with the option A table; if `changes_made` is also `NULL`, or the row is missing, count the snapshot as a data change for every table. Both directions only over-report.
- **Computation:** at commit, in `DuckLakeTransaction::AddTableChanges`, the path both client and server-side commits use.
- **Reading:** `snapshots()` exposes it as a new `data_changes` MAP column.
- **Version:** a new catalog version after `V1_1`, chosen by the maintainers.
- **Untouched:** conflict detection and the existing `changes_made` string.

**Tech Stack:** C++ DuckDB extension (clang-format 11.0.1), sqllogictest. Line numbers are at upstream `ed31fddd`.

## Evidence

| Question | Answer | Evidence |
|---|---|---|
| How often does option A over-report? | 8 of 81 pairs; 8 of the 10 `deleted_from_table` pairs | Local run on `ed31fddd`, identical on DuckDB, SQLite and Postgres catalogs. Workload: DuckLake's default inlining limit of 10 (Altertable sets 0 unless a catalog opts in), small and large inserts, small deletes, MERGE upserts, an append-only table, flush every 3 rounds, `merge_adjacent_files` every 4, `rewrite_data_files` every 6, 12 rounds. Option A agrees on all 65 data pairs and all 8 maintenance pairs. It over-reports 7 `deleted_from_table+inline_flush` flushes and 1 `deleted_from_table` flush, and under-reports nothing. Synthetic, not production. Scripts: `.claude/logical-data-change/oracle/` |
| Can a token rule fix A? | No | A flush of only inlined deletes records `deleted_from_table:T` alone (`ducklake_transaction.cpp:1086-1130`: `inline_flush` only for data files with a begin snapshot). Snapshot 76 deleted 100 rows and snapshot 78 changed none; both are `deleted_from_table:1`. A real delete after a flush in one transaction records `deleted_from_table,inline_flush`, like 7 pure flushes. The catalog cannot tell them apart later: a deletion vector's `begin_snapshot` can predate its commit (`x4_dv_twice`), and inlined deletes have no file |
| Does option B work? | Yes, on `1a4d6ae2`; port to `ed31fddd` in progress | POC `option-b.diff`, 23 files, +470/−30 formatted. The whole suite passed under the default, Postgres, SQLite, Quack, `no_inline`, `deletion_vectors` and `ducklake_version` configs. `data_changes` matched the Definition on all 44 v6 scenarios. See "POC record" |
| What do released 1.1 binaries do with a catalog at the new version? | Refuse at ATTACH, readers included | `DuckLakeVersionFromString` throws `Unsupported ducklake_version '1.2-dev1'` (`ducklake_version.cpp:17-24`), with no options, `AUTOMATIC_MIGRATION`, `READ_ONLY` or a pinned `'1.1'`. Unverified for third parties: ducklake-sdk, brikk, pg_ducklake and moraine refusing an unknown version |
| Who classifies today? | Four consumers, and they disagree | datafusion-ducklake counts `rewrite_data_files` as a change and documents the flush over-report (`change_detection.rs:26-42`); moraine counts `rewrite_delete` and `inline_flush` as compaction (`operations.rs:145, 544-551`); viaduck treats `rewrite_delete` as a delete (`single_destination.py:535-539`); quack-on-demand counts `rewrite_delete` as a data touch (unverified in v11). No upstream work or Discussion on this as of 2026-10-20 |
| Token in `changes_made` or column? | Column | Both need a version bump: today's parser throws `Unsupported change type` on an unknown token (`ducklake_transaction_changes.cpp:67-73`), even in a concurrent writer's conflict check. A token also breaks version-unaware readers (datafusion-ducklake `snapshot_changes.rs:55`, moraine `has_unknown`) and needs a conflict-check rule. A column is ignored by both |
| Precedent? | A commit-level flag where physical actions cannot answer | Delta requires a per-file `dataChange` on add and remove (PROTOCOL.md). RFC #7503 (merged 2026-09-28) makes the commit-level `commitInfo.dataChange` required under `adaptiveMetadata`, because there "a reader tailing the log cannot recover the answer"; DuckLake has neither. #7524 filled the `commitLarge` path; #7658 reads the field behind an off-by-default config and records telemetry when it disagrees with the file actions. Paimon and Iceberg keep a per-snapshot `operation` (#8236; Iceberg `summary.operation`), but their snapshots belong to one table, while a DuckLake snapshot spans tables, so the value must be per table. `author`/`commit_message`/`commit_extra_info` also came with a version bump (`MigrateV02`, `ducklake_metadata_manager.cpp:383-397`) |
| Spec status | Fixed by ducklake-web#437, except two points | #437 lists every change type. It still says `deleted_from_table` means "the snapshot deleted data from the given table", which a flush of inlined deletes contradicts, and keeps the table's stated purpose "for easier conflict detection". ducklake-web#37 and #242 are open |

## Global Constraints

- `changes_made` and every conflict check stay byte-for-byte as today (`STRING_AGG(changes_made)` at `ducklake_metadata_manager.cpp:4727`). `tables_deleted_from` keeps counting `FLUSH` delete files; the new sets are separate.
- Write `data_changes` with `SQLString`, not `SQLStringOrNull` (`ducklake_metadata_manager.cpp:4697-4710`), so `''` stays distinct from `NULL`.
- Write it only on a catalog at the new version. Both version reads need it: `context.supports_v1_1_metadata` (`ducklake_transaction.cpp:1674`) and `ReadSupportsV1_1Metadata` (`ducklake_server_side_commit.cpp:546-554`). 1.0 and 1.1 catalogs keep five columns.
- Every insert into `ducklake_snapshot_changes` names its columns: the snapshot-0 insert (`ducklake_metadata_manager.cpp:237`), `WriteSnapshotChangesSql` (`:4707`) and three positional fixtures.
- Normative rules, from prior art: write `''` only when certain and over-report otherwise; `data_changes` is never read by conflict detection (delta-rs #4851); each entry maps to a token of the same commit for the same table, with `inserted_into_table` coming from `inserted_into_table` or `inlined_insert`, and `deleted_from_table` from `deleted_from_table` or `inlined_delete`. This subset rule lets a reader check the column against `changes_made`, as Delta's #7658 telemetry does against file actions.
- Every new or updated test pins `DUCKLAKE_VERSION`, because CI's `test/configs/ducklake_version.json` still sets 1.0 (`DuckLakeVersion.yml:70`). Pinning to a dev version is native (29 tests pin `'1.1-dev1'`).
- AGENTS.md style; `make format-fix`; the DuckDB, SQLite, Postgres and Quack backends.
- Upstream commits: a signed title line only. Never open a PR, issue or Discussion; draft text to files for Florian. No C-task PR before approval.

## Definition (logical entries written for table T)

| Entry | When (signals in `AddTableChanges`, `ducklake_transaction.cpp:1086-1130`) |
|---|---|
| `inserted_into_table:T` | A new in-memory data file without `begin_snapshot` (INSERT, `add_data_files`; in the catalog every file has one, probe `s02`); or `new_inlined_data && new_inlined_data->Count() > 0` |
| `deleted_from_table:T` | A `new_delete_files` entry with `source == REGULAR`; a REGULAR delete on a flushed file; `new_inlined_data_deletes` or `new_inlined_file_deletes`; or T in the state's `tables_deleted_from`, which `WriteSnapshotChanges` copies (`ducklake_transaction_state.cpp:392`; whole-file drops, filled only by `DropFile`; union it) |
| Neither | Compaction (exclusive with DML, `:421-424`), flushes (outputs have `begin_snapshot`, deletes have `source == FLUSH`), DDL, renames, zero-row writes, net-zero inserts |
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
4. Server-side commit (Quack) writes the same entries: it reaches the same `WriteSnapshotChanges` (`ducklake_server_side_commit.cpp:113-121`) but reads the version separately (`:546-554`). Owned by C2.
5. Concurrent commits with retries; each retry recomputes from a fresh copy (`ducklake_transaction_state.cpp:2662`). SQLite reports `database is locked`, so these cases go in their own file. Owned by C2.
6. `data_changes` entries are a subset of `changes_made` on every commit. Owned by C2.

## POC record (`.claude/logical-data-change/poc/`, on `1a4d6ae2`)

- With the version kept opt-in, only 3 existing tests change (error text). Making `1.2-dev1` the default breaks 5. Its 3 new tests fail without it.
- `''` survives a round trip through Postgres and SQLite as distinct from `NULL`.
- 1.0 and 1.1 migrate to `1.2-dev1` with `NULL` on old rows, including with `METADATA_SCHEMA`. A pinned 1.1 target stops at 1.1.
- Retries recompute `data_changes` with the final table ids, even when a concurrent CREATE remaps a CTAS. 200 tables in one commit list all ids.
- Two processes migrating one Postgres catalog at once never failed in 27 overlapping trials, the same as the 1.0 → 1.1 control (25 trials). A loser falls into the upstream attach retry (`ducklake_initializer.cpp:133-162`). SQLite was not run, and PVH#118 shows a migration race on Quack.
- **Open design points for the ask:**
  - While `1.2-dev1` is not `DUCKLAKE_LATEST_VERSION`, `AUTOMATIC_MIGRATION` on a `1.2-dev1` catalog fails with "Cannot downgrade" (`ducklake_initializer.cpp:242-247, 338-341`). This disappears once the new version is the latest.
  - The POC adds `data_changes` to `snapshots()` on 1.0 and 1.1 catalogs too (always `NULL`), which changes `SELECT *` for everyone.
  - A 1.1 Quack server lets a new client migrate the catalog, then refuses every data-only commit with `Unsupported ducklake_version '1.2-dev1'` (`ducklake_server_side_commit.cpp:99`, `:546-554`), while DDL commits still run client-side. Writes stop until the server is upgraded.

---

### Task S0: ducklake-web docs follow-up (independent of the Discussion)

- [ ] After ducklake-web#437 merges, write `scratchpad/plans/ducklake_web_snapshot_changes_flush.md` against it, adding only what it lacks: the flush of inlined deletes is recorded as `deleted_from_table`, with or without `inline_flush`; the option A classification table; the `snapshots()` keys against the stored tokens (`tables_inserted_into` against `inserted_into_table`, `flushed_inlined` against `inline_flush`); and that DuckLake before 0.4 stored `flushed_inlined`. Reference ducklake-web#37 and #242. Florian opens it.

### Task D0 (gate): the ask

- [x] **Step 1: Size the gap.** Done locally (Evidence, first row). A production count stays open: the catalog cannot identify flush deletes afterwards, so it needs `max(snapshot_id)` recorded around every flush and `CHECKPOINT` (`ducklake_checkpoint.cpp:21-31`), on a catalog that opts into inlining.
- [ ] **Step 2: POCs on the latest main.** B (column) rebased onto `ed31fddd`, and B2 (separate table, no bump). Each must pass the CI configs and show zero over-reports and zero under-reports against the content oracle on all three catalogs; B2 must also show what an unmodified 1.1 binary does. Push each to a fork branch after an independent verification. No PR.
- [ ] **Step 3:** Write `scratchpad/plans/ducklake_data_changes_discussion.md`, Ideas category, about 1,400 characters, in the shape of #627 and #1077:
  - **One ask:** would you accept a nullable per-table logical change field in the next version, or should it wait for 2.0?
  - **Why:** `changes_made` exists for conflict detection, so a flush of inlined deletes records the same tokens as a DELETE; 8 of 10 `deleted_from_table` pairs in the measured workload were flushes; four consumers classify differently, and datafusion-ducklake documents the same problem.
  - **Proposed defaults:** the column, `NULL` for unknown and `''` for no change, the subset rule, and the fallback. The name is open because of the "Data Change Feed".
  - **Link** the POC branch, with a README holding the Definition, the rejected alternatives and the compatibility notes, so the post stays short.
- [ ] **Step 4:** Florian raises it through the DuckDB Labs channel and posts the Discussion after 1.1 is published. No C-task PR before a maintainer approves.

Rejected alternatives, for the POC README:
- A new token: needs the same version bump, breaks version-unaware readers, and needs a conflict-check rule.
- Recording `inline_flush` on deletion flushes, or reclassifying existing types: both change tokens that conflict detection reads (`ducklake_transaction_state.cpp:281-328`); v5 `explore_flush/race_replace` gave 169 rows instead of 99.
- A `source` column on `ducklake_delete_file` (the in-memory `DeleteFileSource` exists, `ducklake_data_file.hpp:31,57`): a delete file's `begin_snapshot` can predate its commit, so a reader cannot tell which files a snapshot committed, and inlined deletes have no file.
- A per-snapshot `operation`: a DuckLake snapshot spans tables, so the value must be per table.
- Filtering `table_changes`: it reports row images, and rewrites by design.
- A separate table: B2, pending its POC.

### Task C1 (option B, after approval): next catalog version and the column

**Files:**
- `src/include/common/ducklake_version.hpp`, `src/common/ducklake_version.cpp`: the new enum value, its string, `DUCKLAKE_LATEST_VERSION`, `ParseWritableDuckLakeVersion`. Moving the latest version is the native convention (`e0056364`, `e71e436b`). If the maintainers open the next version themselves, rebase on theirs.
- `src/storage/ducklake_initializer.cpp`: a `MigrateV11(bool from_dev)` step from `V1_1`, guarded by `catalog_version < target_version`; `ResolveTargetVersion` accepts the new version; a `SetVersionedMetadataManager` branch.
- `src/include/storage/ducklake_metadata_manager.hpp`, `src/storage/ducklake_metadata_manager.cpp`: the migration in the 1.1 shape, a `V1_2_ADDED_COLUMNS` entry with a probe like `HasV1_1Additions()` and the dev-catalog DDL skip of `MigrateV10(from_dev1)` (`:461-496`, `:618-628`); a virtual `GetSnapshotChangesTableStatement()`; the snapshot-0 insert names its columns and writes `''` at the new version.
- A versioned manager beside `src/metadata_manager/ducklake_metadata_manager_v1_1.cpp`, overriding that statement and `GetVersionString()`, instantiated for the four backends, and added to `src/metadata_manager/CMakeLists.txt`.
- `src/include/storage/ducklake_catalog.hpp`: a gate beside `SupportsV1_1Metadata()` (`:315-317`, which tests `>= V1_1`); `GetAllSnapshots` reads the column only behind it. Also `ducklake_metadata_info.hpp`, `ducklake_transaction_state.hpp` and `ducklake_server_side_commit.hpp` for the flag and the value.
- Tests whose error text changes when the latest version moves: `test/sql/attach/attach_ducklake_version.test`, `attach_ducklake_version_requires_migration.test`, `test/sql/migration/v11_dev1_to_v11.test`.

- [ ] **Step 1: Write the failing test** `test/sql/migration/v11_data_changes.test`:
  - a new catalog at the new version has the column last, and snapshot 0 is `''`;
  - catalogs created with `DUCKLAKE_VERSION '1.0'` and `'1.1'` have five columns and still commit;
  - each of them after `AUTOMATIC_MIGRATION` has the column with `NULL` on old rows; count columns by schema, or extra metadata schemas in a Postgres database break it.
- [ ] **Step 2: Confirm the failure.** Expected: FAIL on the missing column.
- [ ] **Step 3: Implement** as listed under Files.
- [ ] **Step 4: Verify** under the default, SQLite and Postgres configs, plus `'test/sql/migration/*'`.
- [ ] **Step 5: Commit** with the signed title `Add a data_changes column to ducklake_snapshot_changes`.

### Task C2 (option B, after approval): compute and write the entries

**Files:**
- `src/include/storage/ducklake_transaction_changes.hpp`: two sets in `TransactionChangeInformation`, `tables_data_inserted` and `tables_data_deleted`, and the matching sets in `SnapshotChangeInformation`.
- `src/storage/ducklake_transaction.cpp` (`AddTableChanges`): the Definition signals, leaving `deleted_data` as it is.
- `src/storage/ducklake_transaction_state.cpp` (`WriteSnapshotChanges`): build the string, union the state's `tables_deleted_from`, and pass the new-version flag.
- `src/storage/ducklake_server_side_commit.cpp`: the same flag on the server path.
- `ducklake_metadata_manager.cpp` (`WriteSnapshotChangesSql`, `:4707`): name its columns.
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
  - The default, SQLite, Postgres, Quack, `no_inline.json` and `deletion_vectors.json` configs.
  - `'test/sql/functions/*,test/sql/transaction/*,test/sql/data_inlining/*,test/sql/deletion_inlining/*,test/sql/compaction/*,test/sql/table_changes/*,test/sql/issues/*'`.
  - `changes_made` output byte-identical to main on the same scenarios.
  - The oracle workload on all three catalogs: zero over-reports and zero under-reports against the content diff.
- [ ] **Step 5: Commit** with the signed title `Record the logical data changes of each snapshot`.

### Task C3 (option B, after approval): expose `data_changes` in `snapshots()`

**Files:** `src/functions/ducklake_snapshots.cpp` (`GetSnapshotTypes` and `GetSnapshotValues`: a new `data_changes MAP(VARCHAR, VARCHAR[])` column, `NULL` when unknown, `{}` when `''`, parsed with `SnapshotChangeInformation::ParseChangesMade`); `src/functions/ducklake_expire_snapshots.cpp` (shares both).

- [ ] **Step 1: Write the failing test.** `SELECT snapshot_id, data_changes FROM dl.snapshots()` on the flush of inlined deletes: the flush snapshot is `{}`, while `changes` still shows `tables_deleted_from=[T]`.
- [ ] **Step 2: Implement**, then list the tests that break on the extra output column with `rg -l "(tables_inserted_into|tables_deleted_from|inlined_insert|inlined_delete|flushed_inlined)=\[" test/sql` plus `SELECT *` users of `snapshots()`, and update them. Name this behaviour change in the PR description.
- [ ] **Step 3: Commit** with the signed title `Show the logical data changes in ducklake_snapshots`.

### Task C4 (optional, later): change feed without maintenance

- [ ] Add `data_changes_only => true` to `ducklake_table_changes`/`_insertions`/`_deletions`. It keeps rows only from snapshots whose `data_changes` names the table, and keeps everything where `data_changes IS NULL`. The default is unchanged, so #1571/#1589 hold. Keep it out of the first ask.

### Consumers

- **Altertable:** inlining is off (`SET GLOBAL ducklake_default_data_inlining_row_limit = 0`, `settings.rs:83`) unless a catalog sets `databases.data_inlining_row_limit` (backend #4413). Only those catalogs see the flush over-report. How many opt in is not measured.
- **dbt adapter:** the open altertable-ai/dbt-altertable#45 would read a backend freshness function, so it needs neither option. Once B ships, a freshness reader would filter `snapshots()` on the column per table and use the fallback for `NULL`.
- **Downstream schema cache:** keeps its query. DDL must count, and over-counting only costs a refresh. Only correct its comment claiming user changes and maintenance never share a snapshot: `s07_mixed` shows `{inlined_insert=[2], flushed_inlined=[1]}`.
