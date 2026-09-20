"""Produce alpha/black/white/checker PNGs from raw RGBA and explicit metadata.

Standard library only. Optional --compare uses the frozen P0 bound (4 codes in
premultiplied RGB, exact alpha); reports error counts and a spatial error PNG.
No automatic tolerance changes or golden/hash rewriting.
"""
import argparse
import json
from pathlib import Path
import struct
import zlib


def load(path):
    metadata = dict(line.split("=", 1) for line in Path(str(path) + ".txt").read_text().splitlines() if "=" in line)
    width, height = int(metadata["width"]), int(metadata["height"])
    mode = metadata["alpha_mode"]
    data = bytearray(path.read_bytes())
    if width <= 0 or height <= 0 or len(data) != width * height * 4 or mode not in ("straight", "premultiplied", "opaque"):
        raise ValueError("invalid dimensions or alpha_mode")
    if mode != "straight":
        for i in range(0, len(data), 4):
            if (mode == "opaque" and data[i+3] != 255) or max(data[i:i+3]) > data[i+3]:
                raise ValueError(f"invalid {mode} pixel at {i//4}")
    if mode == "straight":
        for i in range(0, len(data), 4):
            a = data[i+3]
            for c in range(3):
                data[i+c] = (data[i+c]*a+127)//255
    return width, height, data


def png(path, width, height, rgb):
    def chunk(kind, value):
        return struct.pack(">I", len(value)) + kind + value + struct.pack(">I", zlib.crc32(kind + value))
    rows = b"".join(b"\0" + rgb[y*width*3:(y+1)*width*3] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def inspect(path, compare, exact=False):
    w, h, data = load(path)
    planes = {kind: bytearray(w*h*3) for kind in ("alpha", "black", "white", "checker")}
    errors = bytearray(w*h*3)
    peer = load(compare) if compare else None
    if peer and peer[:2] != (w, h):
        raise ValueError("dimension drift")
    maxima = [0, 0, 0, 0]
    changed = outside = 0
    for p in range(w*h):
        i, j = p*4, p*3
        a = data[i+3]
        bg = 80 if ((p%w)//16+(p//w)//16)%2 else 200
        for c in range(3):
            planes["alpha"][j+c] = a
            planes["black"][j+c] = data[i+c]
            planes["white"][j+c] = min(255, data[i+c]+255-a)
            planes["checker"][j+c] = min(255, data[i+c]+(bg*(255-a)+127)//255)
        if peer:
            diff = [abs(data[i+c]-peer[2][i+c]) for c in range(4)]
            maxima = [max(m, d) for m, d in zip(maxima, diff)]
            changed += any(diff)
            outside += max(diff[:3]) > (0 if exact else 4) or diff[3] != 0
            errors[j:j+3] = bytes((min(255, max(diff)*32), 0, min(255, diff[3]*32)))
    for kind, plane in planes.items():
        png(Path(str(path)+f".{kind}.png"), w, h, plane)
    if peer:
        png(Path(str(path)+".diff.png"), w, h, errors)
        report = dict(max_channel_error=maxima, changed_pixels=changed, outside_budget=outside,
                      outside_fraction=outside/(w*h), rgb_budget=0 if exact else 4,
                      comparison="premultiplied RGB and alpha")
        Path(str(path)+".comparison.json").write_text(json.dumps(report, indent=2))
        print(json.dumps(report))
        return outside == 0
    return True


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("frame", type=Path)
    parser.add_argument("--compare", type=Path)
    parser.add_argument("--exact", action="store_true", help="zero error for independent target oracle")
    options = parser.parse_args()
    raise SystemExit(0 if inspect(options.frame, options.compare, options.exact) else 1)
