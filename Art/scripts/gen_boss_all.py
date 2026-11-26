"""Eight arena bosses, one distinct silhouette each.

blender -b -P Art/scripts/gen_boss_all.py

Reads Content/Data/bosses.json, builds one large static archetype per boss from
the same shape language as the horde, exports to Art/Export/Bosses and renders a
turnaround sheet.  The runtime drives the three-phase tables; the mesh only has
to read at boss scale from the top-down camera.
"""
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy

import ws2_common as C
from gen_enemy import build_archetype

# id -> silhouette family.  Every boss reads differently from above.
SHAPES = {
    "kurhaus": "brick",
    "nerobahn": "worm",
    "kochbrunnen": "hex",
    "rogueki": "prism",
    "bergbahn": "gear",
    "sirenkollektiv": "wave",
    "thermalquelle": "star",
    "marktturm": "cross",
}

ACCENTS = {
    "kurhaus": "#ffe27a",
    "nerobahn": "#5dff9b",
    "kochbrunnen": "#ff8a3d",
    "rogueki": "#ff2e88",
    "bergbahn": "#7fe0a8",
    "sirenkollektiv": "#ff5ce0",
    "thermalquelle": "#ff9a3d",
    "marktturm": "#c7a6ff",
}


def title(value):
    return "".join(part.title() for part in value.replace("-", "_").split("_"))


def build_boss(boss, manifest):
    boss_id = boss["id"]
    color = boss.get("col", "#f4c25a")
    accent = ACCENTS.get(boss_id, "#ffe27a")
    radius_m = max(0.9, boss.get("r", 46) / 42.0)
    shape = SHAPES.get(boss_id, "brick")

    C.reset_scene()
    parts = build_archetype(shape, color, accent, radius_m)

    # Boss crown: a broken ring of pylons and a hot core so the silhouette reads
    # as a named encounter, not as a scaled trash mob.
    dark = C.material("boss_trim", "#12151c", roughness=0.55, metallic=0.5)
    core = C.material("boss_core", accent, roughness=0.2, emission=4.0)
    for index in range(8):
        angle = index / 8.0 * math.tau
        parts.append(C.cone("Pylon", radius_m * 0.18, radius_m * 1.1,
                            (math.cos(angle) * radius_m * 1.15, math.sin(angle) * radius_m * 1.15,
                             radius_m * 1.9), dark, verts=5,
                            rot=(0.35 * math.sin(angle), 0.35 * math.cos(angle), 0)))
    parts.append(C.torus("Crown", radius_m * 0.85, radius_m * 0.09,
                         (0, 0, radius_m * 2.15), core, rot=(math.pi / 2, 0, 0)))

    name = "SM_Boss_" + title(boss_id)
    mesh = C.join(parts, name)
    fbx = C.export_fbx(name, [mesh], "Bosses")
    paths, sheet = C.render_views(f"boss_{boss_id}", [mesh], 640)

    entry = {
        "id": boss_id,
        "name": boss.get("name", boss_id),
        "asset": name,
        "shape": shape,
        "tris": len(mesh.data.polygons),
        "fbx": os.path.relpath(fbx, C.ROOT).replace("\\", "/"),
        "sheet": os.path.relpath(sheet, C.ROOT).replace("\\", "/"),
    }
    manifest["bosses"].append(entry)
    print(f"[gen_boss_all] {boss_id}: asset={name} shape={shape} tris={entry['tris']} -> {fbx}")


def main():
    bosses = C.load_data("bosses.json") or []
    manifest = {"bosses": []}
    for boss in bosses:
        build_boss(boss, manifest)
    out_dir = os.path.join(C.EXPORT, "Bosses")
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "boss_manifest.json"), "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2)
    print(f"[gen_boss_all] {len(manifest['bosses'])} bosses -> {out_dir}/boss_manifest.json")


if __name__ == "__main__":
    main()
