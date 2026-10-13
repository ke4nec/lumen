"""Fail fast when pinned performance baselines drift from the candidate source.

The perf gates anchor to reviewed commits (docs/perf-baselines/*/reference.json
plus the LUMEN_*_BASELINE_COMMIT pins in .github/workflows/linux.yml) and
compare source digests between the pinned reference and the candidate. Editing
an anchor source without re-anchoring fails every later gate on ``identity``
after a full build (see docs/perf-baselines/ci/README.md, re-anchoring rule).
This preflight repeats the same digest comparison against git history so the
mistake surfaces in seconds and names the exact files to update.
"""

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

# 每个门禁的钉住位置：manifest、linux.yml 环境变量名与锚定源码清单。
ANCHORS = (
    ("scene", "docs/perf-baselines/ci/reference.json", "LUMEN_PERF_BASELINE_COMMIT",
     ("benchmarks/scene_bench.cpp", "cmake/dependencies.cmake")),
    ("designer", "docs/perf-baselines/designer/reference.json", "LUMEN_DESIGNER_PERF_BASELINE_COMMIT",
     ("benchmarks/designer_bench.cpp", "cmake/dependencies.cmake")),
)


def fail(message):
    print(f"::error::{message}", flush=True)
    raise SystemExit(message)


def blob_digest(repo, commit, path):
    def read_blob():
        return subprocess.run(["git", "-C", str(repo), "cat-file", "blob", f"{commit}:{path}"],
                              capture_output=True)

    proc = read_blob()
    if proc.returncode != 0:
        # fetch-depth: 0 只含当前分支历史；钉住提交若只在远端其他分支上，
        # 先补拉该提交再读（本地无 origin 或提交不存在时维持原样报错）。
        if subprocess.run(["git", "-C", str(repo), "fetch", "--quiet", "origin", commit],
                          capture_output=True).returncode == 0:
            proc = read_blob()
    if proc.returncode != 0:
        fail(f"无法从钉住提交 {commit} 读取 {path}：{proc.stderr.decode(errors='replace').strip()}"
             "（preflight 需要完整历史，checkout 请用 fetch-depth: 0；"
             "若钉住提交在远端其他分支，请确认其已推送）")
    return hashlib.sha256(proc.stdout).hexdigest()


def file_digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path("."), help="repository checkout")
    args = parser.parse_args()

    workflow_text = (args.repo / ".github/workflows/linux.yml").read_text(encoding="utf-8")
    problems = []
    for name, manifest_path, env_var, sources in ANCHORS:
        manifest = json.loads((args.repo / manifest_path).read_text(encoding="utf-8"))
        pin = manifest["commit"]
        match = None
        for candidate in workflow_text.splitlines():
            if candidate.strip().startswith(f"{env_var}:"):
                match = candidate.strip().split(":", 1)[1].strip()
                break
        if match is None:
            problems.append(f"{name}: linux.yml 缺少 {env_var} 定义，性能门禁无法定位基线")
            continue
        if match != pin:
            problems.append(f"{name}: {manifest_path} 钉住 {pin}，而 linux.yml {env_var} 为 {match}；"
                            "两处必须同一变更同步更新")
            continue
        for source in sources:
            if blob_digest(args.repo, pin, source) != file_digest(args.repo / source):
                problems.append(
                    f"{name}: {source} 与钉住提交 {pin} 的内容摘要不一致——锚定源码变更后未重锚。"
                    f"请按 docs/perf-baselines/ci/README.md 的 re-anchoring 规则，在同一变更中把 "
                    f"{manifest_path} 的 commit 与 linux.yml 的 {env_var} 更新为包含该源码变更的已复核提交")
    if problems:
        for problem in problems:
            print(f"::error::preflight: {problem}", flush=True)
        raise SystemExit("preflight 失败：性能基线同步义务未满足（见上方注解）")
    print("preflight: 性能基线钉住提交与锚定源码一致", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
