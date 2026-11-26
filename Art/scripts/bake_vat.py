"""Vertex animation texture baking.

blender -b -P Art/scripts/bake_vat.py -- --input X.fbx --name SM_X

Writes Art/Textures/VAT/<name>_VAT.png (width = vertices, height = frames,
RGB = normalised world position) plus a JSON sidecar with the bounds so the UE
material can decode it without guessing.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy
import numpy as np

import ws2_common as C


def main():
    args = C.parse_args()
    source = args.get("input")
    name = args.get("name", "SM_Asset")
    frames = int(args.get("frames", 24))
    if not source:
        raise SystemExit("--input is required")

    C.reset_scene()
    bpy.ops.import_scene.fbx(filepath=source)

    meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    if not meshes:
        raise SystemExit("no mesh in input")
    mesh = meshes[0]

    scene = bpy.context.scene
    scene.frame_start = 1
    scene.frame_end = frames
    depsgraph = bpy.context.evaluated_depsgraph_get()

    samples = []
    for frame in range(1, frames + 1):
        scene.frame_set(frame)
        evaluated = mesh.evaluated_get(depsgraph)
        baked = evaluated.to_mesh()
        matrix = evaluated.matrix_world
        positions = np.array([list(matrix @ vertex.co) for vertex in baked.vertices], dtype=np.float32)
        samples.append(positions)
        evaluated.to_mesh_clear()

    data = np.stack(samples, axis=0)  # frames, vertices, 3
    vertex_count = data.shape[1]
    low = data.reshape(-1, 3).min(axis=0)
    high = data.reshape(-1, 3).max(axis=0)
    span = np.maximum(high - low, 1e-5)
    normalised = (data - low) / span

    image = bpy.data.images.new(f"{name}_VAT", width=vertex_count, height=frames, alpha=True)
    rgba = np.ones((frames, vertex_count, 4), dtype=np.float32)
    rgba[:, :, :3] = normalised
    image.pixels = rgba.ravel()
    image.file_format = "PNG"

    out_dir = os.path.join(C.ART, "Textures", "VAT")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, f"{name}_VAT.png")
    image.filepath_raw = out_path
    image.save()

    meta = {
        "asset": name,
        "frames": frames,
        "vertices": int(vertex_count),
        "fps": 24,
        "boundsMin": [float(v) for v in low],
        "boundsMax": [float(v) for v in high],
        "rowOrder": "bottom-up (frame 1 at the bottom row)",
        "source": source,
    }
    with open(os.path.join(out_dir, f"{name}_VAT.json"), "w", encoding="utf-8") as handle:
        json.dump(meta, handle, indent=2)

    print(f"[bake_vat] {name}: {frames} frames x {vertex_count} verts -> {out_path}")


main()
