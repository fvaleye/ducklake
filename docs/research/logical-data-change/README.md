# Logical data changes: plan and research

A plan to record, per commit, which tables' data a snapshot logically inserted into or deleted from, as distinct from maintenance such as flushes and compaction. It proposes a nullable `data_changes` column on `ducklake_snapshot_changes`, exposed by `snapshots()`.

| File | Contents |
| --- | --- |
| [`plan.md`](plan.md) | Implementation plan, v6 (2026-10-14) |
| [`marker-prototype.md`](marker-prototype.md) | Prototype of the marker on `900cf2f9`, with the signals it records and the plan changes it forced |
| [`challenge-v5.md`](challenge-v5.md) | Adversarial review of plan v5: a token in `changes_made` against a new column |
| [`challenge-v3.md`](challenge-v3.md) | Adversarial review of plan v3, with executed probes |
| [`inlined-net-zero.md`](inlined-net-zero.md) | Investigation of an inlined insert deleted in the same transaction, which creates no snapshot (task U1) |

Each report is the final summary of its investigation. The probe scripts, prototype diff and raw outputs they cite under `scratchpad/` were not kept, so those paths no longer resolve.
