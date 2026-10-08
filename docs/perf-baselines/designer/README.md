# Designer performance gate

DP-8 (`lumen-designer-prerequisites.md` §4.18) uses four workloads:
`l0_12`, `edit_100`, `outline_1000`, and `virtual_list_1000`. The reviewed source
reference is `a1e012eedf4cd69bb0a25f32cf0c530e761898e0`, pinned in `reference.json`
and `LUMEN_DESIGNER_PERF_BASELINE_COMMIT` in `.github/workflows/linux.yml`.
Linux CPU CI builds both revisions with the same toolchain and configuration.

`run_designer_perf_gate.py` alternates five runs per version on one allowed
logical CPU. Each run warms up ten times and measures three hundred iterations.
Both binaries must report Release. Source commit, tracked dirty status, binary
SHA256, collector/dependency digests, CMake compiler/build flags, hardware,
affinity and a shared measurement-session ID accompany every raw report.
The build cache must refer to the supplied source checkout. The reference must
be clean. Local diagnostics may use `--allow-dirty-current`; CI never does.

The 10% relative limit covers every phase's p50/p95 time, allocation count and
allocated bytes; scoped heap allocations/bytes/live/peak; preview rebuilds;
render nodes and virtual materialization. Timing also requires an absolute
shift greater than 50 microseconds, matching the existing scene gate's noise
floor. Minimum and median timing aggregations are both compared; the gate
fails when both comparisons fail. Heap and allocations use the median under
both. The limit does not establish an absolute cross-machine budget.

All four fixtures and their phase sets are mandatory. Document node counts
are fixed by the workload; fixed-tree render counts are also checked.
Frame hashes and workload shape must remain identical across repeats of the
same binary. Cross-version hashes may differ, permitting reviewed visual
changes. Missing or non-finite metrics, incomplete provenance, different
sampling/environments, and dirty references fail rather than silently pass.
Zero-to-positive allocation/live-heap regressions fail with a null relative
delta; no JSON Infinity is emitted.

CI archives ten raw reports and `gate.json` under `bench-results/designer/`.
One retry goes to `bench-results/designer-retry/`; both attempts are archived
and the thresholds and reference remain unchanged. A failed self-comparison
on the clean reference indicates unstable measurement conditions; it does
not make a candidate failure pass. Preserve failed reports and use an idle
runner to diagnose persistent timing differences.

The initial five-by-twenty sampling protocol failed a clean, bit-identical
binary control on the local QEMU runner (p95 shifts over 20%). Increasing the
sample count improves the tail estimate; the 10% limit and 50us floor remain
unchanged. A failed control is retained as evidence, never treated as a pass.

Local validation on 2026-10-08 (Linux/QEMU, GNU 15.2.0, Release) passed with
the canonical five-by-three-hundred protocol: the clean reference binary
control passed under both estimators; the reference/candidate pair passed the
combined gate under minimum. Its median comparison failed on `l0_12` paint
p50 (389.769us to 489.849us, +25.68%); that result remains in the verdict.
The candidate was measured with `--allow-dirty-current` and is labelled dirty;
there were no framework/collector source changes relative to `a1e012e`.
This is local diagnostic evidence, not a completed GitHub CI or desktop run.
Raw reports and verdicts remain in the local output directories:

| Attempt | Result | Local directory |
| --- | --- | --- |
| Initial 20-iteration pair | FAIL | `/tmp/lumen-designer-perf-gate-2026-10-08/` |
| Initial pair retry | FAIL | `/tmp/lumen-designer-perf-gate-2026-10-08-retry/` |
| 20-iteration identical-binary control | FAIL | `/tmp/lumen-designer-perf-gate-2026-10-08-control/` |
| 300-iteration identical-binary control | PASS (both estimators) | `/tmp/lumen-designer-perf-gate-2026-10-08-control-300/` |
| 300-iteration pair | PASS (minimum; median has one timing failure) | `/tmp/lumen-designer-perf-gate-2026-10-08-300/` |

A subsequent clean-checkout comparison on the same local runner passed for
`580ea50f84e7b8e20affdfddaa3b266b1b3bfffe` against the pinned reference.
Both revisions were clean, using the canonical five-by-three-hundred protocol.
Minimum passed; median failed only on `l0_12` paint p95 (753.062us to
845.033us, +12.21%). The existing combined rule passes this pair; the failure
is retained without changing the reference, threshold or noise floor.
Provenance and the exact verdict are summarized in
[`designer-perf-linux-2026-10-08.json`](../../platform-evidence/designer-perf-linux-2026-10-08.json);
the ten raw reports and `gate.json` remain at
`/tmp/lumen-designer-perf-gate-580ea50-clean/`. This is local evidence, with
the accessibility bridge OFF in both builds, and does not replace hosted CI
or any native desktop acceptance record.

Build with `-DCMAKE_BUILD_TYPE=Release -DLUMEN_BUILD_BENCHMARKS=ON`, then run:

```sh
python3 -B benchmarks/run_designer_perf_gate.py \
  --baseline-source .designer-perf-reference \
  --baseline-bin designer-perf-reference-build/benchmarks/lumen-designer-bench \
  --current-bin build/benchmarks/lumen-designer-bench \
  --output bench-results/designer
```

Changing `benchmarks/designer_bench.cpp` or `cmake/dependencies.cmake` changes
the workload identity. Re-anchor both pins together to a reviewed, tested
commit; do not refresh a baseline to hide a genuine regression. The existing
scene benchmark has its own independent reference.

The collector measures scoped C++ `new/delete` in headless CPU preview
operations, including L2 VirtualList composition. It excludes desktop
presentation, font/window backends, malloc-only allocations and the production
`FrameAllocationSource`. Passing this gate does not satisfy real desktop or
production allocator acceptance. `designer_perf_gate_integrity` tests the
comparator's failure boundaries, provenance, CLI exits, and runner ordering;
`designer_benchmark_report_integrity` tests the actual C++ collector.
