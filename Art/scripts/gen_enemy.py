"""Enemy generator.  blender -b -P Art/scripts/gen_enemy.py -- --enemy runner"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy

import ws2_common as C


def build_archetype(shape, color, accent, radius_m):
    """Translate the 2D shape language into a readable 3D archetype."""
    body = C.material("enemy_body", color, roughness=0.55, metallic=0.1)
    glow = C.material("enemy_glow", accent, roughness=0.3, emission=2.5)
    dark = C.material("enemy_dark", "#101318", roughness=0.7)

    parts = []
    r = radius_m

    if shape in ("tri",):
        parts.append(C.cone("Body", r, r * 2.0, (0, 0, r), body, verts=3))
        parts.append(C.sphere("Eye", r * 0.28, (0, -r * 0.55, r * 1.25), glow, segments=10, rings=6))
    elif shape == "dot":
        parts.append(C.sphere("Body", r, (0, 0, r), body, segments=14, rings=10, scale=(1.0, 1.0, 0.9)))
        parts.append(C.sphere("Eye", r * 0.35, (0, -r * 0.7, r * 1.1), glow, segments=10, rings=6))
    elif shape in ("box", "brick"):
        parts.append(C.cube("Body", None, (0, 0, r * 0.9), body, bevel=0.02,
                            scale=(r * 1.4, r * 1.2, r * 1.3)))
        parts.append(C.cube("Visor", None, (0, -r * 0.62, r * 1.15), glow, bevel=0.01,
                            scale=(r * 0.9, r * 0.12, r * 0.2)))
        if shape == "brick":
            for sign in (-1, 1):
                parts.append(C.cube("Pauldron", None, (sign * r * 0.8, 0, r * 1.5), dark, bevel=0.02,
                                    scale=(r * 0.45, r * 0.7, r * 0.4)))
    elif shape == "diamond":
        parts.append(C.cone("BodyLower", r, r * 1.4, (0, 0, r * 0.7), body, verts=4,
                            rot=(math.pi, 0, 0)))
        parts.append(C.cone("BodyUpper", r, r * 1.4, (0, 0, r * 1.7), body, verts=4))
        parts.append(C.sphere("Core", r * 0.25, (0, 0, r * 1.2), glow, segments=8, rings=6))
    elif shape in ("hex", "gear", "star", "prism"):
        verts = {"hex": 6, "gear": 10, "star": 6, "prism": 3}.get(shape, 6)
        parts.append(C.cylinder("Body", r, r * 1.6, (0, 0, r * 0.9), body, verts=verts))
        parts.append(C.cylinder("Core", r * 0.4, r * 1.7, (0, 0, r * 0.9), glow, verts=verts))
        if shape in ("star", "gear"):
            for index in range(6):
                angle = index / 6.0 * math.tau
                parts.append(C.cube("Tooth", None,
                                    (math.cos(angle) * r * 0.95, math.sin(angle) * r * 0.95, r * 0.9),
                                    body, bevel=0.01, scale=(r * 0.32, r * 0.18, r * 0.4),
                                    rot=(0, 0, angle)))
    elif shape == "turret":
        parts.append(C.cylinder("Base", r, r * 0.8, (0, 0, r * 0.4), dark, verts=12))
        parts.append(C.sphere("Dome", r * 0.7, (0, 0, r * 1.0), body, segments=12, rings=8))
        parts.append(C.cylinder("Barrel", r * 0.16, r * 1.5, (0, -r * 0.8, r * 1.1), glow, verts=8,
                                rot=(math.pi / 2, 0, 0)))
    elif shape == "spider":
        parts.append(C.sphere("Body", r * 0.8, (0, 0, r * 0.8), body, segments=12, rings=8))
        for index in range(6):
            angle = index / 6.0 * math.tau
            parts.append(C.cylinder("Leg", r * 0.08, r * 1.5,
                                    (math.cos(angle) * r * 0.8, math.sin(angle) * r * 0.8, r * 0.55),
                                    dark, verts=6, rot=(0, math.pi / 2.6, angle)))
        parts.append(C.sphere("Eye", r * 0.3, (0, -r * 0.6, r * 1.0), glow, segments=8, rings=6))
    elif shape == "eye":
        parts.append(C.sphere("Body", r, (0, 0, r), body, segments=16, rings=10))
        parts.append(C.sphere("Iris", r * 0.45, (0, -r * 0.8, r), glow, segments=10, rings=8))
    elif shape == "worm":
        for index in range(4):
            parts.append(C.sphere("Segment", r * (0.85 - index * 0.1),
                                  (0, -r * 1.2 * index, r * 0.7), body if index % 2 == 0 else dark,
                                  segments=10, rings=8))
        parts.append(C.sphere("Mouth", r * 0.28, (0, -r * 0.2, r * 0.75), glow, segments=8, rings=6))
    elif shape == "wing":
        parts.append(C.sphere("Body", r * 0.7, (0, 0, r * 1.1), body, segments=12, rings=8))
        for sign in (-1, 1):
            parts.append(C.cube("Wing", None, (sign * r * 0.9, 0, r * 1.2), dark, bevel=0.01,
                                scale=(r * 1.1, r * 0.5, r * 0.06)))
        parts.append(C.sphere("Eye", r * 0.25, (0, -r * 0.55, r * 1.2), glow, segments=8, rings=6))
    elif shape == "wave":
        parts.append(C.torus("Body", r * 0.8, r * 0.35, (0, 0, r * 0.8), body,
                             rot=(math.pi / 2, 0, 0)))
        parts.append(C.sphere("Core", r * 0.3, (0, 0, r * 0.8), glow, segments=8, rings=6))
    elif shape == "cross":
        parts.append(C.cube("Body", None, (0, 0, r * 0.9), body, bevel=0.02,
                            scale=(r * 0.6, r * 0.6, r * 1.2)))
        parts.append(C.cube("Arms", None, (0, 0, r * 1.1), body, bevel=0.02,
                            scale=(r * 1.5, r * 0.5, r * 0.5)))
        parts.append(C.sphere("Core", r * 0.25, (0, -r * 0.4, r * 1.1), glow, segments=8, rings=6))
    else:
        parts.append(C.cylinder("Body", r, r * 1.6, (0, 0, r * 0.8), body, verts=8))
        parts.append(C.sphere("Core", r * 0.3, (0, 0, r * 1.0), glow, segments=8, rings=6))

    return parts


def main():
    args = C.parse_args()
    enemy_id = args.get("enemy", "runner")

    enemies = C.load_data("enemies.json") or []
    enemy = next((e for e in enemies if e.get("id") == enemy_id), None)
    if enemy is None:
        enemy = {"id": enemy_id, "name": "Laeufer", "col": "#ff5d6e", "shape": "tri", "r": 11}

    color = enemy.get("col", "#ff5d6e")
    shape = enemy.get("shape", "dot")
    radius_m = max(0.18, enemy.get("r", 12) / 100.0)

    C.reset_scene()
    parts = build_archetype(shape, color, "#ffe27a", radius_m)
    # ASCII-only asset names: the id is canonical, the German display name is not a path.
    asset_name = "".join(word.title() for word in enemy_id.split("-"))
    mesh = C.join(parts, f"SM_{asset_name}")

    # Gentle idle bob: baked later into a vertex animation texture.
    scene = bpy.context.scene
    scene.frame_start = 1
    scene.frame_end = 24
    scene.render.fps = 24
    mesh.keyframe_insert("scale", frame=1)
    mesh.scale = (1.0, 1.0, 0.92)
    mesh.keyframe_insert("scale", frame=12)
    mesh.scale = (1.0, 1.0, 1.0)
    mesh.keyframe_insert("scale", frame=24)

    C.save_blend(f"enemy_{enemy_id}")
    fbx = C.export_fbx(f"SM_{asset_name}", [mesh], "Enemies")
    paths, sheet = C.render_views(f"enemy_{enemy_id}", [mesh], 512)

    print(f"[gen_enemy] enemy={enemy_id} asset=SM_{asset_name} shape={shape} "
          f"tris={len(mesh.data.polygons)} fbx={fbx} sheet={sheet}")


if __name__ == "__main__":
    main()
