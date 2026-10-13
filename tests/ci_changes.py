"""Classify changed paths into CI gating categories (push and pull_request).

Emits ``key=value`` lines for $GITHUB_OUTPUT; human progress goes to stderr.
The classifier is conservative by design: whenever it cannot establish a
comparison base it reports every category as true, so a surprise here runs
more gates instead of silently skipping any.
"""

import argparse
import subprocess
import sys
from fnmatch import fnmatch

# 性能门禁关注框架代码、基准与基线锚点；examples 与测试脚本不参与
# scene/designer 基准的锚定源码，docs/design 也不影响。
PERF_PATTERNS = (
    "src/*", "include/*", "benchmarks/*", "docs/perf-baselines/*",
    "cmake/dependencies.cmake", ".github/workflows/linux.yml",
)
# 打包 job 验证 install/CPack 规则、包内冒烟脚本与工作流自身。
PACKAGING_PATTERNS = (
    "cmake/*", "CMakeLists.txt", "examples/*", "tests/package_*.py",
    ".github/workflows/*",
)


def git(repo, *args):
    return subprocess.run(["git", "-C", repo, *args], capture_output=True, text=True)


def is_commit(repo, revision):
    if not revision or set(revision) <= {"0"}:
        return False
    return git(repo, "rev-parse", "--verify", "--quiet", f"{revision}^{{commit}}").returncode == 0


def changed_paths(repo, base):
    """Paths changed between the merge base of `base` and HEAD; None = unknown."""
    if not is_commit(repo, base):
        if not is_commit(repo, "HEAD^"):
            return None
        base_ref = "HEAD^"
        note = "基线不可用，回退比较 HEAD^"
    else:
        merged = git(repo, "merge-base", base, "HEAD")
        if merged.returncode != 0 or not merged.stdout.strip():
            return None
        base_ref = merged.stdout.strip()
        note = f"比较 {base_ref[:12]}..HEAD"
    diff = git(repo, "diff", "--name-only", base_ref, "HEAD")
    if diff.returncode != 0:
        return None
    print(f"changes: {note}，共 {len(diff.stdout.splitlines())} 个文件", file=sys.stderr)
    return diff.stdout.splitlines()


def matches(paths, patterns):
    return any(fnmatch(path, pattern) for path in paths for pattern in patterns)


def emit(key, value):
    print(f"{key}={'true' if value else 'false'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", default=".", help="repository checkout")
    parser.add_argument("--base", default="", help="comparison base (push before / PR base sha)")
    parser.add_argument("--all", action="store_true", help="force every category true")
    args = parser.parse_args()

    paths = None if args.all else changed_paths(args.repo, args.base)
    if paths is None:
        print("changes: 无法确定比较基线，保守触发全部门禁", file=sys.stderr)
        emit("perf", True)
        emit("packaging", True)
        return 0
    emit("perf", matches(paths, PERF_PATTERNS))
    emit("packaging", matches(paths, PACKAGING_PATTERNS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
