"""Build and run a consumer outside the repository against a relocated SDK."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    location = parser.add_mutually_exclusive_group(required=True)
    location.add_argument("--prefix", type=Path)
    location.add_argument("--unpack-root", type=Path)
    parser.add_argument("--forbid-path", type=Path, action="append", default=[])
    args = parser.parse_args()
    source = Path(__file__).resolve().parent / "package_consumer"
    if args.unpack_root:
        packages = list(args.unpack_root.glob("*/lib*/cmake/Lumen/LumenConfig.cmake"))
        if len(packages) != 1:
            raise ValueError(f"expected exactly one unpacked SDK, found {packages}")
        args.prefix = packages[0].parents[3]
    # Include spaces so exported targets must handle a normal user install path.
    with tempfile.TemporaryDirectory(prefix="lumen SDK consumer ") as directory:
        root = Path(directory)
        sdk = root / "relocated sdk"
        shutil.copytree(args.prefix, sdk, symlinks=False)
        shutil.copytree(source, root / "source")
        forbidden = [source.parents[1], args.prefix, *args.forbid_path]
        for config in sdk.rglob("*.cmake"):
            content = config.read_text(encoding="utf-8").replace("\\", "/").lower()
            for path in forbidden:
                if path.resolve().as_posix().lower() in content:
                    raise ValueError(f"{config}: exported build/install path leaks {path}")
        configs = list(sdk.glob("lib*/cmake/Lumen/LumenConfig.cmake"))
        if len(configs) != 1:
            raise ValueError(f"expected one installed LumenConfig, found {configs}")
        subprocess.run(["cmake", "-S", str(root / "source"), "-B", str(root / "build"),
                        "-DCMAKE_BUILD_TYPE=Release", f"-DLumen_DIR={configs[0].parent}",
                        "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF",
                        "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF"], check=True)
        subprocess.run(["cmake", "--build", str(root / "build"), "--config", "Release"], check=True)
        executables = list((root / "build").rglob("sdk-consumer.exe" if os.name == "nt" else "sdk-consumer"))
        if len(executables) != 1:
            raise ValueError(f"expected one consumer executable, found {executables}")
        environment = os.environ.copy()
        environment["PATH"] = str(sdk / "bin") + os.pathsep + environment.get("PATH", "")
        subprocess.run([str(executables[0])], env=environment, check=True, timeout=30)


if __name__ == "__main__":
    main()
