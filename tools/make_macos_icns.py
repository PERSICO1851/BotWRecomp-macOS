#!/usr/bin/env python3
"""Pack a standard macOS iconset directory into an ICNS file."""

import argparse
import struct
from pathlib import Path

CHUNKS = (
    ("icp4", "icon_16x16.png"),
    ("icp5", "icon_32x32.png"),
    ("icp6", "icon_64x64.png"),
    ("ic07", "icon_128x128.png"),
    ("ic08", "icon_256x256.png"),
    ("ic09", "icon_512x512.png"),
    ("ic10", "icon_512x512@2x.png"),
)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("iconset", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()

payload = bytearray()
for chunk_type, filename in CHUNKS:
    image = (args.iconset / filename).read_bytes()
    payload += chunk_type.encode("ascii") + struct.pack(">I", len(image) + 8) + image
args.output.write_bytes(b"icns" + struct.pack(">I", len(payload) + 8) + payload)
