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
    package = packages[0].resolve()
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

    def run_designer(*arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [str(binary), *arguments],
            cwd=binary.parent,
            capture_output=True,
            text=True,
            timeout=30,
        )

    result = run_designer(
        "--headless", "--file", str(examples / "gallery.design")
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

    invalid_option = run_designer("--headless", "--package-unknown-option")
    if invalid_option.returncode == 0:
        raise RuntimeError("Designer package accepted an unknown option")
    if "usage error: unknown option: --package-unknown-option" not in invalid_option.stderr:
        raise RuntimeError(
            "Designer package unknown-option failure did not report the expected usage error"
        )

    missing_file_value = run_designer("--headless", "--file")
    if missing_file_value.returncode == 0:
        raise RuntimeError("Designer package accepted --file without a value")
    if "usage error: --file requires a value" not in missing_file_value.stderr:
        raise RuntimeError(
            "Designer package missing-file failure did not report the expected usage error"
        )

    option_as_file = run_designer("--headless", "--file", "--max-frames", "1")
    if option_as_file.returncode == 0:
        raise RuntimeError("Designer package accepted an option as the --file value")
    if "usage error: --file requires a value" not in option_as_file.stderr:
        raise RuntimeError(
            "Designer package option-as-file failure did not report the expected usage error"
        )


if __name__ == "__main__":
    main()
