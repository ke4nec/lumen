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

    subprocess.run(
        [str(binary), "--headless", "--file", str(examples / "gallery.design")],
        cwd=binary.parent,
        check=True,
        timeout=30,
    )


if __name__ == "__main__":
    main()
