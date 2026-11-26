"""Leonidas slice asset: rigged body with idle / run / attack cycles baked to VAT.

blender -b -P Art/scripts/gen_hero_slice.py

Writes Art/Export/Heroes/Slice/SM_Leonidas_LOD0.fbx plus
Art/Textures/VAT/SM_Leonidas_VAT.png. The atlas stacks three clips:
idle = frames 0..15, run = 16..31, attack = 32..43 (44 rows total).
"""
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy
import numpy as np
from mathutils import Euler

import lib_body
import lib_head
import ws2_common as C

FPS = 24
CLIPS = {"idle": (0, 16), "run": (16, 16), "attack": (32, 12)}
FRAMES = sum(length for _, length in CLIPS.values())


def build_mesh():
    C.reset_scene()
    C.setup_workbench(512)

    parts, head_z, head_r, height, m_skin, m_accent = lib_body.build_body(
        "kid", "#ffcf4a", "#e2564a", "#f2d5b0")
    m_hair = C.material("hair", "#2a1a12", roughness=0.75)
    parts += lib_head.build_head(head_z, head_r, m_skin, m_hair)
    m_hat = C.material("hat_primary", "#ffcf4a", metallic=0.2, roughness=0.4)
    m_hat_accent = C.material("hat_accent", "#e2564a", metallic=0.35, roughness=0.35)
    parts += lib_head.build_hat("fire", head_z, head_r, m_hat, m_hat_accent)
    m_prop = C.material("prop_wood", "#8a5a2a", roughness=0.6)
    parts += lib_body.build_prop("guitar", 0.55 * height / 1.78, height * 0.46, m_prop, m_hat_accent, height)

    mesh = C.join(parts, "SM_Leonidas")
    # Flatten geometry modifiers before skinning so vertex order stays stable.
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = mesh
    mesh.select_set(True)
    for modifier in list(mesh.modifiers):
        if modifier.type != "ARMATURE":
            bpy.ops.object.modifier_apply(modifier=modifier.name)
    mesh.select_set(False)

    rig = C.build_rig("Leonidas", height, broad=1.0)
    C.rigid_skin(mesh, rig)
    return mesh, rig, height


def key_bone(rig, name, frame, rotation=None, location=None):
    pose_bone = rig.pose.bones.get(name)
    if pose_bone is None:
        return
    pose_bone.rotation_mode = "XYZ"
    if rotation is not None:
        pose_bone.rotation_euler = Euler(rotation)
        pose_bone.keyframe_insert("rotation_euler", frame=frame)
    if location is not None:
        pose_bone.location = location
        pose_bone.keyframe_insert("location", frame=frame)


def animate(rig):
    scene = bpy.context.scene
    scene.render.fps = FPS
    scene.frame_start = 1
    scene.frame_end = FRAMES
    rig.animation_data_create()
    action = bpy.data.actions.new("Leonidas_Slice")
    rig.animation_data.action = action

    # --- idle: breathing + small arm sway -----------------------------------
    idle_start, idle_len = CLIPS["idle"]
    for index in range(idle_len):
        frame = idle_start + index + 1
        phase = index / float(idle_len)
        breath = math.sin(phase * math.tau) * 0.05
        key_bone(rig, "chest", frame, rotation=(breath, 0.0, 0.0))
        key_bone(rig, "head", frame, rotation=(-breath * 0.6, 0.0, math.sin(phase * math.tau) * 0.08))
        key_bone(rig, "upperarm.L", frame, rotation=(math.sin(phase * math.tau) * 0.10, 0.0, 0.0))
        key_bone(rig, "upperarm.R", frame, rotation=(-math.sin(phase * math.tau) * 0.10, 0.0, 0.0))
        key_bone(rig, "pelvis", frame, location=(0.0, 0.0, 0.0))

    # --- run: opposing limbs, forward lean, vertical bob --------------------
    run_start, run_len = CLIPS["run"]
    for index in range(run_len):
        frame = run_start + index + 1
        phase = index / float(run_len) * math.tau
        swing = math.sin(phase)
        key_bone(rig, "thigh.L", frame, rotation=(swing * 0.85, 0.0, 0.0))
        key_bone(rig, "thigh.R", frame, rotation=(-swing * 0.85, 0.0, 0.0))
        key_bone(rig, "shin.L", frame, rotation=(max(0.0, -swing) * 0.9, 0.0, 0.0))
        key_bone(rig, "shin.R", frame, rotation=(max(0.0, swing) * 0.9, 0.0, 0.0))
        key_bone(rig, "upperarm.L", frame, rotation=(-swing * 0.75, 0.0, 0.0))
        key_bone(rig, "upperarm.R", frame, rotation=(swing * 0.75, 0.0, 0.0))
        key_bone(rig, "chest", frame, rotation=(0.18, 0.0, swing * 0.10))
        key_bone(rig, "pelvis", frame, location=(0.0, 0.0, abs(math.sin(phase * 2.0)) * 0.05))

    # --- attack: wind up, swing the right arm across, recover ---------------
    attack_start, attack_len = CLIPS["attack"]
    for index in range(attack_len):
        frame = attack_start + index + 1
        t = index / float(attack_len - 1)
        if t < 0.4:
            swing = -1.2 * (t / 0.4)
        elif t < 0.7:
            swing = -1.2 + 3.0 * ((t - 0.4) / 0.3)
        else:
            swing = 1.8 * (1.0 - (t - 0.7) / 0.3)
        key_bone(rig, "upperarm.R", frame, rotation=(swing, 0.0, swing * 0.35))
        key_bone(rig, "forearm.R", frame, rotation=(swing * 0.4, 0.0, 0.0))
        key_bone(rig, "chest", frame, rotation=(0.0, 0.0, -swing * 0.18))
        key_bone(rig, "pelvis", frame, location=(0.0, 0.0, 0.0))


