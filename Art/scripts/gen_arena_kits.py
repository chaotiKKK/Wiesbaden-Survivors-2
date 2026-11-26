"""Wiesbaden arena kits: one distinct module set per arena.

blender -b -P Art/scripts/gen_arena_kits.py -- --kit all

Each kit writes one FBX per module to Art/Export/Arena/<Kit>/ and a turnaround
sheet to Art/Renders/.  The C++ arena builder assembles the runtime layout from
these modules; the Kurpark kit (gen_arena_kit.py) stays the reference set.
"""
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy

import ws2_common as C

# ---------------------------------------------------------------- palette

PALETTES = {
    "Innenstadt": {"stone": "#3b3f50", "dark": "#161922", "accent": "#ffe27a"},
    "Rheinufer": {"stone": "#25415c", "dark": "#0b1724", "accent": "#39e6ff"},
    "Labor": {"stone": "#3a2b49", "dark": "#140f1d", "accent": "#a06bff"},
    "Neroberg": {"stone": "#4a3034", "dark": "#1b1013", "accent": "#ff2e88"},
    "Warmerdamm": {"stone": "#4f4a2e", "dark": "#201d12", "accent": "#ffd23e"},
    "Schlachthof": {"stone": "#4a2a30", "dark": "#190d10", "accent": "#ff6a3d"},
}


def mats(kit):
    p = PALETTES[kit]
    stone = C.material("kit_stone", p["stone"], roughness=0.8)
    dark = C.material("kit_dark", p["dark"], roughness=0.6, metallic=0.3)
    accent = C.material("kit_accent", p["accent"], roughness=0.3, emission=1.8)
    light = C.material("kit_light", "#d8d2c0", roughness=0.5)
    return stone, dark, accent, light


# ---------------------------------------------------------------- Innenstadt

def facade(stone, dark, accent, light):
    parts = [C.cube("Block", None, (0, 0, 2.0), stone, bevel=0.03, scale=(2.4, 1.2, 4.0))]
    for row in range(3):
        for col in (-1, 1):
            parts.append(C.cube("Window", None, (col * 0.9, -1.22, 1.2 + row * 1.1), accent,
                                bevel=0.01, scale=(0.45, 0.05, 0.5)))
    parts.append(C.cube("Cornice", None, (0, 0, 4.1), dark, bevel=0.02, scale=(2.7, 1.4, 0.18)))
    parts.append(C.cube("Roof", None, (0, 0, 4.45), dark, bevel=0.02, scale=(2.5, 1.2, 0.5)))
    return parts


def arcade(stone, dark, accent, light):
    parts = [C.cube("Base", None, (0, 0, 0.2), dark, bevel=0.02, scale=(2.8, 1.2, 0.4))]
    for x in (-1.1, 0.0, 1.1):
        parts.append(C.cylinder("Pillar", 0.16, 2.2, (x, 0, 1.4), stone, verts=10))
        parts.append(C.cube("Capital", None, (x, 0, 2.55), accent, bevel=0.01, scale=(0.4, 0.4, 0.12)))
    parts.append(C.cube("Beam", None, (0, 0, 2.75), stone, bevel=0.02, scale=(2.9, 1.1, 0.4)))
    return parts


def column(stone, dark, accent, light):
    return [
        C.cylinder("Foot", 0.45, 0.3, (0, 0, 0.15), dark, verts=10),
        C.cylinder("Shaft", 0.28, 2.6, (0, 0, 1.6), stone, verts=12),
        C.cylinder("Capital", 0.46, 0.24, (0, 0, 3.0), accent, verts=10),
    ]


def gate(stone, dark, accent, light):
    parts = []
    for sign in (-1, 1):
        parts.append(C.cube("Pillar", None, (sign * 0.95, 0, 1.3), stone, bevel=0.02,
                            scale=(0.45, 0.7, 2.6)))
    parts.append(C.cube("Lintel", None, (0, 0, 2.7), dark, bevel=0.02, scale=(2.5, 0.8, 0.35)))
    parts.append(C.cube("Neon", None, (0, -0.42, 2.7), accent, bevel=0.01, scale=(2.1, 0.05, 0.08)))
    return parts


def planter(stone, dark, accent, light):
    parts = [C.cube("Box", None, (0, 0, 0.4), stone, bevel=0.03, scale=(1.3, 1.3, 0.8))]
    parts.append(C.sphere("Bush", 0.7, (0, 0, 1.15), C.material("bush", "#235c33", roughness=0.8),
                          segments=10, rings=8))
    return parts


