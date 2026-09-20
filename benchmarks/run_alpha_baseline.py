"""Alpha plan P0/P4. Run Release artifacts sequentially, optionally paired.

Standard-library-only driver. It never rebuilds binaries: preserve the P0 build
and pass --peer-build at P4 for three interleaved before/after groups. The manifest
identifies the source and binary hashes so stale/mixed artifacts are visible.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess


def executable(build, name, folder="benchmarks"):
    base = build / folder
    suffix = ".exe" if os.name == "nt" else ""
    for directory in (base / "Release", base):
        path = directory / (name + suffix)
        if path.exists():
            return path.resolve()
    raise FileNotFoundError(base / name)


def run(builds, output, backend, windows, groups):
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    # Never silently turn real-window measurements into dummy-driver reports.
    if windows and env.get("SDL_VIDEODRIVER") == "dummy":
        raise ValueError("real-window measurements cannot use SDL_VIDEODRIVER=dummy")
    for label, build, revision in builds:
        manifest = {
            "revision": revision, "platform": platform.platform(),
            "machine": platform.machine(), "processor": platform.processor(),
            "backend": backend, "build_directory": str(build.resolve()),
            "current_worktree_diff": subprocess.check_output(
                ["git", "diff", "--stat"], text=True),
            "current_worktree_status": subprocess.check_output(
                ["git", "status", "--short"], text=True),
            "executables": {},
        }
        for name in ("lumen-scene-bench", "lumen-alpha-bench"):
            path = executable(build, name)
            manifest["executables"][name] = {
                "path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
        (output / f"{label}-manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        (output / f"{label}-CMakeCache.txt").write_bytes((build / "CMakeCache.txt").read_bytes())

    cases = [("canonical-" + s, "lumen-scene-bench", ["--scenario", s, "--json"])
             for s in ("card-grid-6x8-1080p", "text-heavy", "resource-upload")]
    cases += [("alpha-" + s, "lumen-alpha-bench", ["--scenario", s])
              for s in ("layers", "edges", "images", "upload")]
    if windows:
        cases += [(f"present-{height}-{path}-{kind}", "lumen-alpha-bench",
                   ["--scenario", "probe", "--height", str(height), "--present", path]
                   + (["--opaque"] if kind == "opaque" else []))
                  for height in (1080, 2160) for path in ("software", "texture")
                  for kind in ("transparent", "opaque")]
    for group in range(1, groups + 1):
        # Alternate which build goes first while keeping each pair adjacent.
        ordered = builds if group % 2 else list(reversed(builds))
        for case, name, arguments in cases:
            for label, build, revision in ordered:
                stem = output / f"{label}-{case}-{group}"
                args = [str(executable(build, name)), "--backend", backend,
                        "--warmup", "30", "--frames", "300", *arguments]
                if group == 1 and not case.startswith("present-"):
                    args += ["--dump-frame", str(stem.with_suffix(".rgba"))]
                env["LUMEN_BENCH_COMMIT"] = revision
                print(f"{label} group {group}: {case}", flush=True)
                result = subprocess.run(args, env=env, capture_output=True, text=True, timeout=600)
                stem.with_suffix(".stderr.txt").write_text(result.stderr, encoding="utf-8")
                if result.returncode:
                    raise RuntimeError(f"{case}: {result.returncode}\n{result.stderr}")
                report = json.loads(result.stdout)
                if report["build_type"] != "Release":
                    raise ValueError("performance baselines require Release")
                report.update(revision=revision, group=group, command=args)
                stem.with_suffix(".json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    for label, _, _ in builds:
        for case, _, _ in cases:
            hashes = {json.loads((output / f"{label}-{case}-{group}.json").read_text())["frame_hash"]
                      for group in range(1, groups + 1)}
            if len(hashes) != 1:
                raise ValueError(f"non-repeatable frame: {label}/{case}: {hashes}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--backend", choices=("cpu", "skia"), default="cpu")
    parser.add_argument("--windows", action="store_true")
    parser.add_argument("--groups", type=int, default=3)
    parser.add_argument("--peer-build", type=Path)
    parser.add_argument("--peer-revision")
    options = parser.parse_args()
    if options.groups < 3:
        parser.error("at least three independent groups are required")
    builds = [("before", options.build, options.revision)]
    if options.peer_build:
        if not options.peer_revision:
            parser.error("--peer-build requires --peer-revision")
        builds.append(("after", options.peer_build, options.peer_revision))
    run(builds, options.output, options.backend, options.windows, options.groups)
