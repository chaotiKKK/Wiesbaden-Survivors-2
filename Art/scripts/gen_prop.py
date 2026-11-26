"""Prop generator.  blender -b -P Art/scripts/gen_prop.py -- --prop crate"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy

import ws2_common as C


def crate(wood, metal, accent):
    parts = [C.cube("CrateBody", None, (0, 0, 0.45), wood, bevel=0.02, scale=(0.9, 0.9, 0.9))]
    for sign in (-1, 1):
        parts.append(C.cube("Band", None, (0, sign * 0.46, 0.45), metal, bevel=0.01,
                            scale=(0.94, 0.04, 0.14)))
    return parts


def barrel(wood, metal, accent):
    parts = [C.cylinder("BarrelBody", 0.34, 0.95, (0, 0, 0.475), wood, verts=14)]
    for height in (0.22, 0.73):
        parts.append(C.cylinder("BarrelRing", 0.36, 0.06, (0, 0, height), metal, verts=14))
    parts.append(C.cylinder("BarrelTop", 0.33, 0.04, (0, 0, 0.96), metal, verts=14))
    return parts


def bench(wood, metal, accent):
    parts = [C.cube("Seat", None, (0, 0, 0.45), wood, bevel=0.02, scale=(1.8, 0.22, 0.08)),
             C.cube("Back", None, (0, 0.24, 0.78), wood, bevel=0.02, scale=(1.8, 0.07, 0.3))]
    for sign in (-1, 1):
        parts.append(C.cube("Leg", None, (sign * 0.75, 0, 0.22), metal, bevel=0.01,
                            scale=(0.09, 0.18, 0.44)))
    return parts


def kiosk(wood, metal, accent):
    parts = [C.cube("KioskBody", None, (0, 0, 1.1), wood, bevel=0.03, scale=(2.2, 1.6, 2.2))]
    parts.append(C.cube("KioskRoof", None, (0, 0, 2.3), metal, bevel=0.03, scale=(2.5, 1.9, 0.14)))
    parts.append(C.cube("KioskAwning", None, (0, -1.0, 2.0), accent, bevel=0.02,
                        scale=(2.4, 0.7, 0.06)))
    parts.append(C.cube("KioskWindow", None, (0, -0.81, 1.3), C.material("kiosk_glass", "#8fd8ff",
                                                                        roughness=0.2),
                        bevel=0.01, scale=(1.4, 0.05, 0.8)))
    return parts


PROPS = {"crate": crate, "barrel": barrel, "bench": bench, "kiosk": kiosk}


def main():
    args = C.parse_args()
    kind = args.get("prop", "crate")

    C.reset_scene()
    wood = C.material("wood", "#7a5230", roughness=0.75)
    metal = C.material("prop_metal", "#3a4247", roughness=0.5, metallic=0.6)
    accent = C.material("prop_accent", "#ffe27a", roughness=0.4, emission=0.8)

    parts = PROPS.get(kind, crate)(wood, metal, accent)
    name = "SM_Prop_" + kind.capitalize()
    mesh = C.join(parts, name)

    C.save_blend(f"prop_{kind}")
    fbx = C.export_fbx(name, [mesh], "Props")
    paths, sheet = C.render_views(f"prop_{kind}", [mesh], 512)
    print(f"[gen_prop] prop={kind} tris={len(mesh.data.polygons)} fbx={fbx} sheet={sheet}")


main()
