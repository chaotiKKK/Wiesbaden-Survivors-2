"""Import only the generated *_LOD*.fbx files into the matching UE folders.

UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script="Tools/import_lods.py"
"""
import json
import os

import unreal

PROJECT = os.path.abspath(unreal.Paths.project_dir())
EXPORT_ROOT = os.path.join(PROJECT, "Art", "Export")
report = {"imported": [], "failed": []}


def log(message):
    unreal.log(f"[WS2 lods] {message}")


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
    options.static_mesh_import_data.import_mesh_lods = False
    task.options = options
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    return list(task.imported_object_paths)


def main():
    for root, _dirs, files in os.walk(EXPORT_ROOT):
        for name in sorted(files):
            if not name.lower().endswith(".fbx") or "_lod" not in name.lower():
                continue
            source = os.path.join(root, name)
            relative = os.path.relpath(root, EXPORT_ROOT).replace("\\", "/")
            destination = "/Game/Art/" + relative
            try:
                imported = import_fbx(source, destination)
                report["imported"].append({"source": os.path.relpath(source, PROJECT).replace("\\", "/"),
                                           "destination": destination, "objects": list(imported)})
                log(f"imported {name} -> {destination}")
            except Exception as error:  # noqa: BLE001
                report["failed"].append(f"{name}: {error}")
                log(f"FAILED {name}: {error}")

    output = os.path.join(PROJECT, "Saved", "WS2", "lod_import_report.json")
    os.makedirs(os.path.dirname(output), exist_ok=True)
    with open(output, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2)
    log(f"done: {len(report['imported'])} imported, {len(report['failed'])} failed -> {output}")


main()
