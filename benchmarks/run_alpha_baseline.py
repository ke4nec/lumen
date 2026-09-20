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
import threading


def executable(build, name, folder="benchmarks"):
    base = build / folder
    suffix = ".exe" if os.name == "nt" else ""
    for directory in (base / "Release", base):
        path = directory / (name + suffix)
        if path.exists():
            return path.resolve()
    raise FileNotFoundError(base / name)


def valid_drawable(report):
    if report.get("presentation", "none") == "none":
        return True
    expected = report["physical_pixels"]
    monitor = report.get("window_monitor", {})
    if monitor and (monitor.get("error") or not monitor.get("samples") or not monitor.get("states")):
        return False
    return (report["window_drawable_pixels"] == expected and
            all(state[:2] == expected and not state[2] for state in monitor.get("states", [])))


def execute(args, env, watch_window):
    # Sampling is outside the measured process and identical for both binaries.
    # It catches native minimize/resize even if the window is restored by exit.
    monitor = {"poll_interval_ms": 200, "samples": 0, "states": []}
    stopped = threading.Event()
    with subprocess.Popen(args, env=env, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=True) as process:
        def watch_impl():
            import ctypes
            from ctypes import wintypes
            api = ctypes.WinDLL('user32', use_last_error=True)
            api.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
            api.FindWindowW.restype = wintypes.HWND
            api.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
            api.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
            api.IsIconic.argtypes = [wintypes.HWND]
            # Keep observer coordinates in device pixels on high-DPI desktops.
            if hasattr(api, 'SetThreadDpiAwarenessContext'):
                api.SetThreadDpiAwarenessContext.argtypes = [ctypes.c_void_p]
                api.SetThreadDpiAwarenessContext.restype = ctypes.c_void_p
                api.SetThreadDpiAwarenessContext(ctypes.c_void_p(-4))
            while not stopped.is_set():
                hwnd = api.FindWindowW(None, 'Lumen alpha baseline')
                owner = wintypes.DWORD()
                if hwnd:
                    api.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
                    rect = wintypes.RECT()
                    if owner.value == process.pid and api.GetClientRect(hwnd, ctypes.byref(rect)):
                        state = [rect.right-rect.left, rect.bottom-rect.top, bool(api.IsIconic(hwnd))]
                        monitor['samples'] += 1
                        if state not in monitor['states']:
                            monitor['states'].append(state)
                stopped.wait(0.2)
        def watch():
            try:
                watch_impl()
            except Exception as error:
                monitor['error'] = str(error)
        watcher = threading.Thread(target=watch, daemon=True) if watch_window and os.name == 'nt' else None
        if watcher:
            watcher.start()
        try:
            stdout, stderr = process.communicate(timeout=600)
        except BaseException:
            process.kill()
            process.communicate()
            raise
        finally:
            stopped.set()
            if watcher:
                watcher.join()
        if monitor.get('error'):
            raise RuntimeError('native window observer failed: ' + monitor['error'])
        if watcher and process.returncode == 0 and monitor['samples'] == 0:
            raise RuntimeError('native window observer saw no samples')
        return subprocess.CompletedProcess(args, process.returncode, stdout, stderr), monitor if watcher else None


def run(builds, output, backend, windows, groups, resume=False):
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
        manifest_path = output / f"{label}-manifest.json"
        cache_path = output / f"{label}-CMakeCache.txt"
        if resume and manifest_path.exists():
            saved = json.loads(manifest_path.read_text(encoding="utf-8"))
            for field in ("revision", "platform", "machine", "processor", "backend", "executables"):
                if saved[field] != manifest[field]:
                    raise ValueError(f"cannot resume changed {label} {field}")
            if cache_path.read_bytes() != (build / "CMakeCache.txt").read_bytes():
                raise ValueError(f"cannot resume changed {label} CMakeCache")
        else:
            manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
            cache_path.write_bytes((build / "CMakeCache.txt").read_bytes())

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
            pending = output / f"{case}-{group}.pending"
            # Resume only complete adjacent pairs. A half-finished pair is
            # rerun in full so a suspend/interruption cannot separate its runs.
            if resume and not pending.exists() and all((output / f"{label}-{case}-{group}.json").exists()
                              for label, _, _ in ordered):
                for label, build, revision in ordered:
                    saved = json.loads((output / f"{label}-{case}-{group}.json").read_text(encoding="utf-8"))
                    expected = [str(executable(build, name)), "--backend", backend,
                                "--warmup", "30", "--frames", "300", *arguments]
                    if group == 1 and not case.startswith("present-"):
                        expected += ["--dump-frame", str(output / f"{label}-{case}-{group}.rgba")]
                    if (saved["revision"] != revision or saved["backend"] != backend or
                            saved["group"] != group or saved["build_type"] != "Release" or
                            saved["warmup_frames"] != 30 or saved["measured_frames"] != 300 or
                            saved["command"] != expected or not valid_drawable(saved)):
                        raise ValueError(f"cannot resume changed report {label}/{case}/{group}")
                print(f"kept complete group {group}: {case}", flush=True)
                continue
            # Mark the pair before replacing either report: if a retry fails,
            # an older peer report must not make the next resume look complete.
            pending.write_text("pair in progress\n", encoding="utf-8")
            for label, build, revision in ordered:
                stem = output / f"{label}-{case}-{group}"
                args = [str(executable(build, name)), "--backend", backend,
                        "--warmup", "30", "--frames", "300", *arguments]
                if group == 1 and not case.startswith("present-"):
                    args += ["--dump-frame", str(stem.with_suffix(".rgba"))]
                env["LUMEN_BENCH_COMMIT"] = revision
                print(f"{label} group {group}: {case}", flush=True)
                result, monitor = execute(args, env, case.startswith("present-"))
                stem.with_suffix(".stderr.txt").write_text(result.stderr, encoding="utf-8")
                if result.returncode:
                    raise RuntimeError(f"{case}: {result.returncode}\n{result.stderr}")
                report = json.loads(result.stdout)
                if report["build_type"] != "Release":
                    raise ValueError("performance baselines require Release")
                report.update(revision=revision, group=group, command=args)
                if monitor is not None:
                    report["window_monitor"] = monitor
                if not valid_drawable(report):
                    stem.with_suffix(".rejected.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
                    raise ValueError(f"{case}: drawable changed/minimized; reject and rerun the whole pair")
                stem.with_suffix(".json").write_text(json.dumps(report, indent=2), encoding="utf-8")
            pending.unlink()
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
    parser.add_argument("--resume", action="store_true", help="verify artifacts and retain complete pairs")
    options = parser.parse_args()
    if options.groups < 3:
        parser.error("at least three independent groups are required")
    builds = [("before", options.build, options.revision)]
    if options.peer_build:
        if not options.peer_revision:
            parser.error("--peer-build requires --peer-revision")
        builds.append(("after", options.peer_build, options.peer_revision))
    run(builds, options.output, options.backend, options.windows, options.groups, options.resume)
