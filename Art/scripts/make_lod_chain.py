"""Combine base + LOD FBXs into one UE LOD-group FBX.

blender -b -P Art/scripts/make_lod_chain.py -- --base X.fbx --name SM_X \
    --lod1 X_LOD1.fbx --lod2 X_LOD2.fbx --lod3 X_LOD3.fbx --subfolder Heroes
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy

import ws2_common as C


def import_meshes(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path)
    return [obj for obj in bpy.context.scene.objects
            if obj.type == "MESH" and obj not in before
            and not obj.name.startswith(("Cube", "Light", "Camera"))]


def main():
    args = C.parse_args()
    base = args.get("base")
    name = args.get("name", "SM_Asset")
    subfolder = args.get("subfolder", "")
    if not base:
        raise SystemExit("--base is required")

    C.reset_scene()
    base_meshes = import_meshes(base)
    if not base_meshes:
        raise SystemExit("no base mesh")
    for obj in base_meshes:
        # UE groups a base mesh with LOD1_/LOD2_/... siblings when the base is
        # explicitly tagged as LOD0_ inside the same FBX.
        obj.name = f"LOD0_{name}"

    levels = [base_meshes]
    for level in (1, 2, 3):
        lod_path = args.get(f"lod{level}")
        if not lod_path:
            continue
        meshes = import_meshes(lod_path)
        if not meshes:
            raise SystemExit(f"no mesh in LOD{level}")
        for obj in meshes:
            obj.name = f"LOD{level}_{name}"
        levels.append(meshes)

    combined = [obj for level in levels for obj in level]
    # The Blender factory scene can leak its default cube/camera into the FBX;
    # delete everything that is not one of the four LOD levels before export.
    for obj in list(bpy.data.objects):
        if obj not in combined:
            bpy.data.objects.remove(obj, do_unlink=True)
    out = C.export_fbx(name, combined, os.path.join("LODChains", subfolder))
    print(f"[make_lod_chain] {name}: {len(combined)} objects -> {out}")


main()
