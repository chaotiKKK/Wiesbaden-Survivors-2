"""LOD chain generator.  blender -b -P Art/scripts/make_lods.py -- --input X.fbx --name SM_X --subfolder Heroes"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy

import ws2_common as C

RATIOS = [0.5, 0.25, 0.12]


def main():
    args = C.parse_args()
    source = args.get("input")
    name = args.get("name", "SM_Asset")
    subfolder = args.get("subfolder", "")
    if not source:
        raise SystemExit("--input is required")

    C.reset_scene()
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    bpy.ops.import_scene.fbx(filepath=source)
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    if not meshes:
        raise SystemExit("no mesh in input")

    originals = [(obj.name, len(obj.data.polygons)) for obj in meshes]
    outputs = []

    for level, ratio in enumerate(RATIOS, start=1):
        for obj in meshes:
            duplicate = obj.copy()
            duplicate.data = obj.data.copy()
            duplicate.name = f"{obj.name}_LOD{level}"
            bpy.context.collection.objects.link(duplicate)

            decimate = duplicate.modifiers.new("Decimate", "DECIMATE")
            decimate.ratio = ratio
            decimate.use_collapse_triangulate = True

        lod_objects = [bpy.context.scene.objects[f"{obj.name}_LOD{level}"] for obj in meshes]
        outputs.append(C.export_fbx(f"{name}_LOD{level}", lod_objects, subfolder))

    print(f"[make_lods] source={name} originals={originals} lods={outputs}")


main()