def write_index_uv(mesh, atlas_width):
    for layer in list(mesh.data.uv_layers):
        mesh.data.uv_layers.remove(layer)
    uv_layer = mesh.data.uv_layers.new(name="UVMap")
    for index, loop in enumerate(mesh.data.loops):
        uv_layer.data[index].uv = ((loop.vertex_index + 0.5) / atlas_width, 0.5)


def bake(mesh, asset):
    scene = bpy.context.scene
    width = len(mesh.data.vertices)
    depsgraph = bpy.context.evaluated_depsgraph_get()

    rest = None
    frames = np.zeros((FRAMES, width, 3), dtype=np.float32)
    for row in range(FRAMES):
        scene.frame_set(row + 1)
        bpy.context.view_layer.update()
        evaluated = mesh.evaluated_get(depsgraph)
        baked = evaluated.to_mesh()
        matrix = evaluated.matrix_world
        positions = np.array([list(matrix @ v.co) for v in baked.vertices], dtype=np.float32)
        if rest is None:
            rest = positions.copy()
        frames[row, :positions.shape[0]] = positions - rest
        evaluated.to_mesh_clear()

    low = frames.reshape(-1, 3).min(axis=0)
    high = frames.reshape(-1, 3).max(axis=0)
    extent = np.maximum(np.maximum(np.abs(low), np.abs(high)), 1e-5)
    normalised = np.clip(frames / (extent * 2.0) + 0.5, 0.0, 1.0)

    image = bpy.data.images.new(f"{asset}_VAT", width=width, height=FRAMES, alpha=True)
    rgba = np.ones((FRAMES, width, 4), dtype=np.float32)
    rgba[:, :, :3] = normalised
    image.pixels = rgba.ravel()
    image.file_format = "PNG"

    out_dir = os.path.join(C.ART, "Textures", "VAT")
    os.makedirs(out_dir, exist_ok=True)
    png = os.path.join(out_dir, f"{asset}_VAT.png")
    image.filepath_raw = png
    image.save()

    meta = {
        "asset": asset,
        "frames": FRAMES,
        "fps": FPS,
        "atlasWidth": width,
        "vertexCounts": [width],
        "clips": {name: {"start": start, "count": length} for name, (start, length) in CLIPS.items()},
        "extent": [float(v) for v in extent],
        "boundsMin": [float(v) for v in low],
        "boundsMax": [float(v) for v in high],
        "rowOrder": "bottom-up (frame 1 at the bottom row)",
    }
    with open(os.path.join(out_dir, f"{asset}_VAT.json"), "w", encoding="utf-8") as handle:
        json.dump(meta, handle, indent=2)
    return png, meta


def main():
    mesh, rig, height = build_mesh()
    atlas_width = len(mesh.data.vertices)
    write_index_uv(mesh, atlas_width)
    animate(rig)

    png, meta = bake(mesh, "SM_Leonidas")

    bpy.context.scene.frame_set(1)
    export_dir = os.path.join("Heroes", "Slice")
    mesh.name = "SM_Leonidas"
    fbx = C.export_fbx("SM_Leonidas", [mesh, rig], export_dir)

    paths, sheet = C.render_views("hero_leonidas_slice", [mesh], 640)

    manifest = {
        "asset": "SM_Leonidas",
        "heightMeters": height,
        "tris": len(mesh.data.polygons),
        "verts": atlas_width,
        "export": os.path.relpath(fbx, C.ROOT).replace("\\", "/"),
        "vat": os.path.relpath(png, C.ROOT).replace("\\", "/"),
        "clips": meta["clips"],
        "frames": meta["frames"],
        "extent": meta["extent"],
        "sheet": os.path.relpath(sheet, C.ROOT).replace("\\", "/"),
    }
    out_path = os.path.join(C.EXPORT, "Heroes", "Slice", "hero_manifest.json")
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2)

    print(f"[gen_hero_slice] tris={manifest['tris']} verts={atlas_width} "
          f"clips={meta['clips']} vat={manifest['vat']}")


main()
