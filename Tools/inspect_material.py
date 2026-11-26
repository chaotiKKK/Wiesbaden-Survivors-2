"""Print how /Game/Art/M_WS2Color is wired, so render issues are diagnosable.

UnrealEditor-Cmd.exe <project> -run=pythonscript -script="Tools/inspect_material.py" -unattended -nop4 -nosplash -NullRHI
"""
import json

import unreal

PATH = "/Game/Art/M_WS2Color"
report = {}


def describe(node):
    if node is None:
        return None
    info = {"class": type(node).__name__}
    for prop in ("parameter_name", "default_value", "r", "g", "b"):
        try:
            info[prop] = str(node.get_editor_property(prop))
        except Exception:  # noqa: BLE001
            pass
    return info


def main():
    material = unreal.EditorAssetLibrary.load_asset(PATH)
    if material is None:
        report["error"] = "material missing"
    else:
        report["shading_model"] = str(material.get_editor_property("shading_model"))
        report["two_sided"] = str(material.get_editor_property("two_sided"))
        report["blend_mode"] = str(material.get_editor_property("blend_mode"))
        library = unreal.MaterialEditingLibrary
        for prop_name in ("MP_EMISSIVE_COLOR", "MP_BASE_COLOR", "MP_ROUGHNESS"):
            prop = getattr(unreal.MaterialProperty, prop_name)
            report[prop_name] = {
                "input": describe(library.get_material_property_input_node(material, prop)),
                "has_input": bool(library.has_material_property_input(material, prop))
                if hasattr(library, "has_material_property_input") else "unknown",
            }
        report["expressions"] = [describe(e) for e in library.get_material_expressions(material)]

    output = f"{unreal.Paths.project_dir()}Saved/WS2/material_report.json"
    with open(output, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2)
    unreal.log(f"[WS2 material] {json.dumps(report)}")


main()
