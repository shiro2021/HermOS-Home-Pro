#!/usr/bin/env python3
"""Create the small RGBA runtime texture from the unchanged canonical PNG."""

from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1]
source = root / "assets/design/reference/Futuristic-Golden-Compass-Emblem.png"
target = root / "src/app/hermos_shell/emblem.rgba"

image = Image.open(source).convert("RGBA").resize((80, 80), Image.Resampling.LANCZOS)
target.write_bytes(image.tobytes())
print(f"wrote {target} ({image.width}x{image.height} RGBA)")
