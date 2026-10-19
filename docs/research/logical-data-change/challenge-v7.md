## v7 defects and POC results, most serious first (claim, then evidence, then fix)

Code: upstream main `1a4d6ae2`. Probes ran on DuckDB-file metadata only, because this build has no postgres_scanner. Reports, probes and patches are in `scratchpad/challenge-v7/` and `scratchpad/poc/`.

1. **Released 1.1 binaries refuse to attach the new version.** v7 said they would fail at commit.
   - `DuckLakeVersionFromString` throws `Unsupported ducklake_version '1.2-dev1'` (`src/common/ducklake_version.cpp:17-24`).
   - A catalog with the column and version `1.2-dev1` fails in every mode tried: with no options, `AUTOMATIC_MIGRATION`, `READ_ONLY`, and a pinned `DUCKLAKE_VERSION '1.1'` (`challenge-v7/probes/out/compat_duck.txt`). Readers fail too.
   - The POC binary's real `1.2-dev1` catalog gives the same result (`poc/option-b-out/compat.txt`).
   - Leaving the version at `'1.1'` with the column added makes every commit fail with "has 6 columns but 5 values were supplied". That is a reason never to add the column without a version bump.
   - Third-party writers: ducklake-sdk, brikk, pg_ducklake's native writer (it accepts exactly "1.0") and moraine refuse an unknown version. datafusion-ducklake does not check it and writes named columns. So `NULL` for new snapshots comes only from writers like datafusion-ducklake. v7 wrongly said pg_ducklake writes `NULL`.
   - Fix: rewrite the compatibility row and the D0 bullet.

2. **C1 is the maintainers' job.**
   - Every catalog version was opened by a maintainer: Mytherin #252 (0.3-dev1), and pdet #506, #784, #966, #1017, #1799.
   - The only outside schema change, #1102 by redox, tried `1.1-dev2`. pdet replied "you can just keep it under `1.1-dev1`".
   - Fix: ask which dev version to target, then rebase the column and migration onto it.

3. **The gate and migration need care.**
   - `SupportsV1_1Metadata()` returns `ducklake_version >= DuckLakeVersion::V1_1` (`ducklake_catalog.hpp:312-314`), which is also true on the new version. B needs its own predicate there and in the server-side commit's version read.
   - The initializer checks `catalog_version >= target_version` only once (`ducklake_initializer.cpp:279`). An unguarded `MigrateV11` would therefore move a 1.0 catalog pinned to 1.1 past its target.
   - Fix: guard `MigrateV11` with `catalog_version < target_version` and test it.

4. **The subset rule is false as written.**
   - Inlined writes record `inlined_insert:T` and `inlined_delete:T` (an inlined UPDATE records both), while `data_changes` writes `inserted_into_table:T` and `deleted_from_table:T`.
   - Fix: map `inlined_insert` to `inserted_into_table` and `inlined_delete` to `deleted_from_table`. Under that mapping the rule held on every snapshot of the 44 POC scenarios.

5. **D0 Step 1 cannot ask the caller.**
   - `CHECKPOINT` runs the flush internally (`src/storage/ducklake_checkpoint.cpp:20-34`) and flushes every table in one snapshot.
   - A flush that only writes deletes reports 0 flushed rows.
   - Inlined delete rows are physically removed, and later flushes merge and drop earlier delete files.
   - Fix: record `max(snapshot_id)` around every flush and `CHECKPOINT` call, and report the count as a lower bound.

