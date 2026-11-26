"""Slice enemy generator: four archetypes, LOD chain, walk-cycle VAT atlas.

blender -b -P Art/scripts/gen_enemy_slice.py

For every archetype this writes
  Art/Export/Enemies/Slice/<Asset>_LOD0..3.fbx   (UV0 = normalised vertex index)
  Art/Textures/VAT/<Asset>_VAT.png + .json       (one atlas row per animation frame)
  Art/Renders/enemy_<id>_sheet.png               (turnaround contact sheet)
and Art/Export/Enemies/Slice/slice_manifest.json summarising all of it.

The atlas uses the LOD0 vertex count as its width, so every LOD samples the same
texture: column i of LODn holds that LOD's vertex i.
"""
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy
import numpy as np

import ws2_common as C
from gen_enemy import build_archetype

FRAMES = 16
FPS = 24
LOD_RATIOS = [0.55, 0.28, 0.12]

# id, def-id used for silhouette, extra scale on the archetype
SLICE_ENEMIES = [
    ("runner", "runner"),
    ("walker", "walker"),
    ("swarm", "swarm"),
    ("tank", "tank"),
]


def find_def(enemy_id):
    enemies = C.load_data("enemies.json") or []
    for enemy in enemies.values() if isinstance(enemies, dict) else enemies:
        if enemy.get("id") == enemy_id:
            return enemy
    return None


def animate_motion(root, kind):
    """Key the motion carrier: bob + rock, heavier for the big archetypes."""
    scene = bpy.context.scene
    scene.frame_start = 1
    scene.frame_end = FRAMES
    scene.render.fps = FPS

    if kind == "swarm":
        bob, rock, squash = 0.05, math.radians(14.0), 0.10
    elif kind == "runner":
        bob, rock, squash = 0.07, math.radians(11.0), 0.08
    elif kind == "tank":
        bob, rock, squash = 0.03, math.radians(5.0), 0.04
    else:
        bob, rock, squash = 0.045, math.radians(7.0), 0.05

    for frame in range(1, FRAMES + 1):
        phase = (frame - 1) / float(FRAMES)
        root.location = (0.0, 0.0, abs(math.sin(phase * math.tau)) * bob)
        root.rotation_euler = (0.0, math.sin(phase * math.tau) * rock, 0.0)
        root.scale = (1.0, 1.0, 1.0 - abs(math.cos(phase * math.tau)) * squash)
        root.keyframe_insert("location", frame=frame)
        root.keyframe_insert("rotation_euler", frame=frame)
        root.keyframe_insert("scale", frame=frame)



def apply_modifiers(obj, keep=("Armature",)):
    """Flatten the stack so baked vertex order equals the exported vertex order."""
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    for modifier in list(obj.modifiers):
        if modifier.type in keep:
            continue
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    obj.select_set(False)


def make_lods(mesh, name):
    """Decimated copies parented to the same motion carrier."""
    levels = [mesh]
    for level, ratio in enumerate(LOD_RATIOS, start=1):
        duplicate = mesh.copy()
        duplicate.data = mesh.data.copy()
        duplicate.name = f"{name}_LOD{level}"
        bpy.context.collection.objects.link(duplicate)
        decimate = duplicate.modifiers.new("Decimate", "DECIMATE")
        decimate.ratio = ratio
        decimate.use_collapse_triangulate = True
        apply_modifiers(duplicate)
        levels.append(duplicate)
    return levels


def write_index_uv(obj, atlas_width):
    mesh = obj.data
    for layer in list(mesh.uv_layers):
        mesh.uv_layers.remove(layer)
    uv_layer = mesh.uv_layers.new(name="UVMap")
    for index, loop in enumerate(mesh.loops):
        vertex_index = loop.vertex_index
        uv_layer.data[index].uv = ((vertex_index + 0.5) / atlas_width, 0.5)


