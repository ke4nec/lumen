"""Independent target oracle for the fixed alpha-bench images/upload fixtures.

P0 discovers old straight-channel overflow. Those broken pixels are NOT golden
colors: P2/P4 must match this target, while keeping exact alpha and unchanged
sampling/geometry. Uses only the standard library and no renderer helpers.
"""
import argparse
import math
from pathlib import Path
import struct


def f32(value):
    return struct.unpack('f', struct.pack('f', value))[0]


def scaled(value, alpha):
    return math.floor(value * alpha / 255.0 + 0.5)


def reference(output):
    width, height = 1920, 1080
    data = bytearray(width * height * 4)
    # Geometric fixture: clip [60,1760) x [60,960), radius 33.
    # Distance to the inner rectangle gives the circular corner coverage.
    for layer in range(6):
        left, top = 30 + 220 * layer, 40 + 70 * layer
        for y in range(max(60, top), min(960, top + 600)):
            sy = ((y - top) * 2 + 1) * 256 // 1200
            for x in range(max(60, left), min(1760, left + 600)):
                sx = ((x - left) * 2 + 1) * 256 // 1200
                source = [sx, scaled(sy, sx), scaled(32, sx), sx]
                dx = max(93 - (x + 0.5), x + 0.5 - 1727, 0)
                dy = max(93 - (y + 0.5), y + 0.5 - 927, 0)
                coverage = min(1, max(0, f32(33.5 - f32(math.hypot(dx, dy)))))
                if coverage < 1:
                    source = [math.floor(f32(c * coverage) + 0.5) for c in source]
                i = (y * width + x) * 4
                for c in range(4):
                    data[i+c] = source[c] + scaled(data[i+c], 255 - source[3])
    output.write_bytes(data)
    Path(str(output) + '.txt').write_text('width=1920\nheight=1080\nalpha_mode=premultiplied\n')
    return data


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--before', type=Path, help='P0 raw frame to verify identical alpha geometry')
    args = parser.parse_args()
    data = reference(args.output)
    if args.before:
        previous = args.before.read_bytes()
        if len(previous) != len(data) or previous[3::4] != data[3::4]:
            raise SystemExit('alpha/sampling geometry mismatch: oracle must be investigated')
        print('reference alpha exactly matches P0; target red always equals alpha')