# ---------------------------------------------------------------- Rheinufer

def railing(stone, dark, accent, light):
    parts = [C.cube("Rail", None, (0, 0, 1.0), dark, bevel=0.01, scale=(3.4, 0.12, 0.12))]
    parts.append(C.cube("RailLow", None, (0, 0, 0.55), dark, bevel=0.01, scale=(3.4, 0.1, 0.1)))
    for x in (-1.5, -0.5, 0.5, 1.5):
        parts.append(C.cylinder("Post", 0.09, 1.15, (x, 0, 0.58), stone, verts=8))
    parts.append(C.sphere("Lamp", 0.14, (1.5, 0, 1.25), accent, segments=8, rings=6))
    return parts


def kiosk(stone, dark, accent, light):
    parts = [C.cube("Body", None, (0, 0, 0.9), stone, bevel=0.02, scale=(1.8, 1.4, 1.8))]
    parts.append(C.cube("Awning", None, (0, -0.2, 1.95), accent, bevel=0.01, scale=(2.1, 1.7, 0.12)))
    parts.append(C.cube("Window", None, (0, -0.72, 1.05), light, bevel=0.01, scale=(1.2, 0.06, 0.8)))
    parts.append(C.cube("Sign", None, (0, -0.76, 1.6), accent, bevel=0.01, scale=(0.9, 0.05, 0.18)))
    return parts


def ponton(stone, dark, accent, light):
    parts = [C.cube("Deck", None, (0, 0, 0.18), stone, bevel=0.02, scale=(2.6, 2.0, 0.35))]
    for x in (-1.1, 1.1):
        for y in (-0.8, 0.8):
            parts.append(C.cylinder("Bollard", 0.12, 0.5, (x, y, 0.5), dark, verts=8))
            parts.append(C.sphere("Cap", 0.13, (x, y, 0.78), accent, segments=8, rings=6))
    return parts


def crane(stone, dark, accent, light):
    parts = [C.cube("Tower", None, (0, 0, 2.0), dark, bevel=0.02, scale=(0.4, 0.4, 4.0))]
    parts.append(C.cube("Jib", None, (0.8, 0, 3.9), stone, bevel=0.02, scale=(2.4, 0.3, 0.25)))
    parts.append(C.cylinder("Cable", 0.02, 2.4, (1.8, 0, 2.7), accent, verts=6))
    parts.append(C.cube("Hook", None, (1.8, 0, 1.45), dark, bevel=0.02, scale=(0.3, 0.3, 0.3)))
    return parts


def bollard(stone, dark, accent, light):
    return [
        C.cylinder("Post", 0.22, 0.9, (0, 0, 0.45), dark, verts=10),
        C.torus("Ring", 0.26, 0.06, (0, 0, 0.82), accent, rot=(math.pi / 2, 0, 0)),
    ]


# ---------------------------------------------------------------- Labor

def labwall(stone, dark, accent, light):
    parts = [C.cube("Panel", None, (0, 0, 1.8), stone, bevel=0.02, scale=(2.6, 0.5, 3.6))]
    for x in (-0.8, 0.2, 1.0):
        parts.append(C.cube("Screen", None, (x, -0.28, 2.1), accent, bevel=0.01, scale=(0.4, 0.05, 0.6)))
    parts.append(C.cube("Vent", None, (0, -0.3, 0.8), dark, bevel=0.01, scale=(1.6, 0.05, 0.35)))
    return parts


def tank(stone, dark, accent, light):
    parts = [C.cylinder("Body", 0.9, 2.6, (0, 0, 1.4), stone, verts=14)]
    parts.append(C.sphere("Dome", 0.9, (0, 0, 2.75), dark, segments=14, rings=8, scale=(1, 1, 0.5)))
    for z in (0.7, 1.5, 2.3):
        parts.append(C.cylinder("Band", 0.95, 0.08, (0, 0, z), accent, verts=14))
    parts.append(C.cylinder("Pipe", 0.12, 1.2, (0.75, 0, 2.2), dark, verts=8, rot=(0, math.pi / 2, 0)))
    return parts


