"""Validate the Designer fixtures and headless preview in a package tree."""

import argparse
from pathlib import Path
import subprocess


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--unpack-root", type=Path, required=True)
    args = parser.parse_args()

    packages = sorted(
        path for path in args.unpack_root.glob("lumen-*") if path.is_dir()
    )
    if len(packages) != 1:
        raise ValueError(f"expected one unpacked package, found {packages}")
    package = packages[0]
    examples = package / "share" / "doc" / "lumen" / "examples"
    for fixture in ("gallery.lumen", "gallery.design"):
        path = examples / fixture
        if not path.is_file():
            raise FileNotFoundError(path)

    binary = package / "bin" / "lumen-designer"
    if not binary.is_file():
        binary = binary.with_suffix(".exe")
    if not binary.is_file():
        raise FileNotFoundError(binary)

    result = subprocess.run(
        [str(binary), "--headless", "--file", str(examples / "gallery.design")],
        cwd=binary.parent,
        capture_output=True,
        text=True,
        timeout=30,
    )
    print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="")
    if result.returncode != 0:
        raise RuntimeError(
            f"Designer headless smoke failed with exit code {result.returncode}"
        )

    lines = result.stdout.splitlines()
    frame_lines = [line for line in lines if line.startswith("frame0 ")]
    if len(frame_lines) != 1:
        raise RuntimeError("Designer headless smoke did not produce frame0")
    frame_hash = frame_lines[0][len("frame0 "):]
    if len(frame_hash) != 16 or any(
        digit not in "0123456789abcdefABCDEF" for digit in frame_hash
    ):
        raise RuntimeError(f"Designer headless smoke produced invalid frame0: {frame_hash}")
    if "document 1" not in lines:
        raise RuntimeError("Designer headless smoke did not load a document")
    diagnostic_line = next(
        (line for line in lines if line.startswith("diagnostics ")), None
    )
    if diagnostic_line != "diagnostics 0":
        raise RuntimeError(
            "Designer headless smoke reported diagnostics: "
            f"{diagnostic_line or '<missing>'}"
        )


if __name__ == "__main__":
    main()
