"""Report bounds, LODs and material slots for slice meshes plus the VAT material.

UnrealEditor-Cmd.exe <project> -run=pythonscript -script="Tools/probe_assets.py"
"""
import json
import os

import unreal

PATHS = [
    "/Game/Art/Enemies/Slice/SM_Runner",
    "/Game/Art/Enemies/Slice/SM_Walker",
    "/Game/Art/Enemies/Slice/SM_Swarm",
    "/Game/Art/Enemies/Slice/SM_Tank",
    "/Game/Art/Heroes/Slice/SM_Leonidas",
    "/Game/Art/Arena/Kurpark/SM_Kurpark_Tree",
    "/Game/Art/Arena/Kurpark/SM_Kurpark_Pavilion",
    "/Game/Art/Arena/Kurpark/SM_Kurpark_Hedge",
]

report = {"meshes": [], "material": {}}

for path in PATHS:
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        report["meshes"].append({"path": path, "missing": True})
        continue
    bounds = asset.get_bounds()
    slots = []
    for index in range(asset.get_num_sections(0)):
        material = asset.get_material(index)
        slots.append(material.get_path_name() if material else "none")
    report["meshes"].append({
        "path": path,
        "extent": [bounds.box_extent.x, bounds.box_extent.y, bounds.box_extent.z],
        "origin": [bounds.origin.x, bounds.origin.y, bounds.origin.z],
        "lods": asset.get_num_lods(),
        "sections": asset.get_num_sections(0),
        "materials": slots,
        "triangles": [asset.get_num_triangles(lod) for lod in range(asset.get_num_lods())],
    })

material = unreal.EditorAssetLibrary.load_asset("/Game/Art/M_WS2VAT")
if material:
    report["material"] = {
        "path": material.get_path_name(),
        "scalar": [str(p) for p in unreal.MaterialEditingLibrary.get_scalar_parameter_names(material)],
        "vector": [str(p) for p in unreal.MaterialEditingLibrary.get_vector_parameter_names(material)],
        "instanced": material.get_editor_property("used_with_instanced_static_meshes"),
        "shading": str(material.get_editor_property("shading_model")),
    }

out = os.path.join(unreal.Paths.project_saved_dir(), "WS2", "asset_probe.json")
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, "w", encoding="utf-8") as handle:
    json.dump(report, handle, indent=2)
unreal.log(f"[WS2 probe] written {out}")
