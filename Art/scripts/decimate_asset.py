"""Reduce one authored kit mesh to a triangle budget.

blender -b -P Art/scripts/decimate_asset.py -- --input X.fbx --name SM_X --subfolder Arena/Kurpark --target 2950
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy

import ws2_common as C


def triangle_count(obj):
    obj.data.calc_loop_triangles()
    return len(obj.data.loop_triangles)


def main():
    args = C.parse_args()
    source = args.get("input")
    name = args.get("name", "SM_Asset")
    subfolder = args.get("subfolder", "")
    target = int(args.get("target", 2950))
    if not source:
        raise SystemExit("--input is required")

    C.reset_scene()
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    bpy.ops.import_scene.fbx(filepath=source)
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    if not meshes:
        raise SystemExit("no mesh in input")

    bpy.ops.object.select_all(action="DESELECT")
    for obj in meshes:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    obj = bpy.context.object
    obj.name = name

    # The budget is measured in UE triangles, so author in triangles.
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.quads_convert_to_tris(quad_method="BEAUTY", ngon_method="BEAUTY")
    bpy.ops.object.mode_set(mode="OBJECT")
    before = triangle_count(obj)

    for _attempt in range(4):
        current = triangle_count(obj)
        if current <= target:
            break
        decimate = obj.modifiers.new("Decimate", "DECIMATE")
        decimate.ratio = max(0.05, target / float(current))
        decimate.use_collapse_triangulate = True
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.modifier_apply(modifier=decimate.name)

    after = triangle_count(obj)
    for other in list(bpy.data.objects):
        if other is not obj:
            bpy.data.objects.remove(other, do_unlink=True)
    out = C.export_fbx(name, [obj], subfolder)
    print(f"[decimate] {name}: {before} -> {after} tris (target {target}) -> {out}")


main()