6. **The S0 target and citations are wrong.**
   - duckdb/ducklake#924 is about temporal partition values; the right issues are ducklake-web#37 and ducklake-web#242.
   - `docs/stable/specification` is the 1.0 tree. Past releases copied a preview tree and then swapped it in (ducklake-web #297, #299, #317).
   - Fix: open S0 after the 1.1 swap, and document both the `snapshots()` keys and the stored tokens.
   - Also document both flush spellings: `flushed_inlined` (0.3 and earlier) and `inline_flush` (since 2026-01-30). DuckLake 1.0 already wrote all five missing tokens.

7. **The dbt consumer is obsolete.** altertable-ai/dbt-altertable#45 reads a backend freshness function now. It needs neither option nor the v4 change-feed query.

## What survived

- **Option A never under-reports.** No counterexample in about 60 scenarios across the probes and the POC.
  - `BEGIN; INSERT; flush; COMMIT` writes `{inlined_insert}`, because the flush skips transaction-local rows.
  - `CHECKPOINT` runs each step on its own connection (`ducklake_checkpoint.cpp:20-34`), so it never shares a snapshot with DML.
  - There is no auto-flush.
  - ADD COLUMN DEFAULT, ALTER TYPE, DROP TABLE and DROP SCHEMA CASCADE write DDL tokens only. Both options class them as DDL; whether they count as data changes is a policy question.
- **Option A over-reports exactly four POC snapshots:**
  - three flushes of inlined deletes (`s03_flush_del` 3, `s03_flush_both` 5, `s04_dv_flush` 3), written as `deleted_from_table:T` alone, or with `inline_flush:T` when the rows were inlined;
  - U1 (`s05_u1` 2).
- **The Definition is right on all 44 scenarios** of the option B POC, and the server-side and staged commits re-derive through `AddTableChanges`.

## POC: option B (`poc/option-b.diff`, `poc/option-b.md`)

- **What was built:** an opt-in `1.2-dev1` (`DUCKLAKE_LATEST_VERSION` stays `V1_1`) with:
  - a versioned manager, `GetSnapshotChangesTableStatement()`, `MigrateV11`, a new gate and the server-side version read;
  - the two logical sets, written with `SQLString` and named columns;
  - a `snapshots()` `data_changes MAP(VARCHAR, VARCHAR[])` column.
- **Size:** 23 files, +465/−27. Source is 17 files, +184/−23; tests are 6 files, +281/−4.
- **Suite** (`test/sql/*`, default config): the baseline passes all 944 cases. The POC fails only 3 tests, all on version error text, and those are updated in the diff.
- **With `1.2-dev1` as the default, 5 tests break:**
  - one version-string assert;
  - two migration error texts;
  - the positional inserts in `ducklake_snapshots_chunked.test` and `quack/server_side_commit_atomicity.test`.
- **Not run:**
  - the SQLite, Postgres, Quack, `no_inline`, `deletion_vectors` and `ducklake_version.json` configs;
  - clang-format 11.0.1;
  - proving that the new tests fail without the change.

## POC: Track 0 fixes (`poc/track0-u1.diff`, `poc/track0-reset-partition.diff`, `poc/track0.md`)

Each patch applies on its own to `1a4d6ae2`, and still applies on `origin/main` 12 commits later: neither file has changed. Each test fails on the base and passes with its fix. data_inlining, deletion_inlining, transaction, partitioning, table_changes and issues pass: 218 cases, 16 skipped, 0 failed.

- **U1:** +9 lines in `LocalTableChanges::DeleteFromLocalInlinedData`. When the rebuilt collection is empty, it resets `new_inlined_data` and erases the table entry if nothing else changed. A 129-line test covers 36 assertions. No path calls the function twice for one table: `FlushDelete` runs once per file (`ducklake_delete.cpp:593-602`), and local inlined data is one file per table.
- **RESET PARTITIONED BY then INSERT:**
  - Cause: RESET leaves a non-null partition spec with no fields and a local id (`ducklake_table_entry.cpp:685-688`). `GetPartitionId` returned that id, so the file carried a `partition_id` and no values, which trips `ducklake_metadata_manager.cpp:4289,4384`.
  - Fix: `if (partition_data && !partition_data->fields.empty())`, +2/−1 in `ducklake_insert.cpp`.
  - A 142-line test covers 48 assertions.
  - Neighbours that failed on the base and pass with the fix: SET+RESET+INSERT, CREATE+SET+RESET+INSERT, and RESET+INSERT+SET+INSERT.
  - #1711 (SET/RESET/SET) is already in `1a4d6ae2`, and the bug reproduces there.
- **Retry of internal errors:**
  - `RetryOnError` (`ducklake_transaction.cpp:1382-1396`) matches "unique".
  - The commit loop passes `error.Message()` (`ducklake_transaction_state.cpp:2707`), which for internal errors appends the resolved stack trace (`duckdb/src/common/error_data.cpp:74-79`). The bug's trace has four `unique_ptr` frames.
  - So an internal error is retried 10 times and reported with advice to raise `ducklake_max_retry_count`.
  - Likely fix: `error.RawMessage()`. This is not instrumented, and there is no patch yet: the test needs another way to trigger an internal error at commit.
- **Duplicates:** none upstream or in the forks for any of the three.

## Not verified

- The Postgres, SQLite and Quack backends for both POCs and both patches.
- clang-format 11.0.1, since only 22.1.8 is installed.
- Concurrent commits with retries.
- The real frequency of flushes of inlined deletes.
- When 1.1 will be published: there are 0 tags and the calendar says 2026-10-21, tentatively.
