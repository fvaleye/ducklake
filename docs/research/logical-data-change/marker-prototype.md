## Verdict

The design works for what it sets out to do. I applied M1 and M2 to upstream `900cf2f9`, gated on `context.supports_v1_1_metadata` (U1 not applied). On the probe sequence the markers match the plan's expected snapshots exactly. No conflict check reads the marker. The only existing tests that break do so because of the extra token. But one plan decision is wrong: D3 has to be a version bump, and keeping `1.1-dev1` is not workable. Two test-plan items also need fixing.

Full report: `scratchpad/marker/REPORT.md`. It has the diff, outputs and commands. Everything below lives in that `marker/` folder.

## Results

- **M1 parse test:** fails on the baseline build with `Unsupported change type data_changed_table` and passes on the prototype (3 assertions). It also passes under SQLite, Postgres and `ducklake_version.json`.
- **M2 Definition test:** fails on the baseline (`Expected 3 rows, but got 0 rows` at line 163) and passes on the prototype (89 assertions) on default, Postgres, `deletion_vectors.json` and `no_inline.json`.
  - Probe replay gives t [3,4,5,14,15,22], ti [18,19,20], tf [24,25,27]. Snapshots 16, 17, 21, 26 and 28 have no marker. Snapshot 28 still exists because U1 was not applied, and it carries only `inlined_insert`.
  - CTAS remapped by a concurrent CREATE: the first attempt got id 1 and the final id is 2. The marker says 2.
  - On a 1.0 catalog, DML works and no marker is written.
- **3a, conflict isolation:** `tables_data_changed` appears only in `snapshots.cpp:133`, the two header sets, the parse case (`transaction_changes.cpp:202`), `AddTableChanges` (`transaction.cpp:1110`) and `WriteSnapshotChanges` (`transaction_state.cpp:395,425`). None of these is inside `CheckForConflicts` (229-361).
  - `transaction/*` gives the same result on both builds: default config 21/21 pass, SQLite 212 assertions pass, Postgres 519 pass.
- **3b, compatibility:** the prototype wrote markers into a 1.1-dev1 catalog on Postgres, then I used it with the unpatched helper binary:
  - ATTACH, reads, `table_changes`, cleanup and merge all work.
  - `snapshots()` and `expire_snapshots` over a marker snapshot fail with `Unsupported change type`. Expire renders rows through `GetSnapshotValues` (`expire_snapshots.cpp:77`).
  - A transaction open while the prototype commits to a *different* table fails at COMMIT: `Failed to commit DuckLake transaction. Unsupported change type data_changed_table`. In a control run with both processes unpatched, the same commit succeeds.
  - The old binary's autocommit INSERT succeeds but **writes a data snapshot with no marker** (`{inlined_insert=[1]}`).
  - With the version set to `1.1-dev2`, the old binary refuses at ATTACH: `Unsupported ducklake_version '1.1-dev2'`.
- **3c, completeness:** these operations get correctly marked: MERGE, DELETE with a subquery, TRUNCATE, whole-file DELETE (0 delete files, marker from the state set), `add_data_files`, inlined UPDATE, flush plus delete in either order, partition change then insert, rename then insert.
  - No marker, as intended: CTAS with 0 rows, net-zero inserts, flushes (including inlined file deletes), compaction, and DDL such as `ADD COLUMN DEFAULT` and `SET DATA TYPE`.
  - DROP TABLE on its own gets no marker. DML followed by DROP in one transaction marks the dropped id, just as main already writes `inlined_insert` for it. I'd keep that: `tables_dropped` already records the drop and table ids are never reused.
  - Separately, main has a bug with no connection to the marker: RESET PARTITIONED BY plus INSERT in one transaction throws `INTERNAL Error: File should either not be partitioned, or have partition values`.
- **3d, existing tests:** 7 of 820 fail on the prototype and none on the baseline. A script confirmed every failure is the token only, then updated them:
  - `compaction/merge_files_expired_snapshots`
  - `data_inlining/data_inlining_flush`
  - `deletion_inlining/test_deletion_inlining_compaction`
  - `issues/issue_1573_rewritten_delete_snapshot`
  - `rewrite_data_files/test_last_snapshot_rewrite`
  - `sorted_table/data_inlining_flush_sorted_basic`
  - `sorted_table/data_inlining_flush_sorted_basic_expression`

  The plan's grep finds only 4 of these. It misses the 3 whose expected output contains only `inlined_insert=`.
- **3e, size:** source changes are 6 files, +36 −7. With tests it is 15 files, +374 −28. clang-format 11.0.1 and `git diff --check` are clean.

## Required plan changes, ranked

1. **D3: bump to `1.1-dev2`.** On 1.1-dev1 an older binary fails in the middle of a commit, which breaks Review Focus 1. Worse, it keeps committing data changes without markers, so the D2 cutoff ("no marker means maintenance") becomes false. The gate becomes `version >= V1_1_DEV_2`, and M3 handles the dev1 to dev2 migration.
2. **Updated tests must pin the catalog version.** All 7 updated tests and the new M2 test fail under `ducklake_version.json`, which defaults to 1.0 and runs in CI (`DuckLakeVersion.yml:70`). The original versions pass there. Pinning `DUCKLAKE_VERSION` in the ATTACH fixes it; I verified that on the M2 test.
3. **The server-side commit case can't use `ducklake_commit_prepared.test`.** That test only checks two error paths. A successful server-side commit needs the Quack backend, so the case needs a Quack test, or should be dropped and rely on the shared `WriteSnapshotChanges`.
4. **Move the concurrency cases into their own test file.** Under SQLite, one `database is locked` skips the whole M2 file. Without those cases, 76 assertions pass on SQLite.
5. **M1 needs the new set in both change structs**, and M2 needs one new `WriteSnapshotChanges` parameter in place of two call-site flags.
6. **Definition text:** say that the whole-file DELETE signal is added in `WriteSnapshotChanges`, not `AddTableChanges`. Also document the DDL and DML-then-DROP behaviour.

## Not verified

- Quack and server-side commit were not run (no Quack build). I only read the code: it reaches the same `WriteSnapshotChanges` (`server_side_commit.cpp:113-127`), and the signals it needs are rebuilt at :83, :340 and :419.
- M3 and M4.
- The full suite under SQLite and Postgres; I ran only `transaction/*` and the 9 touched tests there.
- The ducklake-web spec page.

## Artifacts and cleanup

- Diffs: `marker.diff` (everything) and `marker_src.diff` (source only).
- New tests are in `tests/`; binaries are in `proto_bin/` and `base_bin/`, with hashes in `SHA256SUMS`. Prototype `duckdb` is `ae2a2cad…`, baseline `duckdb` is `8649d615…`.
- The `unittest` binaries in those folders still point at the deleted worktree for their tests. Rerunning tests needs a fresh checkout of `900cf2f9` with `marker.diff` applied.
- The worktree is removed. I created two Postgres databases on the local server (`marker_compat`, `marker_proto`) and dropped them at the end. I did not touch the user's repo or the helper worktree, apart from running the helper's binary.
