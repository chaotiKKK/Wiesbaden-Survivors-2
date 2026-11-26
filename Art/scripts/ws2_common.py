"""Shared helpers for the Wiesbaden Survivors 2.0 Blender pipeline.

Everything here is designed to run headless:  blender -b -P <generator>.py -- ...
No viewport clicks, no manual asset edits.
"""
import json
import math
import os

import bpy
import numpy as np
from mathutils import Vector

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ART = os.path.join(ROOT, "Art")
SOURCE = os.path.join(ART, "Source")
EXPORT = os.path.join(ART, "Export")
RENDERS = os.path.join(ART, "Renders")
DATA = os.path.join(ROOT, "Content", "Data")

for folder in (SOURCE, EXPORT, RENDERS):
    os.makedirs(folder, exist_ok=True)


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    return scene


def hex_rgb(value, fallback=(0.8, 0.8, 0.8)):
    if not value:
        return fallback
    text = value.strip().lstrip("#")
    if len(text) < 6:
        return fallback
    return tuple(int(text[i:i + 2], 16) / 255.0 for i in (0, 2, 4))


def load_data(name):
    path = os.path.join(DATA, name)
    if not os.path.exists(path):
        return None
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def material(name, color, metallic=0.05, roughness=0.55, emission=0.0):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    rgb = hex_rgb(color, color if isinstance(color, tuple) else None)
    nodes = mat.node_tree.nodes
    bsdf = nodes.get("Principled BSDF")
    if bsdf:
        bsdf.inputs["Base Color"].default_value = (*rgb, 1.0)
        bsdf.inputs["Roughness"].default_value = roughness
        bsdf.inputs["Metallic"].default_value = metallic
        if emission > 0.0:
            bsdf.inputs["Emission Color"].default_value = (*rgb, 1.0)
            bsdf.inputs["Emission Strength"].default_value = emission
    mat.diffuse_color = (*rgb, 1.0)
    return mat


def _finish(obj, mat, bevel):
    if mat:
        obj.data.materials.append(mat)
    if bevel > 0.0:
        mod = obj.modifiers.new("Bevel", "BEVEL")
        mod.width = bevel
        mod.segments = 2
        mod.limit_method = "ANGLE"
    return obj


def cube(name, size, loc, mat=None, bevel=0.02, scale=None, rot=None):
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=loc)
    obj = bpy.context.object
    obj.name = name
    if scale is None:
        scale = (size[0], size[1], size[2]) if isinstance(size, (tuple, list)) else (size, size, size)
    obj.scale = scale
    if rot:
        obj.rotation_euler = rot
    return _finish(obj, mat, bevel)


def sphere(name, radius, loc, mat=None, segments=20, rings=12, scale=None):
    bpy.ops.mesh.primitive_uv_sphere_add(radius=radius, segments=segments, ring_count=rings, location=loc)
    obj = bpy.context.object
    obj.name = name
    if scale:
        obj.scale = scale
    return _finish(obj, mat, 0.0)


def cylinder(name, radius, depth, loc, mat=None, verts=16, rot=None, scale=None):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=radius, depth=depth, location=loc)
    obj = bpy.context.object
    obj.name = name
    if rot:
        obj.rotation_euler = rot
    if scale:
        obj.scale = scale
    return _finish(obj, mat, 0.01)


def cone(name, radius, depth, loc, mat=None, verts=8, rot=None, scale=None):
    bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=radius, radius2=0.0, depth=depth, location=loc)
    obj = bpy.context.object
    obj.name = name
    if rot:
        obj.rotation_euler = rot
    if scale:
        obj.scale = scale
    return _finish(obj, mat, 0.0)


def torus(name, major, minor, loc, mat=None, rot=None):
    bpy.ops.mesh.primitive_torus_add(major_radius=major, minor_radius=minor, location=loc)
    obj = bpy.context.object
    obj.name = name
    if rot:
        obj.rotation_euler = rot
    return _finish(obj, mat, 0.0)


def join(objects, name):
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.join()
    merged = bpy.context.object
    merged.name = name
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    return merged


# ----------------------------------------------------------------- rigging
def build_rig(name, height, broad=1.0):
    """Simple game skeleton: root, pelvis, spine, chest, head, arms, legs."""
    arm_data = bpy.data.armatures.new(f"{name}_Armature")
    arm_obj = bpy.data.objects.new(f"{name}_Rig", arm_data)
    bpy.context.collection.objects.link(arm_obj)
    bpy.context.view_layer.objects.active = arm_obj
    bpy.ops.object.mode_set(mode="EDIT")

    bones = {}

    def add(bone_name, head, tail, parent=None):
        bone = arm_data.edit_bones.new(bone_name)
        bone.head = head
        bone.tail = tail
        if parent:
            bone.parent = bones[parent]
        bones[bone_name] = bone

    hip = height * 0.52
    add("root", (0, 0, 0), (0, 0, 0.12 * height))
    add("pelvis", (0, 0, hip), (0, 0, hip + 0.08 * height), "root")
    add("spine", (0, 0, hip + 0.08 * height), (0, 0, hip + 0.24 * height), "pelvis")
    add("chest", (0, 0, hip + 0.24 * height), (0, 0, hip + 0.38 * height), "spine")
    add("head", (0, 0, hip + 0.38 * height), (0, 0, height), "chest")

    for side, sign in (("L", 1.0), ("R", -1.0)):
        add(f"upperarm.{side}", (sign * 0.20 * broad, 0, hip + 0.33 * height),
            (sign * 0.30 * broad, 0, hip + 0.10 * height), "chest")
        add(f"forearm.{side}", (sign * 0.30 * broad, 0, hip + 0.10 * height),
            (sign * 0.34 * broad, 0, hip - 0.12 * height), f"upperarm.{side}")
        add(f"thigh.{side}", (sign * 0.09 * broad, 0, hip),
            (sign * 0.10 * broad, 0, hip * 0.5), "pelvis")
        add(f"shin.{side}", (sign * 0.10 * broad, 0, hip * 0.5),
            (sign * 0.10 * broad, 0, 0.02 * height), f"thigh.{side}")

    bpy.ops.object.mode_set(mode="OBJECT")
    return arm_obj


