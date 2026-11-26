"""Headless verification of the imported blockout assets.

UnrealEditor-Cmd.exe <project> -run=pythonscript -script="Tools/verify_assets.py" -unattended -nopriv -nosplash -NullRHI
"""
import json

import unreal

PROJECT = unreal.Paths.project_dir()
report = {"material": {}, "meshes": []}


def verify_material():
    path = "/Game/Art/M_WS2Color"
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        report["material"] = {"exists": False}
        return
    material = unreal.EditorAssetLibrary.load_asset(path)
    report["material"] = {
        "exists": True,
        "shading_model": str(material.get_editor_property("shading_model")),
        "expressions": [str(type(e).__name__) for e in unreal.MaterialEditingLibrary.get_material_expressions(material)],
        "scalar_parameters": [str(p.get_editor_property("parameter_name"))
                              for p in unreal.MaterialEditingLibrary.get_material_expressions(material)
                              if type(p).__name__ == "MaterialExpressionVectorParameter"],
    }


def verify_meshes():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = registry.get_assets_by_path("/Game/Art", recursive=True)
    for asset in assets:
        if str(asset.asset_class_path.asset_name) not in ("StaticMesh",):
            continue
        mesh = unreal.EditorAssetLibrary.load_asset(str(asset.package_name))
        if mesh is None:
            continue
        bounds = mesh.get_bounds()
        report["meshes"].append({
            "path": str(asset.package_name),
            "triangles": mesh.get_num_triangles(0),
            "lod_count": mesh.get_num_lods(),
            "extent": [round(bounds.box_extent.x, 2), round(bounds.box_extent.y, 2), round(bounds.box_extent.z, 2)],
        })


def main():
    verify_material()
    verify_meshes()
    output = f"{PROJECT}Saved/WS2/asset_report.json"
    with open(output, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2)
    unreal.log(f"[WS2 verify] material={report['material']} meshes={len(report['meshes'])} -> {output}")


main()