def console(stone, dark, accent, light):
    parts = [C.cube("Desk", None, (0, 0, 0.55), dark, bevel=0.02, scale=(1.8, 0.9, 1.0))]
    for x in (-0.45, 0.45):
        parts.append(C.cube("Screen", None, (x, -0.35, 1.3), accent, bevel=0.01, scale=(0.65, 0.08, 0.5)))
    parts.append(C.cube("Keyboard", None, (0, -0.3, 1.02), light, bevel=0.01, scale=(1.1, 0.35, 0.06)))
    return parts


def pipe(stone, dark, accent, light):
    parts = [C.cylinder("Pipe", 0.16, 3.0, (0, 0, 1.3), stone, verts=10, rot=(0, math.pi / 2, 0))]
    parts.append(C.cylinder("Elbow", 0.2, 0.8, (1.4, 0, 1.0), dark, verts=10))
    parts.append(C.cylinder("Valve", 0.3, 0.16, (0, 0, 1.3), accent, verts=12))
    return parts


def crate(stone, dark, accent, light):
    parts = [C.cube("Box", None, (0, 0, 0.65), stone, bevel=0.02, scale=(1.4, 1.4, 1.3))]
    parts.append(C.cube("BraceA", None, (0, -0.72, 0.65), dark, bevel=0.01, scale=(1.5, 0.06, 0.12),
                        rot=(0, 0, 0.6)))
    parts.append(C.cube("Label", None, (0, -0.73, 1.05), accent, bevel=0.01, scale=(0.5, 0.04, 0.2)))
    return parts


# ---------------------------------------------------------------- Neroberg

def pine(stone, dark, accent, light):
    leaves = C.material("pine", "#1d4a32", roughness=0.8)
    parts = [C.cylinder("Trunk", 0.18, 2.0, (0, 0, 1.0), dark, verts=8)]
    for index, radius in enumerate((1.2, 0.9, 0.6)):
        parts.append(C.cone("Crown", radius, 1.3, (0, 0, 2.0 + index * 0.8), leaves, verts=8))
    return parts


def monopteros(stone, dark, accent, light):
    parts = [C.cylinder("Base", 2.8, 0.35, (0, 0, 0.18), stone, verts=12)]
    for index in range(10):
        angle = index / 10.0 * math.tau
        parts.append(C.cylinder("Column", 0.14, 2.8,
                                (math.cos(angle) * 2.2, math.sin(angle) * 2.2, 1.75),
                                light, verts=10))
    parts.append(C.cylinder("Ring", 2.45, 0.18, (0, 0, 3.2), accent, verts=12))
    parts.append(C.cone("Roof", 2.9, 1.0, (0, 0, 3.8), dark, verts=12))
    parts.append(C.sphere("Finial", 0.18, (0, 0, 4.45), accent, segments=10, rings=6))
    return parts


def rail(stone, dark, accent, light):
    parts = [C.cube("Beam", None, (0, 0, 0.5), dark, bevel=0.01, scale=(3.2, 0.18, 0.18))]
    for x in (-1.4, -0.7, 0.0, 0.7, 1.4):
        parts.append(C.cylinder("Post", 0.06, 0.8, (x, 0, 0.4), stone, verts=6))
    parts.append(C.cube("Cable", None, (0, 0, 1.15), accent, bevel=0.01, scale=(3.2, 0.04, 0.04)))
    return parts


def funicular(stone, dark, accent, light):
    parts = [C.cube("Car", None, (0, 0, 1.0), stone, bevel=0.03, scale=(2.2, 1.2, 1.6))]
    parts.append(C.cube("Roof", None, (0, 0, 1.9), dark, bevel=0.02, scale=(2.4, 1.3, 0.2)))
    for x in (-0.6, 0.6):
        parts.append(C.cube("Window", None, (x, -0.62, 1.15), accent, bevel=0.01, scale=(0.55, 0.05, 0.5)))
    for x in (-0.7, 0.7):
        parts.append(C.cylinder("Wheel", 0.24, 0.12, (x, 0.4, 0.25), dark, verts=10,
                                rot=(math.pi / 2, 0, 0)))
    return parts


def vista(stone, dark, accent, light):
    parts = [C.cube("Ledge", None, (0, 0, 0.2), stone, bevel=0.02, scale=(2.8, 1.4, 0.4))]
    for x in (-1.1, 0.0, 1.1):
        parts.append(C.cylinder("Baluster", 0.12, 0.8, (x, 0.45, 0.8), light, verts=8))
    parts.append(C.cube("Rail", None, (0, 0.45, 1.25), dark, bevel=0.01, scale=(2.6, 0.18, 0.12)))
    return parts


# ---------------------------------------------------------------- Warmerdamm

