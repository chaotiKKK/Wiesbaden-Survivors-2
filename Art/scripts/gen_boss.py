"""Boss generator.  blender -b -P Art/scripts/gen_boss.py -- --boss kurhaus"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy

import ws2_common as C


def main():
    args = C.parse_args()
    boss_id = args.get("boss", "kurhaus")

    bosses = C.load_data("bosses.json") or []
    boss = next((b for b in bosses if b.get("id") == boss_id), None)
    if boss is None:
        boss = {"id": boss_id, "name": "Kurhaus-Koloss", "col": "#f4c25a", "r": 46, "hp": 3400}

    color = boss.get("col", "#f4c25a")
    radius = max(0.7, boss.get("r", 46) / 40.0)

    C.reset_scene()
    shell = C.material("boss_shell", color, roughness=0.45, metallic=0.3)
    dark = C.material("boss_dark", "#1b1f26", roughness=0.6, metallic=0.4)
    core = C.material("boss_core", "#ff5ce0", roughness=0.2, emission=4.0)

    parts = [C.sphere("Torso", radius, (0, 0, radius * 1.1), shell, segments=20, rings=14,
                      scale=(1.0, 0.9, 1.15))]
    parts.append(C.sphere("Core", radius * 0.34, (0, -radius * 0.72, radius * 1.2), core,
                          segments=12, rings=8))

    for index in range(8):
        angle = index / 8.0 * math.tau
        parts.append(C.cone("Spike", radius * 0.16, radius * 0.9,
                            (math.cos(angle) * radius * 0.85, math.sin(angle) * radius * 0.85, radius * 1.6),
                            dark, verts=6, rot=(0.5 * math.cos(angle), 0.5 * math.sin(angle), 0)))

    for sign in (-1, 1):
        parts.append(C.cylinder("Arm", radius * 0.18, radius * 1.5, (sign * radius * 1.05, 0, radius * 1.1),
                                shell, verts=10, rot=(0, sign * 0.45, 0)))
        parts.append(C.sphere("Fist", radius * 0.3, (sign * radius * 1.5, 0, radius * 0.5), dark,
                              segments=10, rings=8))

    parts.append(C.cylinder("Crown", radius * 0.7, radius * 0.25, (0, 0, radius * 2.2), shell, verts=8))
    parts.append(C.cone("CrownSpire", radius * 0.35, radius * 0.8, (0, 0, radius * 2.7), core, verts=6))

    name = "SM_Boss_" + (boss.get("name") or boss_id).split("-")[0].title().replace(" ", "")
    mesh = C.join(parts, name)

    rig = C.build_rig(name, radius * 2.4, broad=1.4)
    C.rigid_skin(mesh, rig)

    C.save_blend(f"boss_{boss_id}")
    fbx = C.export_fbx(name, [mesh, rig], "Bosses")
    paths, sheet = C.render_views(f"boss_{boss_id}", [mesh], 640)
    print(f"[gen_boss] boss={boss_id} asset={name} tris={len(mesh.data.polygons)} fbx={fbx} sheet={sheet}")


main()
