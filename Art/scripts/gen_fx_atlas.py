"""FX flipbook atlas generator.  blender -b -P Art/scripts/gen_fx_atlas.py -- --name ring --color "#ffb347" """
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy
import numpy as np

import ws2_common as C


def main():
    args = C.parse_args()
    name = args.get("name", "ring")
    color = C.hex_rgb(args.get("color", "#ffb347"))
    grid = 8
    tile = 64
    size = grid * tile

    atlas = np.zeros((size, size, 4), dtype=np.float32)
    rng = np.random.default_rng(1984)

    total = grid * grid
    for frame in range(total):
        row, column = divmod(frame, grid)
        progress = frame / (total - 1)
        radius = 4.0 + progress * (tile * 0.46)
        thickness = max(1.2, (1.0 - progress) * 5.0 + 0.8)

        ys, xs = np.mgrid[0:tile, 0:tile]
        cx = cy = (tile - 1) * 0.5
        dist = np.sqrt((xs - cx) ** 2 + (ys - cy) ** 2)

        ring = np.exp(-((dist - radius) ** 2) / (2.0 * thickness ** 2))
        fade = (1.0 - progress) ** 1.4
        flicker = 0.85 + 0.15 * rng.random((tile, tile))

        alpha = np.clip(ring * fade * flicker, 0.0, 1.0)
        top = (grid - 1 - row) * tile
        left = column * tile
        atlas[top:top + tile, left:left + tile, 0] = color[0]
        atlas[top:top + tile, left:left + tile, 1] = color[1]
        atlas[top:top + tile, left:left + tile, 2] = color[2]
        atlas[top:top + tile, left:left + tile, 3] = alpha

    out_dir = os.path.join(C.ART, "Textures", "FX")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, f"fx_{name}.png")

    image = bpy.data.images.new(f"fx_{name}", width=size, height=size, alpha=True)
    image.pixels = atlas.ravel()
    image.filepath_raw = out_path
    image.file_format = "PNG"
    image.save()

    with open(os.path.join(out_dir, f"fx_{name}.json"), "w", encoding="utf-8") as handle:
        json.dump({"name": name, "grid": [grid, grid], "tile": tile, "frames": total,
                   "durationFps24": round(total / 24.0, 2)}, handle, indent=2)

    print(f"[gen_fx_atlas] {name}: {grid}x{grid} tiles -> {out_path}")


main()
