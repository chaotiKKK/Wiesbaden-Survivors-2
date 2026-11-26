"""Import combined LOD-chain FBXs into the final hero, boss and kit asset paths.

UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script="Tools/import_lod_chains.py"
"""
import json
import os

import unreal

PROJECT = os.path.abspath(unreal.Paths.project_dir())
CHAIN_ROOT = os.path.join(PROJECT, "Art", "Export", "LODChains")
report = {"imported": [], "failed": []}


def log(message):
    unreal.log(f"[WS2 chain] {message}")


def destination_for(relative):
    parts = relative.split("/")
    if parts[0] == "Heroes":
        return "/Game/Art/Heroes/Slice"
    if parts[0] == "Bosses":
        return "/Game/Art/Bosses"
    if parts[0] == "Arena" and len(parts) >= 3:
        return "/Game/Art/Arena/" + parts[1]
    return None


def import_fbx(path, destination):
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = destination
    task.automated = True
    task.save = True
    task.replace_existing = True

    options = unreal.FbxImportUI()
    options.import_mesh = True
    options.import_as_skeletal = False
    options.import_animations = False
    options.import_materials = False
    options.import_textures = False
    options.static_mesh_import_data.combine_meshes = True
    options.static_mesh_import_data.generate_lightmap_u_vs = True
    options.static_mesh_import_data.import_mesh_lods = True
    options.minimum_lod_number = 0
    options.lod_number = 8
    options.auto_compute_lod_distances = True
    task.options = options
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    return list(task.imported_object_paths)


def main():
    for root, _dirs, files in os.walk(CHAIN_ROOT):
        for name in sorted(files):
            if not name.lower().endswith(".fbx"):
                continue
            source = os.path.join(root, name)
            relative = os.path.relpath(source, CHAIN_ROOT).replace("\\", "/")
            destination = destination_for(relative)
            if not destination:
                report["failed"].append(f"no destination for {relative}")
                continue
            try:
                imported = import_fbx(source, destination)
                report["imported"].append({"source": relative, "destination": destination,
                                           "objects": list(imported)})
                log(f"imported {relative} -> {destination}")
            except Exception as error:  # noqa: BLE001
                report["failed"].append(f"{relative}: {error}")
                log(f"FAILED {relative}: {error}")

    output = os.path.join(PROJECT, "Saved", "WS2", "lod_chain_import_report.json")
    os.makedirs(os.path.dirname(output), exist_ok=True)
    with open(output, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2)
    log(f"done: {len(report['imported'])} imported, {len(report['failed'])} failed -> {output}")


main()
