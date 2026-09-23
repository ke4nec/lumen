# CI performance baselines

The checked-in timing JSON files are **historical samples with incomplete
provenance** (`commit: working-tree` and no machine identity). They are retained
for inspection and are no longer accepted by the CI gate. Do not relabel them
with a guessed commit or hardware description.

The reviewed reference is pinned in `reference.json` and
`LUMEN_PERF_BASELINE_COMMIT` in `.github/workflows/linux.yml`. CI checks out and
builds that revision with the same toolchain as the candidate, then
`run_perf_gate.py` alternates five reference/candidate measurements for each
backend and scene, with 30 warmup and 300 measured frames and the unchanged 10%
limit. Timing metrics are judged under two aggregations of the same repeats —
the interference-robust minimum (host noise only ever adds CPU time) and the
mode-robust median (a short phase's tail percentile has wide run-to-run
sampling spread) — and a regression fails only when it exceeds the limit under
both. Allocation counts always use the median: they are discrete,
path-dependent values whose runs settle in one of a few modes, so a minimum
would latch onto whichever side happened to catch the low mode. Raw
measurements and both per-aggregation comparisons are archived in
`bench-results/<backend>/`; a reference refresh is an explicit reviewed commit.
Linux CPU/raster measurements use the same allowed logical CPU for both versions;
the affinity is recorded. GPU keeps its allowed CPU set for driver workers.

The five required workloads are archived for each backend:

- `card-grid-6x8-1080p`
- `text-heavy-1080p`
- `grid-1080p`
- `virtual-list-1000-1080p`
- `semantics-diff-1080p`

`check_perf_regression.py` compares phase p50/p95, p50 allocation count and
bytes, and `commands_per_frame`. `submit` and `gpu_wait` are present in every
report; CPU and Skia raster intentionally record zero GPU wait. A report is
only comparable with matching backend, scene source hash, dependency revision,
viewport, compiler flags, compiler version, Release configuration, platform,
runner hardware, sampling, alpha mode and measurement scope. The wrapper records
exact source commits, dirty status, binary SHA256 and a shared measurement-session
ID. Repeated runs must have identical frame hashes within each version. Missing
phases/metrics and incomparable environments fail instead of silently passing.
Zero-to-positive regressions fail and serialize as a null relative delta, not
nonstandard JSON Infinity.

GPU CI still uses Mesa/llvmpipe and these measurements exclude desktop present.
They do not establish hardware GPU or compositor performance; that requires the
separate self-hosted platform acceptance and measured hardware reports.

Run a comparison after building both clean source revisions:

```sh
python3 -B benchmarks/run_perf_gate.py --backend cpu \
  --baseline-source .perf-reference \
  --baseline-bin perf-reference-build/benchmarks/lumen-scene-bench \
  --current-bin build/benchmarks/lumen-scene-bench --output bench-results/cpu
```

Local diagnostic comparisons may opt into `--allow-dirty-current`; the report
records that fact. CI never uses it. `perf_gate_tests.py` tests actual CLI exit
codes at and above 10%, zero baselines, missing metrics and mismatched provenance.

If a shared runner reports a regression, a diagnostic control may use the same
clean reference checkout and binary for both sides (`--current-source` and
`--current-bin`). A failed self-comparison indicates an unstable measurement
environment; it does not turn the candidate result into a pass. Retain the failed
reports and repeat validation on an idle, dedicated runner. Do not increase the
10% limit or refresh the reference merely to suppress scheduling noise.
