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
        alpha = data[3::4]
        if mode == "opaque" and any(a != 255 for a in alpha):
            raise ValueError("invalid opaque alpha")
        if any(any(c > a for c, a in zip(data[channel::4], alpha)) for channel in range(3)):
            raise ValueError("invalid premultiplied RGB")
    if mode == "straight":
        alpha = data[3::4]
        for c in range(3):
            data[c::4] = bytes((v*a+127)//255 for v,a in zip(data[c::4],alpha))
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
    # Channel-wise byte operations keep the full Gallery matrix practical using
    # only the standard library. Arithmetic/budgets are identical to P0.
    alpha = data[3::4]
    even = (bytes([200]*16+[80]*16)*((w+31)//32))[:w]
    odd = (bytes([80]*16+[200]*16)*((w+31)//32))[:w]
    background = ((even*16+odd*16)*((h+31)//32))[:w*h]
    for c in range(3):
        channel = data[c::4]
        planes["alpha"][c::3] = alpha
        planes["black"][c::3] = channel
        # Valid premultiplied RGB<=A makes these sums <=255 without clamping.
        planes["white"][c::3] = bytes(v+255-a for v,a in zip(channel,alpha))
        planes["checker"][c::3] = bytes(v+(bg*(255-a)+127)//255
                                          for v,a,bg in zip(channel,alpha,background))
    if peer:
        diff = [bytes(abs(a-b) for a,b in zip(data[c::4],peer[2][c::4])) for c in range(4)]
        maxima = [max(channel) for channel in diff]
        changed = sum(any(pixel) for pixel in zip(*diff))
        budget = 0 if exact else 4
        outside = sum(r>budget or g>budget or b>budget or a!=0 for r,g,b,a in zip(*diff))
        amplify = bytes(min(255,value*32) for value in range(256))
        errors[0::3] = bytes(map(max,zip(*diff))).translate(amplify)
        errors[2::3] = diff[3].translate(amplify)
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
