"""Headless Unreal import for the Blender export tree.

Run with:
  UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script="Tools/import_assets.py"

Imports every FBX below Art/Export into /Game/Art/<Group>/<Kit> and creates the
unlit colour material the blockout renderer expects.
"""
import json
import os

import unreal

PROJECT = unreal.Paths.project_dir()
PROJECT = os.path.abspath(os.path.join(PROJECT, "..")) if PROJECT.endswith("Binaries/") else os.path.abspath(PROJECT)
EXPORT_ROOT = os.path.join(PROJECT, "Art", "Export")

GROUPS = {
    "Heroes": "/Game/Art/Heroes",
    "Enemies": "/Game/Art/Enemies",
    "Bosses": "/Game/Art/Bosses",
    "Props": "/Game/Art/Props",
    "Arena": "/Game/Art/Arena",
}

report = {"imported": [], "failed": [], "material": None}


def log(message):
    unreal.log(f"[WS2 import] {message}")


def enable_instanced_usage(material):
    """Crowd rendering is instanced; without this flag UE swaps in the default material."""
    if material is None:
        return False
    try:
        material.set_editor_property("used_with_instanced_static_meshes", True)
    except Exception as error:  # noqa: BLE001
        log(f"instanced usage flag error: {error}")
        return False
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)
    return True


def ensure_material():
    path = "/Game/Art/M_WS2Color"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        material = unreal.EditorAssetLibrary.load_asset(path)
        if enable_instanced_usage(material):
            log(f"material exists: {path} (instanced usage flag set)")
        report["material"] = path
        return

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = tools.create_asset("M_WS2Color", "/Game/Art", unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        report["failed"].append("material")
        log("material creation failed")
        return

    try:
        expression = unreal.MaterialEditingLibrary.create_material_expression(
            material, unreal.MaterialExpressionVectorParameter, -520, 0)
        expression.set_editor_property("parameter_name", "Color")
        expression.set_editor_property("default_value", unreal.LinearColor(0.8, 0.8, 0.8, 1.0))
        unreal.MaterialEditingLibrary.connect_material_property(
            expression, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        enable_instanced_usage(material)
        unreal.MaterialEditingLibrary.recompile_material(material)
        unreal.EditorAssetLibrary.save_asset(path)
        report["material"] = path
        log(f"material created: {path}")
    except Exception as error:  # noqa: BLE001 - surface the reason, never hide it
        report["failed"].append(f"material: {error}")
        log(f"material setup error: {error}")


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
    # Chain LOD1_<Name>/LOD2_<Name>/... from the same FBX into one asset.
    options.static_mesh_import_data.import_mesh_lods = True
    options.minimum_lod_number = 0
    options.lod_number = 8
    options.auto_compute_lod_distances = True
    task.options = options

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    return task.imported_object_paths


def main():
    ensure_material()

    if not os.path.isdir(EXPORT_ROOT):
        log(f"no export tree at {EXPORT_ROOT}")
        return

    for group in sorted(os.listdir(EXPORT_ROOT)):
        group_path = os.path.join(EXPORT_ROOT, group)
        if not os.path.isdir(group_path):
            continue
        for root, _dirs, files in os.walk(group_path):
            for name in sorted(files):
                if not name.lower().endswith(".fbx"):
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

    output = os.path.join(PROJECT, "Saved", "WS2", "import_report.json")
    os.makedirs(os.path.dirname(output), exist_ok=True)
    with open(output, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2)
    log(f"done: {len(report['imported'])} imported, {len(report['failed'])} failed -> {output}")


main()