def hedge(stone, dark, accent, light):
    return [
        C.cube("Body", None, (0, 0, 0.6), C.material("hedge2", "#1d4a2a", roughness=0.8),
               bevel=0.06, scale=(2.4, 1.0, 1.2)),
        C.cube("Top", None, (0, 0, 1.22), C.material("hedge2l", "#2c6a38", roughness=0.8),
               bevel=0.05, scale=(2.2, 0.85, 0.16)),
    ]


def bandstand(stone, dark, accent, light):
    parts = [C.cylinder("Base", 2.2, 0.3, (0, 0, 0.15), stone, verts=10)]
    for index in range(8):
        angle = index / 8.0 * math.tau
        parts.append(C.cylinder("Column", 0.1, 2.0,
                                (math.cos(angle) * 1.7, math.sin(angle) * 1.7, 1.3), light, verts=8))
    parts.append(C.cone("Roof", 2.4, 0.9, (0, 0, 2.65), dark, verts=10))
    parts.append(C.torus("Trim", 1.85, 0.08, (0, 0, 2.1), accent, rot=(math.pi / 2, 0, 0)))
    return parts


def palm(stone, dark, accent, light):
    trunk = C.material("palm_trunk", "#5a4028", roughness=0.8)
    leaf = C.material("palm_leaf", "#2f7a45", roughness=0.7)
    parts = [C.cylinder("Trunk", 0.16, 3.2, (0, 0, 1.6), trunk, verts=8)]
    for index in range(6):
        angle = index / 6.0 * math.tau
        parts.append(C.cube("Frond", None,
                            (math.cos(angle) * 0.9, math.sin(angle) * 0.9, 3.3),
                            leaf, bevel=0.01, scale=(1.6, 0.32, 0.06),
                            rot=(0, 0.45, angle)))
    return parts


def pond(stone, dark, accent, light):
    water = C.material("pond_water", "#2f7f8f", roughness=0.1, metallic=0.2)
    return [
        C.cylinder("Rim", 1.5, 0.18, (0, 0, 0.09), stone, verts=14),
        C.cylinder("Water", 1.35, 0.06, (0, 0, 0.2), water, verts=14),
    ]


def bench(stone, dark, accent, light):
    wood = C.material("bench_wood2", "#6b4a2c", roughness=0.7)
    parts = [C.cube("Seat", None, (0, 0, 0.46), wood, bevel=0.03, scale=(1.9, 0.52, 0.1))]
    parts.append(C.cube("Back", None, (0, 0.26, 0.78), wood, bevel=0.03, scale=(1.9, 0.09, 0.46)))
    for sign in (-1, 1):
        parts.append(C.cube("Leg", None, (sign * 0.78, 0, 0.23), dark, bevel=0.01, scale=(0.1, 0.46, 0.46)))
    return parts


# ---------------------------------------------------------------- Schlachthof

def hall(stone, dark, accent, light):
    parts = [C.cube("Body", None, (0, 0, 1.6), stone, bevel=0.02, scale=(3.0, 1.6, 3.2))]
    parts.append(C.cube("Roof", None, (0, 0, 3.35), dark, bevel=0.02, scale=(3.2, 1.8, 0.3)))
    for x in (-1.0, 0.0, 1.0):
        parts.append(C.cube("Window", None, (x, -1.62, 2.0), accent, bevel=0.01, scale=(0.55, 0.06, 0.5)))
    parts.append(C.cube("Door", None, (0, -1.63, 0.8), dark, bevel=0.01, scale=(0.9, 0.06, 1.4)))
    return parts


def boiler(stone, dark, accent, light):
    parts = [C.cylinder("Body", 1.0, 2.8, (0, 0, 1.4), stone, verts=14)]
    parts.append(C.sphere("Top", 1.0, (0, 0, 2.8), dark, segments=12, rings=8, scale=(1, 1, 0.4)))
    parts.append(C.cylinder("Pipe", 0.14, 1.4, (0.8, 0, 2.4), dark, verts=8, rot=(0, math.pi / 2, 0)))
    for z in (0.7, 1.5, 2.2):
        parts.append(C.cylinder("Band", 1.05, 0.08, (0, 0, z), accent, verts=14))
    return parts


