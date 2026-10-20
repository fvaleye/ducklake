# Logical data changes: plan and research

A plan to let readers tell, per snapshot and per table, logical inserts and deletes from maintenance such as flushes and compaction. It offers two options: a documented classification of today's `changes_made` tokens (no schema change), or a nullable `data_changes` column on `ducklake_snapshot_changes` in the next catalog version, exposed by `snapshots()`.

| File | Contents |
| --- | --- |
| [`plan.md`](plan.md) | Implementation plan, v10 (2026-10-20): option B chosen, gated on an upstream Discussion |
| [`challenge-v7.md`](challenge-v7.md) | Adversarial review of plan v7, with the option B POC and the Track 0 fix POCs |
| [`challenge-v6.md`](challenge-v6.md) | Adversarial review of plan v6 against main after the 1.1 release, with executed probes |
| [`marker-prototype.md`](marker-prototype.md) | Prototype of the marker on `900cf2f9`, with the signals it records and the plan changes it forced |
| [`challenge-v5.md`](challenge-v5.md) | Adversarial review of plan v5: a token in `changes_made` against a new column |
| [`challenge-v3.md`](challenge-v3.md) | Adversarial review of plan v3, with executed probes |
| [`inlined-net-zero.md`](inlined-net-zero.md) | Investigation of an inlined insert deleted in the same transaction, which creates no snapshot (task U1) |

Each report is the final summary of its investigation. The probe scripts, prototype diff and raw outputs cited by v6 and earlier under `scratchpad/` were not kept, so those paths no longer resolve. For v7 to v10, the reports, probe SQL, runner scripts, POC patches and the v10 oracle workload are kept locally (outside git) in the repository's `.claude/logical-data-change/`.
