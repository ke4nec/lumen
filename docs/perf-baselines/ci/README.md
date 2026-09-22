# CI performance baselines

These reports are the fixed 1920x1080, 30 warmup / 300 measured-frame gates
used by `.github/workflows/linux.yml`. Each checked-in metric is the maximum
of three canonical baseline runs, while CI compares the median of three current
runs. This absorbs scheduler outliers without changing the 10% regression rule.
Each backend has its own files because
CPU, Skia raster, and Skia Ganesh GPU timings are not interchangeable.

The five required workloads are archived for each backend:

- `card-grid-6x8-1080p`
- `text-heavy-1080p`
- `grid-1080p`
- `virtual-list-1000-1080p`
- `semantics-diff-1080p`

`check_perf_regression.py` compares phase p50/p95, p50 allocation count and
bytes, and `commands_per_frame`. `submit` and `gpu_wait` are present in every
report; CPU and Skia raster intentionally record zero GPU wait. A report is
only comparable to the same backend, scenario, and viewport. GPU reports are
the Linux Mesa/llvmpipe archive; hardware-GPU acceptance remains the separate
self-hosted platform workflow.

Regenerate a backend set with:

```sh
for scenario in card-grid-6x8-1080p text-heavy grid virtual-list-1000 semantics-diff; do
  ./build/benchmarks/lumen-scene-bench --backend cpu --frames 300 --warmup 30 \
    --scenario "$scenario" --json > "cpu-$scenario.json"
done
```
