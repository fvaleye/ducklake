The inconsistency is real, and an 8-line change in `DeleteFromLocalInlinedData` fixes it. I built a patched scratch copy (`explore_netzero/src_copy`) and confirmed that. Source paths below are relative to the repository root. Probes are in `scratchpad/explore_netzero/` (`run.sh`, `case_*`). Nothing in any repo was edited.

**1. Reproduction on main (table `t` with one committed row, snapshots 0 to 2)**
- **File-backed (limit 0):** no snapshot 3 in all three cases (insert/delete, insert 2/delete twice, insert/UPDATE/DELETE).
- **Inlined (limit 100):** all three cases create snapshot 3 = `{inlined_insert=[1]}`. `ducklake_inlined_data_1_1` still holds only row 0 (a=1, begin 2). `next_row_id` stays 1 but `next_file_id` goes from 1 to 2 (`ducklake_snapshot`).
- **Inlined, also deleting committed row 1:** snapshot 3 = `{inlined_insert=[1], inlined_delete=[1]}`, and row 0 gets end_snapshot 3.
- **Extra case F** (file-backed table, then `set_option('data_inlining_row_limit',100)`, then the net-zero transaction): snapshot 3 bumps `schema_version` from 1 to 2 and creates an empty `ducklake_inlined_data_1_2`.
- **Control:** a zero-row `INSERT ... WHERE false` creates no snapshot in either mode. So `Count()==0` only happens after `DeleteFromLocalInlinedData`.

**2. How the commit decides there are changes**
- `ChangesMade()` = schema changes, or `local_changes.HasChanges()`, or dropped files, or name maps (`ducklake_transaction.cpp:905-908`). Commit flushes only if that is true (`:800-807`).
- `HasChanges()` is just `!changes.empty()` (`:96-99`).
- `LocalTableDataChanges::IsEmpty()` tests the `new_inlined_data` pointer, not `Count()` (`:44-46`).
- `TryDropTransactionLocalFile` erases the entry once it is empty (`:204-207`). `DeleteFromLocalInlinedData` swaps in an empty collection and keeps the pointer (`:349-351`).
- So an inlined collection with `Count()==0` counts as non-empty. Downstream, `AddTableChanges` adds `inlined_insert` on the pointer alone (`:1089-1091`), and `GetNewDataFiles` processes it (`ducklake_transaction_state.cpp:1445`, `:1482`), forcing `next_file_id++` (`:1521`). `WriteNewInlinedData` creates the inlined table and bumps the schema version when none exists (`ducklake_metadata_manager.cpp:3404`, `:3441`).

**3. The fix (the `src_copy` diff)**

After the collection is rebuilt in `DeleteFromLocalInlinedData` (`ducklake_transaction.cpp:~347`):
```cpp
if (new_data->Count() == 0) {
	table_changes->new_inlined_data.reset();
	if (table_changes->IsEmpty()) {
		changes.erase(table.GetTableId());
	}
	return;
}
```
On the patched build: inlined insert/delete, two DELETEs and UPDATE-then-DELETE each create no snapshot. Case E records only `{inlined_delete=[1]}`. Case F creates no snapshot and no inlined table.

Side effects:
- **Conflicts:** the phantom `inlined_insert` currently makes concurrent deletes or SET NOT NULL by other transactions conflict as "inserted into it" (`ducklake_transaction_state.cpp:299, 304, 322, 342`). This last point comes from reading the code; I did not run a concurrent test. In case E, the drop/alter checks still apply through `tables_deleted_inlined` (`:314-323`). `tables_delete_attempted` is still set (`ducklake_delete.cpp:589`), the same as file-backed today.
- **Row ids:** no change. A count of 0 already added 0 to `next_row_id` (`:1500-1509`).
- **Inlined-table creation:** tables come from CREATE/ALTER (`:1772-1778`, `:1815-1836`) or are created lazily on the next real insert (`ducklake_metadata_manager.cpp:3404`). A stale `RequiresNewInlinedTable` flag (`ducklake_inline_data.cpp:418-422`) is only a routing hint and is reset at commit and rollback (`ducklake_transaction.cpp:817, 833`).
- **Server-side commit:** the staged emit skips a null `new_inlined_data` (`ducklake_staged_commit.cpp:301`), so the server never rebuilds an `external_row_count=0` entry (`ducklake_server_side_commit.cpp:332-348`). This is unverified at runtime because Quack was not built.
- **Callers:** `HasTransactionInlinedData` and `HasTransactionLocalInserts` now return false, which is correct with no rows left. The "delete all, then insert" case in `data_inlining_delete_local_stats.test` still passes.

**4. Prior reports**

None of the four requested queries (or broader ones) found an existing report of this bug. The related hits:
- **#1666 and #1714** (merged): both changed `DeleteFromLocalInlinedData` (delete by position, stale stats), but neither addresses the snapshot.
- **#1237:** empty inlined tables left after a flush, a different issue.
- Unrelated `inlined_insert` matches: #1186, #924, #1175.

**5. Test and diff size**

`explore_netzero/data_inlining_net_zero_insert.test` (119 lines): limit 10, `CREATE TABLE t` plus `INSERT (1)`, then the three net-zero transactions. It asserts that `ducklake_snapshots WHERE snapshot_id >= 2` returns only `2 {inlined_insert=[1]}`. Then the case E transaction must give `3 {inlined_delete=[1]}` and `count(*)=0`. Finally insert, `DELETE FROM t`, insert 7 must leave 7.
- On main it fails at line 69, returning 4 rows (snapshots 2 to 5, each `inlined_insert`). On the patched build all 34 assertions pass.
- Regression run on the patched build, all passing: data_inlining, deletion_inlining, update, delete, transaction, merge, concurrent, issues, table_changes, snapshot_info, functions, rowid, stats, compaction.
- Diff: 8 source lines plus about 119 test lines.

Not checked: the SQLite, Postgres and Quack catalogs, a concurrent-conflict probe, and the full test suite. One risk: if a single statement ever calls `DeleteFromLocalInlinedData` twice for a table, the second call after a reset would now hit an InternalException or a null dereference. I found no such path, but did not prove it can't happen.
