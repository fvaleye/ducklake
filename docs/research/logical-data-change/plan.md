# DuckLake Data-Change Marker Implementation Plan (v12, 2026-10-21)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Readers can tell, per snapshot and per table, whether a commit logically inserted or deleted data, or only did maintenance (flushes, compaction). Today they guess from physical change types.

**Decision (2026-10-20):** Florian chose option B, a column in the next catalog version. Option A stays as the reader fallback for `NULL` rows and as a docs fix (S0). A second direction, B2, stores the same value in a separate table without a version bump, so that 1.1 binaries keep working. Both POCs are exact on `bbd42124`, upstream main on 2026-10-21; B stays the one ask, and B2 is the fallback named in the POC README (see "Which to ask for").

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
   - **Frame it as a building block of a roadmap item.** "Materialized views and incremental maintenance" is under "Future Work / Looking for Funding", which says "If you are interested in prioritizing some of this work, get in touch with DuckLabs" (https://ducklake.select/roadmap/, read 2026-10-21). Incremental maintenance must know which snapshots changed a table's data; this is that signal. Whether Altertable sponsors or prioritises it is Florian's call.
   - **No competing effort** as of 2026-10-21. Code overlap only: PVH#122 edits the same change sets (`ducklake_transaction_changes.hpp`, `AddTableChanges`) to read stats per changed table, so whichever lands second rebases.
2. **No C-task PR before a maintainer approves the design.** The POC stays a branch on the fork, linked as evidence.
3. **Post after 1.1 is published** (2026-10-21, ducklake-web#437), per #1703.
4. **Ask which version carries it,** and expect 2.0 as an answer. `1.2-dev1` below is a placeholder.
5. **Keep it short, with one ask.** The posts that drew a design reply were short with one proposal: #627 (1,368 characters; pdet came back after "a quick internal discussion" with a more generic `min_merge_size`/`max_merge_size`) and #1077 (1,353 characters, led to #1102). The unanswered #1630 is 2,145. Say which parts are negotiable: the name, the encoding, the version.
6. **Pre-empt the recurring review asks** once code is allowed (`~/.claude/skills/ducklake-dev/references/maintainers.md`): tests enumerated and split by scenario, SQL in the metadata manager per backend, every behaviour change named (`snapshots()` gaining a column is one), throw on invalid state. Expect the bot to probe for races, as it did on #1429.

## What changed since v11

- **Both POCs done on `bbd42124`** (upstream main on 2026-10-21, 57 commits after `ed31fddd`, none touching the change-recording files), independently verified and pushed: B1 (the column) and B2 (a separate table, exact but with a grant step). See "POC results".
- **B1 follows the maintainers' shape:** `1.2-dev1` is the latest version, as opening a dev version is upstream (e0056364, #1017), so `AUTOMATIC_MIGRATION` works; one `SupportsV1_2Metadata()` gate in the style of `SupportsV1_1Metadata()`; `data_changes` is a `Value`, so `NULL` and `''` stay distinct (Mytherin on #335).
- **Final verification found one untested case,** the flush of pending deletes alone, which is the motivating example. It is now tested, and a mutation that reintroduces option A's over-report fails on it.
- **CI parity:** every workflow sets `DUCKLAKE_CI=1`, and 21 tests `require-env DUCKLAKE_CI`, so local runs without it skipped them. With it, `postgres_dev1_migration_skips_present_ddl.test` failed on B1 (its second migration re-added `data_changes`); pinning `DUCKLAKE_VERSION '1.1'`, as e71e436b did for `1.1-dev1`, fixes it. Run every local suite with `DUCKLAKE_CI=1`.
- **Regression gate:** `REGRESSION_LIMIT = 1.10` (duckdb `scripts/regression/test_runner.py:25`, 3% is only the noise band), confirmed by a rerun (`.github/regression/run.py:206`). Building the `NULL` map type per row put `snapshots_listing_changes` at 1.110; building it once puts it at 1.045 (25 interleaved pairs).
- **B1 rebased onto `ea543c71`** (upstream main on 2026-10-21, 17 commits after `bbd42124`, none touching a B1 file) at `7b266fb8`, and simplified: inserts reuse `tables_inserted_into` and `tables_inserted_inlined`, an unreachable `Count() > 0` check and an inert `{IF_NOT_EXISTS}` are gone, and the new tests follow upstream's unpinned pattern with `test/configs/ducklake_version.json`.
- **The flush and delete probe** now covers Postgres too, with the same tokens as DuckDB and SQLite.
- **PVH#58 corrected:** it ignores unexpected tables but requires every table the current code lists, so a table added without a version bump must stay out of `GetCreateTableStatements()`.

## What changed since v10 (adversarial review and probes, `scratchpad/v10-review/`, `scratchpad/probe-flushdel/`)

- **Route:** the Discussion-only route is likely to meet silence (numbers above); #1102 is the precedent to follow.
- **S0 shrinks:** pdet's ducklake-web#437 ("Update documentation for DuckLake 1.1", opened 2026-10-20) already documents every change type and marks `compacted_table` as written only by older versions. It copies `docs/stable` to `docs/1.0` and edits `stable` in place, so there is no preview tree to wait for.
- **Altertable claim corrected:** backend #4413 kept `SET GLOBAL ducklake_default_data_inlining_row_limit = 0` (`settings.rs:83`) and made inlining opt-in per catalog (`databases.data_inlining_row_limit`). The over-report reaches only catalogs that opt in.
- **Reader fallback fixed:** datafusion-ducklake writes `changes_made` as `NULL` first and appends later (`metadata_writer_postgres_single.rs:521-536`), so a missing or `NULL` `changes_made` must count as a change.
- **A fourth classifier:** datafusion-ducklake `src/change_detection.rs` (2026-10-16) documents the same over-report (lines 26-32) and counts `rewrite_data_files` as a change (lines 40-42).
- **One more collision:** `BEGIN; CALL ducklake_flush_inlined_data(...); DELETE ...; COMMIT` records `deleted_from_table:T,inline_flush:T` for a real 1-row delete, the same tokens as the 7 flushes that changed nothing (`probe-flushdel`, snapshot 7, DuckDB and SQLite).
- **Not reproduced:** datafusion-ducklake's claim (`change_detection.rs:38-40`) that a DELETE committed with a flush records only `inline_flush`. Four orders on DuckDB, SQLite and Postgres catalogs all recorded the delete, with identical tokens on all three (`probe-flushdel`). Quack not run.
- **Bot fork:** PeterVanHolland/ducklake has 59 open PRs that reach upstream in batches (#1817). Relevant: PVH#114 rewrites the same `RetryOnError` line as the retry fix; PVH#58 (head `c5499053`) validates metadata column names, order and types per catalog version on attach. It ignores tables it does not expect, but requires every table that `GetCreateTableStatements()` lists, throwing `InvalidInputException` "the metadata catalog has no table" otherwise. PVH#122 reworks the stats read for the tables a commit changes; PVH#118 retries an attach whose migration races another.
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
| Does option B work? | Yes, on `ea543c71` | B1, `fvaleye/fl/record-logical-data-changes` at `7b266fb8`, 33 files, +745/−63; the oracle was rerun independently on `17f614e6` (same recording logic) and the tests with CI's environment on `7b266fb8`. Zero over-reports and zero under-reports on all three catalogs. See "POC results" |
| What do released 1.1 binaries do with a catalog at the new version? | Refuse at ATTACH, readers included | `DuckLakeVersionFromString` throws `Unsupported ducklake_version '1.2-dev1'` (`ducklake_version.cpp:17-24`), with no options, `AUTOMATIC_MIGRATION`, `READ_ONLY` or a pinned `'1.1'`. Unverified for third parties: ducklake-sdk, brikk, pg_ducklake and moraine refusing an unknown version |
| Who classifies today? | Four consumers, and they disagree | datafusion-ducklake counts `rewrite_data_files` as a change and documents the flush over-report (`change_detection.rs:26-42`); moraine counts `rewrite_delete` and `inline_flush` as compaction (`operations.rs:145, 544-551`); viaduck treats `rewrite_delete` as a delete (`single_destination.py:535-539`); quack-on-demand counts `rewrite_delete` as a data touch (unverified in v11). No upstream work or Discussion on this as of 2026-10-21: none of the 57 commits up to `bbd42124` touch the change-recording files; issue, PR and Discussion searches in duckdb/ducklake, ducklake-web and the PVH fork find nothing on point (closest: Discussion #136, returning the snapshot id) |
| Token in `changes_made` or column? | Column | Both need a version bump: today's parser throws `Unsupported change type` on an unknown token (`ducklake_transaction_changes.cpp:67-73`), even in a concurrent writer's conflict check. A token also breaks version-unaware readers (datafusion-ducklake `snapshot_changes.rs:55`, moraine `has_unknown`) and needs a conflict-check rule. A column is ignored by both |
| Precedent? | Every surveyed format marks delete materialization and compaction as no data change; DuckLake alone does not | **Paimon** is the closest shape: the physical `commitKind` (APPEND/COMPACT/OVERWRITE/ANALYZE, `Snapshot.java:511-527`, predates #8236) plus a separate nullable logical `operation` added by #8236 (2026-06-23), which the spec says "are not interchangeable" (`snapshot.md:79-80`). **Iceberg** `summary.operation`, where `replace` means no data change; required from v2, and Java reads a missing one as `overwrite` (`spec.md:1150-1155`, `SnapshotParser.java:161-167`). **Delta** requires a per-file `dataChange` on add and remove (PROTOCOL.md:577, :624); RFC #7503 (2026-09-28), part of the Iceberg V4 Adaptive Metadata RFC (#6640) and not in PROTOCOL.md, adds a required commit-level boolean under `adaptiveMetadata`, because those commits write no file actions to the log. #7658 writes it for every table by default but reads it only behind an off-by-default flag. **Lance** has a `Rewrite` operation that "does not change the data" (`transaction.proto:411-414`); **Hudi** a timeline action plus a nullable `operationType`. Iceberg multi-table commits make one snapshot per table, while a DuckLake snapshot spans tables, so the value must be per table. delta-rs #4851 (open) is the warning: its conflict check skips removes with `data_change=false`, which duplicates rows under concurrent OPTIMIZE. `author`/`commit_message`/`commit_extra_info` also came with a version bump (`MigrateV02`, `ducklake_metadata_manager.cpp:383-397`). Survey: `.claude/logical-data-change/review/lakehouse.md` |
| Spec status | Fixed by ducklake-web#437, except two points | #437 lists every change type. It still says `deleted_from_table` means "the snapshot deleted data from the given table", which a flush of inlined deletes contradicts, and keeps the table's stated purpose "for easier conflict detection". ducklake-web#37 and #242 are open |

## Global Constraints

- `changes_made` and every conflict check stay byte-for-byte as today (`STRING_AGG(changes_made)` at `ducklake_metadata_manager.cpp:4727`). `tables_deleted_from` keeps counting `FLUSH` delete files; the new sets are separate.
- Write `data_changes` with `SQLString`, not `SQLStringOrNull` (`ducklake_metadata_manager.cpp:4697-4710`), so `''` stays distinct from `NULL`.
- Write it only on a catalog at the new version. Both version reads need it: `context.supports_v1_1_metadata` (`ducklake_transaction.cpp:1674`) and `ReadSupportsV1_1Metadata` (`ducklake_server_side_commit.cpp:546-554`). 1.0 and 1.1 catalogs keep five columns.
- Every insert into `ducklake_snapshot_changes` names its columns: the snapshot-0 insert (`ducklake_metadata_manager.cpp:237`), `WriteSnapshotChangesSql` (`:4707`) and three positional fixtures.
- Normative rules, from prior art: a writer at the new version always writes the column, `''` when nothing changed (Delta's required-under-feature rule), and writes `''` only when certain, over-reporting otherwise; a reader treats an entry kind it does not know as a logical change to that table, never as an error (Iceberg reads a missing operation as `overwrite`); `data_changes` is never read by conflict detection (delta-rs #4851); each entry maps to a token of the same commit for the same table, with `inserted_into_table` coming from `inserted_into_table` or `inlined_insert`, and `deleted_from_table` from `deleted_from_table` or `inlined_delete`. This subset rule lets a reader check the column against `changes_made`, as Delta's #7658 telemetry does against file actions.
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

## POC results on `bbd42124` (`.claude/logical-data-change/verify-b1/final/`, `verify-b2/`)

Both directions were built by one subagent and verified by another, which reran every CI config, the content oracle on all three catalogs, and its own probes. The only suite failures are `issue_1309.test:65` and `view/view_schema_cache_eviction.test_slow:89` under `no_inline`, which fail on `ed31fddd` and `bbd42124` too.

### B1: the column (`fvaleye/fl/record-logical-data-changes`, `7b266fb8` on `ea543c71`)

- **Shape:** one commit, 33 files, +745/−63. `1.2-dev1` is `DUCKLAKE_LATEST_VERSION`. `V1_2_ADDED_COLUMNS` in `MigrateV11`, a versioned manager `ducklake_metadata_manager_v1_2.cpp` created through `MakeVersionedMetadataManager` (which replaces the per-backend `if` chain), and one `SupportsV1_2Metadata()` gate for the snapshot-0 insert, `GetAllSnapshots` and the commit. One new set, `tables_data_deleted`; inserts reuse `tables_inserted_into` and `tables_inserted_inlined`. New tests: `ducklake_data_changes_{dml,maintenance,concurrency}.test` (unpinned, listed in `test/configs/ducklake_version.json`), `migration/v11_data_changes.test` (through `{DUCKLAKE_CONNECTION}`, so every backend migrates; `v10_row_group_count.test` runs the 1.0 chain), `quack/server_side_commit_data_changes.test`. Tests about earlier versions pin `DUCKLAKE_VERSION '1.1'` (`v11_dev1_to_v11`, `postgres_dev1_migration_skips_present_ddl`).
- **Suites,** on `7b266fb8` with CI's environment (`DUCKLAKE_CI=1`, which every workflow sets): SQLite 878, Postgres 946 and Quack 924 of 924 pass. default (1,248), `deletion_vectors` and `ducklake_version` fail only `issue_843_postgres_legacy_inlined_types` (needs a Postgres catalog; the local build has the Postgres scanner, those jobs do not) and httpfs's `http_logging` (a remote server's headers); `no_inline` also fails `issue_1309`, as on main. ConfigTests (DuckDB's suite on a DuckLake catalog, 5,454 cases): 16 failures, all in tests of Quack, httpfs and SQLite built locally, which `ConfigTests.yml` does not build (not rerun on main). The builder's run without `DUCKLAKE_CI` agrees: default 946, SQLite 875, Postgres 923, `deletion_vectors` 941, `ducklake_version` 885, Quack 924.
- **Oracle,** rerun independently on `17f614e6` (the recording logic of `7b266fb8`) on each of DuckDB, SQLite and Postgres: 231 and 654 per-kind pairs on two workloads, plus 81 and 158 on the plan workloads, with zero over-reports, zero under-reports and zero subset violations. `changes_made` is byte-identical to `bbd42124` on 79 and 138 snapshots (`.claude/logical-data-change/verify-b1/final17/`).
- **Migration,** 12 runs (three backends, 1.0 and 1.1, with `AUTOMATIC_MIGRATION` alone or a pinned `1.2-dev1`): data unchanged, old rows `NULL`, new rows filled, `''` stored as such on SQLite and Postgres. `bbd42124` refuses the migrated catalog with `Unsupported ducklake_version '1.2-dev1'`, `READ_ONLY` included, not with a positional insert error.
- **Tests catch regressions:**
  - Counting every delete file as logical (option A's over-report) fails the flush of pending deletes alone (`ducklake_data_changes_maintenance.test`, snapshot 27). At `ec419037` nothing caught it; `95825855` adds the case.
  - Dropping the REGULAR delete signal fails the flush-then-delete snapshot (snapshot 9).
  - Ignoring regular delete files fails the maintenance and Quack tests.
- **`snapshots()` cost against the regression gate** (limit 1.10), on CI's `snap100k` fixture (100,001 snapshots), main against B1, 21 interleaved pairs: 1.029 with `data_changes` `NULL` (the gate's case), 1.102 with it filled on every snapshot. Building a map is most of the cost, and snapshots repeat the same text, so each call builds one map per distinct text and reuses it (0.684 of the cost without reuse; 1.022 when every text differs). Without that and the once-built `NULL` type, the ratios were 1.110 and 1.60. A profile (`sample`) showed allocation and `Value`/`LogicalType` construction, not parsing: a dedicated parser saved only 4%. Postgres catalogs, before reuse: 1.069 `NULL`, 1.545 filled. Scripts and raw pairs: `.claude/logical-data-change/verify-b1/final/bench/`.
- **Carried from the `1a4d6ae2` POC:** two processes migrating one Postgres catalog at once never failed (27 trials).
- **Behaviour changes to name in the ask:**
  - Every new catalog is `1.2-dev1`, and `AUTOMATIC_MIGRATION` alone upgrades 1.0 and 1.1 catalogs, which released binaries then refuse. Opening a dev version upstream does the same (e0056364, #1017).
  - `snapshots()` and `ducklake_expire_snapshots` gain `data_changes` on 1.0 and 1.1 catalogs too (always `NULL`), because they share `GetSnapshotTypes` (`ducklake_expire_snapshots.cpp:22,80` at `bbd42124`). This changes `SELECT *` for everyone. No table function changes its columns by catalog version, so keep it rather than vary it.
  - Upgrade Quack servers first. A new server with an old client works (7 of 7 commits on a 1.1 catalog). With an old server, once a new client migrates the catalog, 7 of 12 data-only commits fail with `Unsupported ducklake_version '1.2-dev1'` (`ducklake_server_side_commit.cpp:99`, `:546-554`); DDL, flushes, mixed transactions and the first inlined delete of a Parquet row still succeed, because they commit client-side. A new client creating a catalog on an old server fails its first INSERT.

### B2: a separate table, no version bump (`fvaleye/fl/record-logical-data-changes-in-side-table`, `89488e2c` on `bbd42124`)

- **Final state:** one commit, 25 files, +709/−21. Default, SQLite, Postgres and Quack (972 of 972) pass. An independent verifier measured concurrent opt-ins at 80 of 80 on SQLite, 120 of 120 on Quack and 120 of 120 on Postgres, and the oracle at 323 of 323 per kind on all three catalogs. A permission failure surfaces after the attach retry's 3 s cap, with the schema-qualified DDL.

- **Shape:** `ducklake_snapshot_data_changes(snapshot_id BIGINT PRIMARY KEY, data_changes VARCHAR)`. New catalogs create it; a 1.0 or 1.1 catalog gets it through a `RECORD_DATA_CHANGES` attach option. A commit appends one INSERT only when the table exists, and runs no DDL. A missing row means unknown, so the reader fallback is the same.
- **First design rejected (`37da2154`):** creating the table on the commit path broke DML-only Postgres roles ("permission denied for schema"), retried a failing commit 11 times (9.74 s), and wrote DDL into a `log_statement='ddl'` log.
- **Redesign (`1fc53fe2`):** every suite passes, Quack 910 of 910. Oracle on new catalogs: 323 of 323 per-kind pairs at inlining 10 on all three catalogs, 303 of 303 on Postgres at inlining 0, and 81 of 81 on the plan workload. `changes_made` is identical. A failing commit is attempted once and leaves no orphan rows, and `log_statement='ddl'` logs nothing. Eight processes opting in at once succeeded 120 of 120 times over 15 trials. Released 1.0 and 1.1 binaries keep working on an opted-in catalog. On Quack, rows appear exactly for commits made by a part that records, and no wrong row appeared.
- **Costs that stay:**
  - Grants: `GRANT ... ON ALL TABLES` given before the opt-in does not cover a new table. Until an admin grants again, DML-only roles fail every commit, and read-only roles fail `snapshots()` and expiry. The column keeps the table's grants.
  - Silence: a session attached before the opt-in records nothing until it re-attaches, because the table is probed once per attach.
  - Unversioned: catalogs of one version may or may not have the table, so it must stay out of `GetCreateTableStatements()`, which per-version validation such as PVH#58 reads.
  - A failed opt-in cannot tell a missing privilege from another attach creating the table at the same time, so it goes through the attach retry and shows its hint after about 3 s. Classifying by message was tried and dropped: matching "already exists" and "duplicate key" missed SQLite's `database is locked` and DuckDB's write-write conflict, and 46 of 80 concurrent SQLite opt-ins failed permanently.
  - Still a spec change (pdet on #1815).
- **Fixes after verification:** the table left `GetCreateTableStatements()`, and new catalogs create it through the same function as the opt-in. With PVH#58 built on top, B2 attaches 1.0, 1.1 and new catalogs on DuckDB, SQLite and Postgres, with or without the opt-in, and the oracle still agrees (PVH#58 alone fails `metadata/view_load_error.test`, with or without B2). `InlinedDeletionTableExists` became `MetadataTableExists`. The Quack server looks the table up directly instead of scanning `duckdb_tables()` (4.38 ms per commit with 3,000 tables before; no measurable cost after, about 11.1 ms per commit both ways). A failed opt-in prints the schema-qualified DDL. The opt-in test runs on every backend, Quack included.

### Which to ask for

B, as Florian decided. B2's only advantage is that released binaries keep attaching, and it pays for that with a grant step, a silent window and an unversioned table. The POC README names B2 as the fallback if the maintainers want the field before the next version, so the post keeps one ask.

---

### Task S0: ducklake-web docs follow-up (independent of the Discussion)

- [ ] After ducklake-web#437 merges, write `scratchpad/plans/ducklake_web_snapshot_changes_flush.md` against it, adding only what it lacks: the flush of inlined deletes is recorded as `deleted_from_table`, with or without `inline_flush`; the option A classification table; the `snapshots()` keys against the stored tokens (`tables_inserted_into` against `inserted_into_table`, `flushed_inlined` against `inline_flush`); and that DuckLake before 0.4 stored `flushed_inlined`. Reference ducklake-web#37 and #242. Florian opens it.

### Task D0 (gate): the ask

- [x] **Step 1: Size the gap.** Done locally (Evidence, first row). A production count stays open: the catalog cannot identify flush deletes afterwards, so it needs `max(snapshot_id)` recorded around every flush and `CHECKPOINT` (`ducklake_checkpoint.cpp:21-31`), on a catalog that opts into inlining.
- [x] **Step 2: POCs on the latest main.** Done: B1 `7b266fb8` on `ea543c71` and B2 `89488e2c` on `bbd42124`, both on fvaleye (see "POC results"). B (column), and B2 (separate table, no bump). Each must pass the CI configs and show zero over-reports and zero under-reports against the content oracle on all three catalogs; B2 must also show what an unmodified 1.1 binary does. Push each to a fork branch after an independent verification. No PR.
- [ ] **Step 3:** Write `scratchpad/plans/ducklake_data_changes_discussion.md`, Ideas category, about 1,400 characters, in the shape of #627 and #1077:
  - **One ask:** would you accept a nullable per-table logical change field in the next version, or should it wait for 2.0?
  - **Why:** `changes_made` exists for conflict detection, so a flush of inlined deletes records the same tokens as a DELETE; 8 of 10 `deleted_from_table` pairs in the measured workload were flushes; four consumers classify differently, and datafusion-ducklake documents the same problem.
  - **Proposed defaults:** the column, `NULL` for unknown and `''` for no change, the subset rule, and the fallback. The name is open because of the "Data Change Feed".
  - **Link** the POC branch, with a README holding the Definition, the rejected alternatives and the compatibility notes, so the post stays short.
- [ ] **Step 4:** Florian raises it through the DuckDB Labs channel and posts the Discussion after 1.1 is published. No C-task PR before a maintainer approves.

Rejected alternatives, for the POC README:
- A new token: needs the same version bump, breaks version-unaware readers, and needs a conflict-check rule.
- The column without a version bump: released binaries insert positionally (`ducklake_metadata_manager.cpp:4722`). After `ALTER TABLE ducklake_snapshot_changes ADD COLUMN data_changes VARCHAR` on a 1.1 catalog, an `ed31fddd` binary fails every commit with `table "ducklake_snapshot_changes" has 6 columns but 5 values were supplied` (probe on a DuckDB catalog, 2026-10-21).
- Recording `inline_flush` on deletion flushes, or reclassifying existing types: both change tokens that conflict detection reads (`ducklake_transaction_state.cpp:281-328`); v5 `explore_flush/race_replace` gave 169 rows instead of 99.
- A `source` column on `ducklake_delete_file` (the in-memory `DeleteFileSource` exists, `ducklake_data_file.hpp:31,57`): a delete file's `begin_snapshot` can predate its commit, so a reader cannot tell which files a snapshot committed, and inlined deletes have no file.
- A per-snapshot `operation`: a DuckLake snapshot spans tables, so the value must be per table.
- Filtering `table_changes`: it reports row images, and rewrites by design.
- A separate table without a version bump (B2): exact, and kept as the fallback. It needs a grant step after the opt-in, records nothing in sessions attached before it, and leaves the table outside the catalog version.

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
