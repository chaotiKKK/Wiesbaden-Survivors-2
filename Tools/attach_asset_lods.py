"""Attach generated LOD meshes to the content assets and write the budget report.

UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script="Tools/attach_asset_lods.py"
"""
import json
import os

import unreal

PROJECT = os.path.abspath(unreal.Paths.project_dir())
EXPORT = os.path.join(PROJECT, "Art", "Export")
REPORT = os.path.join(PROJECT, "Saved", "WS2", "lod_report.json")
BUDGETS = {"hero": 12000, "enemy": 3000, "boss": 40000, "kit": 3000}

report = {"assets": [], "failed": [], "warnings": []}


def log(message):
    unreal.log(f"[WS2 lodattach] {message}")


def load(path):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        asset = unreal.EditorAssetLibrary.load_asset(f"{path}.{path.rsplit('/', 1)[-1]}")
    return asset


def attach(kind, base_path, source_paths):
    base = load(base_path)
    if not isinstance(base, unreal.StaticMesh):
        report["failed"].append(f"missing base: {base_path}")
        return
    for level, source_path in enumerate(source_paths, start=1):
        source = load(source_path)
        if not isinstance(source, unreal.StaticMesh):
            report["failed"].append(f"missing LOD{level}: {source_path}")
            continue
        before = base.get_num_lods()
        try:
            result = unreal.EditorStaticMeshLibrary.set_lod_from_static_mesh(
                base, level, source, 0, True)
        except Exception as error:  # noqa: BLE001
            report["failed"].append(f"{base_path} LOD{level}: {error}")
            continue
        if not result:
            report["failed"].append(f"{base_path} LOD{level}: API returned 0")
        log(f"{base_path} LOD{level} from {source_path} (before={before}, after={base.get_num_lods()})")
    unreal.EditorAssetLibrary.save_loaded_asset(base)
    lods = base.get_num_lods()
    tris = []
    for level in range(lods):
        try:
            tris.append(base.get_num_triangles(level))
        except Exception:  # noqa: BLE001
            tris.append(0)
    budget = BUDGETS.get(kind, 0)
    entry = {
        "kind": kind,
        "path": base_path,
        "lodCount": lods,
        "tris": tris,
        "budget": budget,
        "budgetOk": bool(tris) and tris[0] <= budget,
    }
    if entry["lodCount"] < 4:
        report["warnings"].append(f"{base_path}: only {lods} LODs")
    report["assets"].append(entry)


def main():
    # Hero: base is under Slice, LODs were exported into the Heroes root.
    attach("hero", "/Game/Art/Heroes/Slice/SM_Leonidas",
           [f"/Game/Art/Heroes/SM_Leonidas_LOD{level}" for level in (1, 2, 3)])

    with open(os.path.join(EXPORT, "Bosses", "boss_manifest.json"), "r", encoding="utf-8") as handle:
        bosses = json.load(handle)["bosses"]
    for boss in bosses:
        asset = boss["asset"]
        attach("boss", f"/Game/Art/Bosses/{asset}",
               [f"/Game/Art/Bosses/{asset}_LOD{level}" for level in (1, 2, 3)])

    # Arena kits: one folder per kit, one base FBX per module.
    arena_root = os.path.join(EXPORT, "Arena")
    for kit in sorted(os.listdir(arena_root)):
        kit_dir = os.path.join(arena_root, kit)
        if not os.path.isdir(kit_dir):
            continue
        for name in sorted(os.listdir(kit_dir)):
            if not name.lower().endswith(".fbx") or "_lod" in name.lower():
                continue
            asset = os.path.splitext(name)[0]
            attach("kit", f"/Game/Art/Arena/{kit}/{asset}",
                   [f"/Game/Art/Arena/{kit}/{asset}_LOD{level}" for level in (1, 2, 3)])

    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    with open(REPORT, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2)
    log(f"done: {len(report['assets'])} assets, {len(report['failed'])} failed, "
        f"{len(report['warnings'])} warnings -> {REPORT}")


main()