def hook(stone, dark, accent, light):
    parts = [C.cube("Frame", None, (0, 0, 2.2), dark, bevel=0.02, scale=(1.4, 0.3, 0.25))]
    parts.append(C.cylinder("Chain", 0.05, 1.6, (0, 0, 1.35), stone, verts=6))
    parts.append(C.torus("Hook", 0.32, 0.08, (0, 0, 0.45), accent, rot=(math.pi / 2, 0, 0)))
    return parts


def chain(stone, dark, accent, light):
    parts = []
    for index in range(5):
        parts.append(C.torus("Link", 0.24, 0.06, (0, 0, 0.3 + index * 0.42), stone,
                             rot=(math.pi / 2 if index % 2 == 0 else 0, 0, 0)))
    return parts


def trough(stone, dark, accent, light):
    water = C.material("trough_water", "#5c2d2a", roughness=0.2)
    parts = [C.cube("Body", None, (0, 0, 0.45), stone, bevel=0.02, scale=(2.2, 1.0, 0.9))]
    parts.append(C.cube("Water", None, (0, 0, 0.86), water, bevel=0.01, scale=(1.9, 0.75, 0.06)))
    return parts


# ---------------------------------------------------------------- lamp (all kits)

def lamp(stone, dark, accent, light):
    return [
        C.cylinder("Pole", 0.08, 3.0, (0, 0, 1.5), dark, verts=8),
        C.cube("Head", None, (0, 0, 3.1), accent, bevel=0.02, scale=(0.35, 0.35, 0.3)),
        C.cube("Cap", None, (0, 0, 3.3), dark, bevel=0.02, scale=(0.45, 0.45, 0.08)),
    ]


KITS = {
    "Innenstadt": {
        "Facade": facade, "Arcade": arcade, "Column": column, "Gate": gate,
        "Planter": planter, "Lamp": lamp,
    },
    "Rheinufer": {
        "Railing": railing, "Kiosk": kiosk, "Ponton": ponton, "Crane": crane,
        "Bollard": bollard, "Lamp": lamp,
    },
    "Labor": {
        "LabWall": labwall, "Tank": tank, "Console": console, "Pipe": pipe,
        "Crate": crate, "Lamp": lamp,
    },
    "Neroberg": {
        "Pine": pine, "Monopteros": monopteros, "Rail": rail, "Funicular": funicular,
        "Vista": vista, "Lamp": lamp,
    },
    "Warmerdamm": {
        "Hedge": hedge, "Bandstand": bandstand, "Palm": palm, "Pond": pond,
        "Bench": bench, "Lamp": lamp,
    },
    "Schlachthof": {
        "Hall": hall, "Boiler": boiler, "Hook": hook, "Chain": chain,
        "Trough": trough, "Lamp": lamp,
    },
}


def build_kit(kit, budget):
    stone, dark, accent, light = mats(kit)
    for piece, builder in KITS[kit].items():
        C.reset_scene()
        # Materials are recreated after the scene reset, otherwise Blender hands
        # back datablocks that belonged to the previous kit.
        stone, dark, accent, light = mats(kit)
        parts = builder(stone, dark, accent, light)
        name = f"SM_{kit}_{piece}"
        mesh = C.join(parts, name)
        fbx = C.export_fbx(name, [mesh], os.path.join("Arena", kit))
        paths, sheet = C.render_views(f"kit_{kit.lower()}_{piece.lower()}", [mesh], 512)
        lo, hi = C._scene_bounds([mesh])
        entry = {
            "kit": kit,
            "piece": piece,
            "asset": name,
            "tris": len(mesh.data.polygons),
            "size": [round(hi.x - lo.x, 2), round(hi.y - lo.y, 2), round(hi.z - lo.z, 2)],
            "fbx": os.path.relpath(fbx, C.ROOT).replace("\\", "/"),
            "sheet": os.path.relpath(sheet, C.ROOT).replace("\\", "/"),
        }
        budget["pieces"].append(entry)
        print(f"[gen_arena_kits] {kit}/{piece}: tris={entry['tris']} size={entry['size']} -> {fbx}")


def main():
    args = C.parse_args()
    requested = args.get("kit", "all")
    kits = list(KITS) if requested == "all" else [requested]
    budget = {"pieces": []}
    for kit in kits:
        build_kit(kit, budget)

    out_dir = os.path.join(C.EXPORT, "Arena")
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "kit_budget.json"), "w", encoding="utf-8") as handle:
        json.dump(budget, handle, indent=2)
    print(f"[gen_arena_kits] {len(budget['pieces'])} modules -> {out_dir}/kit_budget.json")


if __name__ == "__main__":
    main()
