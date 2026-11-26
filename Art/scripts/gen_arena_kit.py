"""Arena kit generator.  blender -b -P Art/scripts/gen_arena_kit.py -- --piece pavilion"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy

import ws2_common as C


def pavilion(stone, metal, accent):
    parts = []
    parts.append(C.cylinder("Base", 2.4, 0.25, (0, 0, 0.125), stone, verts=8))
    for index in range(8):
        angle = index / 8.0 * math.tau
        parts.append(C.cylinder("Column", 0.13, 2.4,
                                (math.cos(angle) * 1.9, math.sin(angle) * 1.9, 1.45), metal, verts=10))
    parts.append(C.cylinder("Ring", 2.15, 0.16, (0, 0, 2.7), accent, verts=8))
    parts.append(C.cone("Roof", 2.7, 1.1, (0, 0, 3.3), metal, verts=8))
    parts.append(C.sphere("Finial", 0.18, (0, 0, 3.95), accent, segments=10, rings=6))
    return parts


def hedge(stone, metal, accent):
    parts = []
    parts.append(C.cube("HedgeBody", None, (0, 0, 0.6), C.material("hedge", "#1d4a2a", roughness=0.8),
                        bevel=0.08, scale=(2.4, 1.0, 1.2)))
    parts.append(C.cube("HedgeTop", None, (0, 0, 1.22), C.material("hedge_light", "#2c6a38", roughness=0.8),
                        bevel=0.06, scale=(2.2, 0.85, 0.16)))
    return parts


def tree(stone, metal, accent):
    trunk = C.material("trunk", "#3a2a1c", roughness=0.85)
    leaves = C.material("leaves", "#22512f", roughness=0.8)
    parts = [C.cylinder("Trunk", 0.24, 2.4, (0, 0, 1.2), trunk, verts=10)]
    parts.append(C.sphere("CrownA", 1.5, (0, 0, 3.0), leaves, segments=14, rings=10, scale=(1.0, 1.0, 0.9)))
    parts.append(C.sphere("CrownB", 1.0, (0.7, 0.4, 3.7), leaves, segments=12, rings=8))
    return parts


def lantern(stone, metal, accent):
    parts = [C.cylinder("Pole", 0.08, 3.4, (0, 0, 1.7), metal, verts=8)]
    parts.append(C.cube("LampHead", None, (0, 0, 3.5), accent, bevel=0.02, scale=(0.34, 0.34, 0.34)))
    parts.append(C.cube("LampCap", None, (0, 0, 3.72), metal, bevel=0.02, scale=(0.44, 0.44, 0.08)))
    return parts


def fountain(stone, metal, accent):
    water = C.material("water", "#2f6f8f", roughness=0.15, metallic=0.2)
    parts = [C.cylinder("Basin", 2.2, 0.5, (0, 0, 0.25), stone, verts=12)]
    parts.append(C.cylinder("Water", 1.95, 0.12, (0, 0, 0.52), water, verts=12))
    parts.append(C.cylinder("Pillar", 0.28, 1.5, (0, 0, 1.25), stone, verts=10))
    parts.append(C.cylinder("Bowl", 1.05, 0.18, (0, 0, 2.0), stone, verts=12))
    parts.append(C.sphere("Jet", 0.22, (0, 0, 2.3), accent, segments=10, rings=6))
    return parts


def bench(stone, metal, accent):
    wood = C.material("bench_wood", "#6b4a2c", roughness=0.7)
    parts = [C.cube("Seat", None, (0, 0, 0.46), wood, bevel=0.03, scale=(1.9, 0.52, 0.1))]
    parts.append(C.cube("Back", None, (0, 0.26, 0.78), wood, bevel=0.03, scale=(1.9, 0.09, 0.46)))
    for sign in (-1, 1):
        parts.append(C.cube("Leg", None, (sign * 0.78, 0, 0.23), metal, bevel=0.01, scale=(0.1, 0.46, 0.46)))
    parts.append(C.cube("Plaque", None, (0, -0.27, 0.52), accent, bevel=0.01, scale=(0.5, 0.03, 0.12)))
    return parts


def plaza(stone, metal, accent):
    """Flat paved tile: the Kurpark's paths, laid edge to edge."""
    parts = [C.cube("Paving", None, (0, 0, 0.04), stone, bevel=0.01, scale=(4.0, 4.0, 0.08))]
    parts.append(C.cube("Inlay", None, (0, 0, 0.09), accent, bevel=0.005, scale=(1.2, 1.2, 0.02)))
    return parts


PIECES = {
    "pavilion": pavilion,
    "hedge": hedge,
    "tree": tree,
    "lantern": lantern,
    "fountain": fountain,
    "bench": bench,
    "plaza": plaza,
}


def main():
    args = C.parse_args()
    piece = args.get("piece", "pavilion")
    builder = PIECES.get(piece, pavilion)

    C.reset_scene()
    stone = C.material("stone", "#6b665c", roughness=0.75)
    metal = C.material("metal", "#2f3a3d", roughness=0.45, metallic=0.7)
    accent = C.material("accent", "#5dff9b", roughness=0.35, emission=1.4)

    parts = builder(stone, metal, accent)
    name = "SM_Kurpark_" + piece.capitalize()
    mesh = C.join(parts, name)

    C.save_blend(f"kit_{piece}")
    fbx = C.export_fbx(name, [mesh], os.path.join("Arena", "Kurpark"))
    paths, sheet = C.render_views(f"kit_{piece}", [mesh], 512)

    lo, hi = C._scene_bounds([mesh])
    print(f"[gen_arena_kit] piece={piece} asset={name} tris={len(mesh.data.polygons)} "
          f"size=({hi.x - lo.x:.1f},{hi.y - lo.y:.1f},{hi.z - lo.z:.1f}) fbx={fbx} sheet={sheet}")


main()