def bake_atlas(levels, asset, out_dir):
    scene = bpy.context.scene
    depsgraph = bpy.context.evaluated_depsgraph_get()

    width = len(levels[0].data.vertices)
    height = FRAMES

    offsets = np.zeros((height, width, 3), dtype=np.float32)
    vertex_counts = []

    for level_index, obj in enumerate(levels):
        rest = None
        frames = np.zeros((height, len(obj.data.vertices), 3), dtype=np.float32)
        for row in range(height):
            scene.frame_set(row + 1)
            bpy.context.view_layer.update()
            evaluated = obj.evaluated_get(depsgraph)
            baked = evaluated.to_mesh()
            matrix = evaluated.matrix_world
            positions = np.array([list(matrix @ v.co) for v in baked.vertices], dtype=np.float32)
            if rest is None:
                rest = positions.copy()
            frames[row, :positions.shape[0]] = positions - rest
            evaluated.to_mesh_clear()
        vertex_counts.append(int(len(obj.data.vertices)))
        offsets[:, : frames.shape[1]] = frames

    low = offsets.reshape(-1, 3).min(axis=0)
    high = offsets.reshape(-1, 3).max(axis=0)
    extent = np.maximum(np.maximum(np.abs(low), np.abs(high)), 1e-5)

    normalised = np.clip(offsets / (extent * 2.0) + 0.5, 0.0, 1.0)

    image = bpy.data.images.new(f"{asset}_VAT", width=width, height=height, alpha=True)
    rgba = np.ones((height, width, 4), dtype=np.float32)
    rgba[:, :, :3] = normalised
    image.pixels = rgba.ravel()
    image.file_format = "PNG"
    os.makedirs(out_dir, exist_ok=True)
    png = os.path.join(out_dir, f"{asset}_VAT.png")
    image.filepath_raw = png
    image.save()

    meta = {
        "asset": asset,
        "frames": height,
        "fps": FPS,
        "atlasWidth": width,
        "vertexCounts": vertex_counts,
        # Offsets are metres; the UE material multiplies by 100 for centimetres.
        "extent": [float(v) for v in extent],
        "boundsMin": [float(v) for v in low],
        "boundsMax": [float(v) for v in high],
        "rowOrder": "bottom-up (frame 1 at the bottom row)",
    }
    with open(os.path.join(out_dir, f"{asset}_VAT.json"), "w", encoding="utf-8") as handle:
        json.dump(meta, handle, indent=2)
    return png, meta


def build_one(enemy_id, def_id, manifest):
    enemy = find_def(def_id) or {"id": def_id, "col": "#ff5d6e", "shape": "dot", "r": 12}
    color = enemy.get("col", "#ff5d6e")
    shape = enemy.get("shape", "dot")
    radius_m = max(0.18, enemy.get("r", 12) / 100.0)

    C.reset_scene()
    C.setup_workbench(512)

    asset = "SM_" + "".join(word.title() for word in enemy_id.split("-"))
    parts = build_archetype(shape, color, "#ffe27a", radius_m)
    mesh = C.join(parts, asset)
    apply_modifiers(mesh)

    root = bpy.data.objects.new(f"{asset}_Motion", None)
    bpy.context.collection.objects.link(root)
    mesh.parent = root

    levels = make_lods(mesh, asset)

    atlas_width = len(levels[0].data.vertices)
    for obj in levels:
        write_index_uv(obj, atlas_width)
        obj.parent = root

    animate_motion(root, enemy_id)

    vat_dir = os.path.join(C.ART, "Textures", "VAT")
    png, meta = bake_atlas(levels, asset, vat_dir)

    bpy.context.scene.frame_set(1)
    export_dir = os.path.join("Enemies", "Slice")
    # UE's importer chains LODs when the meshes follow LODxx_Name inside one file.
    for level_index, obj in enumerate(levels):
        obj.name = asset if level_index == 0 else f"LOD{level_index}_{asset}"
    exports = [C.export_fbx(asset, levels, export_dir)]

    paths, sheet = C.render_views(f"enemy_{enemy_id}", [levels[0]], 512)

    entry = {
        "id": enemy_id,
        "asset": asset,
        "shape": shape,
        "color": color,
        "radiusData": enemy.get("r", 12),
        "tris": [len(obj.data.polygons) for obj in levels],
        "verts": meta["vertexCounts"],
        "exports": [os.path.relpath(path, C.ROOT).replace("\\", "/") for path in exports],
        "vat": os.path.relpath(png, C.ROOT).replace("\\", "/"),
        "frames": meta["frames"],
        "extent": meta["extent"],
        "sheet": os.path.relpath(sheet, C.ROOT).replace("\\", "/"),
    }
    manifest["enemies"].append(entry)
    print(f"[gen_enemy_slice] {enemy_id}: tris={entry['tris']} verts={entry['verts']} "
          f"atlas={atlas_width}x{FRAMES} vat={entry['vat']}")


def main():
    args = C.parse_args()
    if args.get("all"):
        # Full content layer: every row of the source enemy table gets its own
        # silhouette, VAT atlas and LOD chain.  The four slice archetypes keep
        # their ids, so the existing slice references still resolve.
        targets = [(enemy["id"], enemy["id"]) for enemy in (C.load_data("enemies.json") or [])]
        manifest_name = "enemies_all_manifest.json"
    else:
        targets = SLICE_ENEMIES
        manifest_name = "slice_manifest.json"

    manifest = {"enemies": [], "frames": FRAMES, "fps": FPS}
    for enemy_id, def_id in targets:
        build_one(enemy_id, def_id, manifest)

    out_dir = os.path.join(C.EXPORT, "Enemies", "Slice")
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, manifest_name), "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2)
    print(f"[gen_enemy_slice] {len(manifest['enemies'])} enemies -> {out_dir}/{manifest_name}")


if __name__ == "__main__":
    main()
