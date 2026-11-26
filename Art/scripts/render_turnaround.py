"""Standalone turnaround render.  blender -b -P Art/scripts/render_turnaround.py -- --input X.fbx --name SM_X"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy

import ws2_common as C


def main():
    args = C.parse_args()
    source = args.get("input")
    name = args.get("name", "asset")
    size = int(args.get("size", 640))

    C.reset_scene()
    if source:
        bpy.ops.import_scene.fbx(filepath=source)

    meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    if not meshes:
        raise SystemExit("nothing to render")

    paths, sheet = C.render_views(name, meshes, size)
    print(f"[render_turnaround] {name}: {len(paths)} views -> {sheet}")


main()
