## v6 defects, most serious first (claim, then evidence, then fix)

Code: upstream main `1a4d6ae2`. About 45 scenarios were executed on a release build of it, on fresh catalogs pinned to `DUCKLAKE_VERSION '1.1'`, with DuckDB-file metadata only. Probes are in `scratchpad/challenge-v6/` (`probes/sql`, `probes/out`, `probes.md`, `implementation.md`, `lakehouse.md`, `SUMMARY.md`).

1. **The 1.1-dev1 route is gone.**
   - `src/include/common/ducklake_version.hpp:15-28` ends at `V1_1 = 9`, which is `DUCKLAKE_LATEST_VERSION`. Commit `3a4b4845` ("Require migrating 1.1-dev1 catalogs to 1.1 and remove the in-place dev migration") landed with #1799 (merged 2026-10-17T05:40Z). `V1_1_DEV1_ADDED_COLUMNS` is now `V1_1_ADDED_COLUMNS` (`ducklake_metadata_manager.cpp:462`), and `MigrateV10Dev` is gone.
   - A 1.1 catalog runs no migration (`ducklake_initializer.cpp:279-282`), so the version string cannot say whether the column exists.
   - `WriteSnapshotChangesSql` is still positional (`ducklake_metadata_manager.cpp:4627`). Released 1.1 writers on DuckDB or SQLite metadata would therefore fail every commit; before 1.1 only dev binaries were exposed.
   - The cited precedent does not hold: `author`/`commit_message`/`commit_extra_info` came with `MigrateV02`, a version bump (`:387-397`).
   - Fix: a new catalog version, a `MigrateV11`, and the gate in both `ducklake_transaction.cpp:1658` and `ducklake_server_side_commit.cpp:545-552`. Code waits for maintainer agreement.

2. **A no-schema option covers most of the value, and v6 never weighed it.**
   - Classifying today's tokens never under-reports. It over-reports two cases: the flush of inlined deletes and U1.
   - The flush of inlined deletes writes `deleted_from_table:T` alone (`s03_flush_del`, snapshot 3: `{tables_deleted_from=[1]}`). That is the same as a real DELETE, with no `inline_flush` beside it.
   - The catalog cannot rebuild the difference: `ducklake_delete_file` has no source column, and a deletion vector's `begin_snapshot` can predate its commit (`x4_dv_twice`).
   - Fix: offer option A (the S0 classification, no schema change) next to option B (the column), and measure how often the flush of inlined deletes happens.

3. **Track 0's consumer rewrite would break the consumer.**
   - The downstream query is not a freshness query. It gates a schema-introspection cache, so DDL must count, and a row-level change feed misses CREATE and ALTER.
   - Over-counting there only costs a refresh, so the consumer needs neither option.
   - Its comment says user changes and maintenance never share a snapshot. That is false: `s07_mixed` shows `{inlined_insert=[2], flushed_inlined=[1]}`.
   - Its `NOT LIKE` prefix filter is safe only because `WriteSnapshotChanges` writes user tokens before maintenance tokens (`ducklake_transaction_state.cpp:414-426`).
   - Fix: keep the query and correct the comment.

4. **The consumer evidence is overstated.**
   - Three consumers classify maintenance, and they disagree on `rewrite_delete`: quack-on-demand `BranchChanges.scala` counts it as a data touch, moraine `operations.rs` as compaction, viaduck `single_destination.py` as a delete.
   - The others do not classify maintenance. ducklake-cdc H-011 is about vocabulary coverage; pg_ducklake uses `ParseChangesMade` for conflict checks.
   - As a writer, moraine records flushes as `merge_adjacent` and inline inserts as `inserted_into_table`. delta_export always writes `dataChange: true`.

5. **Mechanics drifted.**
   - `GetSnapshotChangesTableStatement` does not exist. The table is created inline (`ducklake_metadata_manager.cpp:285`), and the pattern to follow is `GetSchemaTableStatement()` (`:248`, overridden in `ducklake_metadata_manager_v1_1.cpp:10`).
   - New locations:

     | Item | Location |
     |---|---|
     | snapshot-0 insert | `:238` |
     | `WriteSnapshotChangesSql` | `:4624` |
     | `STRING_AGG` | `:4647` |
     | `AddTableChanges` | `ducklake_transaction.cpp:1073` |
     | `tables_deleted_from` copy | `ducklake_transaction_state.cpp:392` |
     | compaction exclusivity | `ducklake_transaction_state.cpp:423` |
     | `ParseChangesMade` | `ducklake_transaction_changes.cpp:121` |

   - Missing positional fixture: `benchmark/micro/fixtures/snap100k.benchmark.in:15`.
   - The `probes4/setup.sql` replay no longer exists; the C2 test now lists its scenarios.

6. **Guardrails from prior art are missing.**
   - Two-sided wording: Delta RFC #7503 says "must be false when … must be true otherwise".
   - Over-report when unsure.
   - Never read by conflict detection: delta-rs #4851, where a checker trusting `dataChange=false` duplicated rows.
   - `data_changes` entries are a subset of `changes_made` entries. That answers #1029's "double-source of truth".

7. **C4 should leave the first ask.** The research agent called it redundant because row lineage keeps row ids through compaction. That is not established: the v4 change-feed query still needed a `rewrite_delete` filter, and #1589 only fixed rows being reported twice. Fix: keep C4 optional, after D0, and re-measure rewrite snapshots on main first.

## What survived

- **The Definition holds on main for every probed case:**
  - DML, MERGE, TRUNCATE, CTAS (final id) and `add_data_files`;
  - merge adjacent, rewrite, expire, cleanup and flush;
  - flush plus DML in either order (`x3`);
  - a mixed flush on A with DML on B (`s07`).
- **Deletion vectors add no new path.** The only maintenance that writes a delete file is the flush of inlined deletes (puffin, `FLUSH`). `rewrite_data_files` writes none, and `merge_adjacent_files` skips files that have deletes. `DeleteFileSource` is still `REGULAR`/`FLUSH` (`ducklake_data_file.hpp:31-34`), and 1.1 adds no change type.
- **The column beats the token.** moraine fails closed on unknown tokens (`has_unknown`), and datafusion-ducklake errors on them (`snapshot_changes.rs:55`).
- **Nobody else is working on this** (searched 2026-10-19).
- **The closest precedents are fresh and nullable:**
  - Delta's commit-level `commitInfo.dataChange` (#7524, merged 2026-08-26), optional, with readers falling back to per-file flags;
  - Paimon's nullable `operation` beside `CommitKind` (#8236, merged 2026-06-23).

## Bugs confirmed on `1a4d6ae2`

- **U1:** `s05_u1` gives snapshot 2 `{inlined_insert=[1]}` for an insert deleted in the same transaction.
- **`BEGIN; RESET PARTITIONED BY; INSERT; COMMIT`** throws `INTERNAL Error: ... File should either not be partitioned, or have partition values` from `WriteNewDataFilesWithAppender`, and invalidates the database (`s06b_fresh`). Nobody has reported it. The error is retried 10 times before it surfaces.

## Not verified

- The Postgres, SQLite and Quack backends, and concurrent commits with retries.
- Whether 1.1 is published on 2026-10-21 (no tag yet), and how the maintainers will name the next version.
- How often the flush of inlined deletes happens on real catalogs.