def rigid_skin(mesh_obj, arm_obj):
    """Assign every vertex rigidly to the nearest bone (blockout-grade skinning)."""
    mesh_obj.parent = arm_obj
    modifier = mesh_obj.modifiers.new("Armature", "ARMATURE")
    modifier.object = arm_obj

    centers = []
    for bone in arm_obj.data.bones:
        centers.append((bone.name, (bone.head_local + bone.tail_local) * 0.5))
        mesh_obj.vertex_groups.new(name=bone.name)

    for vertex in mesh_obj.data.vertices:
        point = mesh_obj.matrix_world @ vertex.co
        best_name, best_dist = None, 1e9
        for bone_name, center in centers:
            dist = (point - center).length
            if dist < best_dist:
                best_name, best_dist = bone_name, dist
        if best_name:
            mesh_obj.vertex_groups[best_name].add([vertex.index], 1.0, "REPLACE")


# ----------------------------------------------------------------- export
def save_blend(name):
    path = os.path.join(SOURCE, f"{name}.blend")
    bpy.ops.wm.save_as_mainfile(filepath=path)
    return path


def export_fbx(name, objects, subfolder=""):
    target_dir = os.path.join(EXPORT, subfolder) if subfolder else EXPORT
    os.makedirs(target_dir, exist_ok=True)
    path = os.path.join(target_dir, f"{name}.fbx")
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.export_scene.fbx(
        filepath=path,
        use_selection=True,
        object_types={"MESH", "ARMATURE"},
        use_mesh_modifiers=True,
        apply_scale_options="FBX_SCALE_ALL",
        add_leaf_bones=False,
        bake_anim=True,
    )
    return path


# ----------------------------------------------------------------- render
def _scene_bounds(objects):
    lo = Vector((1e9, 1e9, 1e9))
    hi = Vector((-1e9, -1e9, -1e9))
    for obj in objects:
        if obj.type != "MESH":
            continue
        for corner in obj.bound_box:
            world = obj.matrix_world @ Vector(corner)
            lo = Vector((min(lo[i], world[i]) for i in range(3)))
            hi = Vector((max(hi[i], world[i]) for i in range(3)))
    return lo, hi


def setup_workbench(size):
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.render.resolution_x = size
    scene.render.resolution_y = size
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    shading = scene.display.shading
    shading.light = "STUDIO"
    shading.color_type = "MATERIAL"
    shading.show_shadows = True
    shading.show_cavity = True
    shading.background_type = "VIEWPORT"
    shading.background_color = (0.04, 0.055, 0.075)
    return scene


def render_views(prefix, objects, size=640, views=None):
    scene = setup_workbench(size)
    lo, hi = _scene_bounds(objects)
    center = (lo + hi) * 0.5
    dims = hi - lo
    extent = max(dims.length, 0.2)

    cam_data = bpy.data.cameras.new("TurnaroundCam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = extent * 1.25
    cam = bpy.data.objects.new("TurnaroundCam", cam_data)
    bpy.context.collection.objects.link(cam)
    scene.camera = cam

    directions = views or [
        ("front", Vector((0.0, -1.0, 0.15))),
        ("left", Vector((-1.0, 0.0, 0.15))),
        ("back", Vector((0.0, 1.0, 0.15))),
        ("top", Vector((0.05, -0.05, 1.0))),
    ]

    paths = []
    for label, direction in directions:
        direction = direction.normalized()
        cam.location = center + direction * (extent * 2.5)
        cam.rotation_euler = (center - cam.location).to_track_quat("-Z", "Y").to_euler()
        path = os.path.join(RENDERS, f"{prefix}_{label}.png")
        scene.render.filepath = path
        bpy.ops.render.render(write_still=True)
        paths.append(path)

    sheet = os.path.join(RENDERS, f"{prefix}_sheet.png")
    contact_sheet(paths, sheet)
    return paths, sheet


def contact_sheet(paths, out_path):
    images = [bpy.data.images.load(p) for p in paths]
    width, height = images[0].size
    columns = 2
    rows = math.ceil(len(images) / columns)
    sheet = np.zeros((height * rows, width * columns, 4), dtype=np.float32)
    for index, image in enumerate(images):
        pixels = np.array(image.pixels[:], dtype=np.float32).reshape(height, width, 4)
        row, column = divmod(index, columns)
        top = (rows - 1 - row) * height
        sheet[top:top + height, column * width:(column + 1) * width] = pixels

    image = bpy.data.images.new("ContactSheet", width=width * columns, height=height * rows, alpha=True)
    image.pixels = sheet.ravel()
    image.filepath_raw = out_path
    image.file_format = "PNG"
    image.save()
    return out_path


def parse_args():
    import sys

    argv = sys.argv
    argv = argv[argv.index("--") + 1:] if "--" in argv else []
    out = {}
    key = None
    for token in argv:
        if token.startswith("--"):
            key = token[2:]
            out[key] = True
        elif key:
            out[key] = token
            key = None
    return out
