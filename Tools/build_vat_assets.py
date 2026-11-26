"""Import the VAT textures, build /Game/Art/M_WS2VAT and chain enemy LODs.

UnrealEditor-Cmd.exe <project> -run=pythonscript -script="Tools/build_vat_assets.py"

The material decodes a vertex-animation texture (one row per frame, one column per
vertex) into WorldPositionOffset, so hundreds of instanced enemies animate on the
GPU with no AnimBP. Colour keeps a fake top-light via the vertex normal because the
slice renders unlit.
"""
import json
import os

import unreal

PROJECT = os.path.abspath(unreal.Paths.project_dir())
VAT_SOURCE = os.path.join(PROJECT, "Art", "Textures", "VAT")
MANIFEST = os.path.join(PROJECT, "Art", "Export", "Enemies", "Slice", "slice_manifest.json")
ALL_MANIFEST = os.path.join(PROJECT, "Art", "Export", "Enemies", "Slice", "enemies_all_manifest.json")
HERO_MANIFEST = os.path.join(PROJECT, "Art", "Export", "Heroes", "Slice", "hero_manifest.json")
BOSS_MANIFEST = os.path.join(PROJECT, "Art", "Export", "Bosses", "boss_manifest.json")
VAT_DEST = "/Game/Art/VAT"
MATERIAL_PATH = "/Game/Art/M_WS2VAT"
ARENA_ROOT = "/Game/Art/Arena"

# Plan v3 budgets. Low-poly assets are allowed to sit below the lower bound;
# only exceeding the triangle or data-texture ceiling is a validator warning.
BUDGETS = {
    "hero": {"maxTris": 12000, "minLods": 2, "expected": 1},
    "enemy": {"maxTris": 3000, "minLods": 2, "expected": 28},
    "boss": {"maxTris": 40000, "minLods": 2, "expected": 8},
    "kit": {"maxTris": 3000, "minLods": 2, "expected": 43},
}
VAT_LIMITS = {
    "hero": {"maxWidth": 4096, "maxHeight": 256},
    "enemy": {"maxWidth": 1024, "maxHeight": 256},
}

report = {"textures": [], "material": None, "lods": [], "budgets": [],
          "warnings": [], "failed": [], "summary": {}}


def log(message):
    unreal.log(f"[WS2 vat] {message}")


def import_textures():
    tasks = []
    for name in sorted(os.listdir(VAT_SOURCE)):
        if not name.lower().endswith(".png"):
            continue
        task = unreal.AssetImportTask()
        task.filename = os.path.join(VAT_SOURCE, name)
        task.destination_path = VAT_DEST
        task.automated = True
        task.save = True
        task.replace_existing = True
        tasks.append(task)
    if tasks:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    for task in tasks:
        path = f"{VAT_DEST}/{os.path.splitext(os.path.basename(task.filename))[0]}"
        texture = unreal.EditorAssetLibrary.load_asset(path)
        if texture is None:
            report["failed"].append(f"texture {path}")
            continue
        # Raw data, not colour: no sRGB, no DXT.
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings",
                                    unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
        unreal.EditorAssetLibrary.save_loaded_asset(texture)
        report["textures"].append(path)
        log(f"texture {path}")


def expression(material, cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(material, cls, x, y)


def link(source, source_output, target, target_input):
    ok = unreal.MaterialEditingLibrary.connect_material_expressions(
        source, source_output, target, target_input)
    if not ok:
        report["failed"].append(f"link {target.get_name()}.{target_input}")
    return ok


def constant(material, value, x, y):
    node = expression(material, unreal.MaterialExpressionConstant, x, y)
    node.set_editor_property("r", float(value))
    return node


def constant3(material, color, x, y):
    node = expression(material, unreal.MaterialExpressionConstant3Vector, x, y)
    node.set_editor_property("constant", color)
    return node


def constant2(material, u, v, x, y):
    """float2 constant - mixing float2 and float3 is a shader compile error."""
    node = expression(material, unreal.MaterialExpressionConstant2Vector, x, y)
    try:
        node.set_editor_property("r", float(u))
        node.set_editor_property("g", float(v))
    except Exception:  # noqa: BLE001 - older property spelling
        node.set_editor_property("R", float(u))
        node.set_editor_property("G", float(v))
    return node


def scalar_param(material, name, value, x, y):
    node = expression(material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", float(value))
    return node


def vector_param(material, name, color, x, y):
    node = expression(material, unreal.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", color)
    return node


def build_material():
    if unreal.EditorAssetLibrary.does_asset_exist(MATERIAL_PATH):
        material = unreal.EditorAssetLibrary.load_asset(MATERIAL_PATH)
    else:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "M_WS2VAT", "/Game/Art", unreal.Material, unreal.MaterialFactoryNew())

    # Wipe a previous graph so re-runs stay idempotent.
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)

    time_node = expression(material, unreal.MaterialExpressionTime, -1400, 0)
    fps = scalar_param(material, "VATFps", 24.0, -1400, 140)
    count = scalar_param(material, "VATCount", 16.0, -1400, 220)
    start = scalar_param(material, "VATStart", 0.0, -1400, 300)
    total = scalar_param(material, "VATFrames", 16.0, -1400, 380)
    randomize = scalar_param(material, "VATRandomize", 1.0, -1400, 460)

    mul_time = expression(material, unreal.MaterialExpressionMultiply, -1200, 20)
    link(time_node, "", mul_time, "A")
    link(fps, "", mul_time, "B")

    rnd = expression(material, unreal.MaterialExpressionPerInstanceRandom, -1400, 540)
    mul_rand_on = expression(material, unreal.MaterialExpressionMultiply, -1200, 520)
    link(rnd, "", mul_rand_on, "A")
    link(randomize, "", mul_rand_on, "B")
    mul_rand_count = expression(material, unreal.MaterialExpressionMultiply, -1000, 520)
    link(mul_rand_on, "", mul_rand_count, "A")
    link(count, "", mul_rand_count, "B")

    time_plus_rand = expression(material, unreal.MaterialExpressionAdd, -820, 180)
    link(mul_time, "", time_plus_rand, "A")
    link(mul_rand_count, "", time_plus_rand, "B")

    wrapped = expression(material, unreal.MaterialExpressionFmod, -640, 180)
    link(time_plus_rand, "", wrapped, "A")
    link(count, "", wrapped, "B")

    floored = expression(material, unreal.MaterialExpressionFloor, -480, 180)
    link(wrapped, "", floored, "")

    with_start = expression(material, unreal.MaterialExpressionAdd, -320, 180)
    link(floored, "", with_start, "A")
    link(start, "", with_start, "B")

    centre = expression(material, unreal.MaterialExpressionAdd, -160, 180)
    link(with_start, "", centre, "A")
    link(constant(material, 0.5, -160, 300), "", centre, "B")

    frame_v = expression(material, unreal.MaterialExpressionDivide, 0, 180)
    link(centre, "", frame_v, "A")
    link(total, "", frame_v, "B")

    # UV = texcoord * (1,0) + (0, frame); all terms stay float2.
    texcoord = expression(material, unreal.MaterialExpressionTextureCoordinate, -320, 420)
    keep_u = expression(material, unreal.MaterialExpressionMultiply, -160, 420)
    link(texcoord, "", keep_u, "A")
    link(constant2(material, 1.0, 0.0, -320, 540), "", keep_u, "B")
    frame_only = expression(material, unreal.MaterialExpressionMultiply, -160, 620)
    link(frame_v, "", frame_only, "A")
    link(constant2(material, 0.0, 1.0, -320, 720), "", frame_only, "B")
    uv = expression(material, unreal.MaterialExpressionAdd, 60, 460)
    link(keep_u, "", uv, "A")
    link(frame_only, "", uv, "B")

    sample = expression(material, unreal.MaterialExpressionTextureSampleParameter2D, 340, 180)
    sample.set_editor_property("parameter_name", "VAT")
    default_texture = unreal.EditorAssetLibrary.load_asset(f"{VAT_DEST}/SM_Runner_VAT")
    if default_texture:
        sample.set_editor_property("texture", default_texture)
    link(uv, "", sample, "UVs")

    centred = expression(material, unreal.MaterialExpressionSubtract, 560, 180)
    link(sample, "", centred, "A")
    link(constant3(material, unreal.LinearColor(0.5, 0.5, 0.5, 1.0), 560, 340), "", centred, "B")

    doubled = expression(material, unreal.MaterialExpressionMultiply, 720, 180)
    link(centred, "", doubled, "A")
    link(constant(material, 2.0, 720, 340), "", doubled, "B")

    extent = vector_param(material, "VATExtent", unreal.LinearColor(1.0, 1.0, 1.0, 1.0), 720, 420)
    offset = expression(material, unreal.MaterialExpressionMultiply, 900, 180)
    link(doubled, "", offset, "A")
    link(extent, "", offset, "B")
    if not unreal.MaterialEditingLibrary.connect_material_property(
            offset, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET):
        report["failed"].append("world position offset")

    # Fake top light: unlit shading would otherwise render the archetypes flat.
    normal = expression(material, unreal.MaterialExpressionVertexNormalWS, 900, 420)
    up = constant3(material, unreal.LinearColor(0.0, 0.0, 1.0, 1.0), 900, 540)
    dot = expression(material, unreal.MaterialExpressionDotProduct, 1060, 420)
    link(normal, "", dot, "A")
    link(up, "", dot, "B")
    sat = expression(material, unreal.MaterialExpressionSaturate, 1180, 420)
    link(dot, "", sat, "")
    shade_scale = expression(material, unreal.MaterialExpressionMultiply, 1300, 420)
    link(sat, "", shade_scale, "A")
    link(constant(material, 0.55, 1300, 560), "", shade_scale, "B")
    shade = expression(material, unreal.MaterialExpressionAdd, 1440, 420)
    link(shade_scale, "", shade, "A")
    link(constant(material, 0.45, 1440, 560), "", shade, "B")

    color = vector_param(material, "Color", unreal.LinearColor(0.8, 0.8, 0.8, 1.0), 1440, 640)
    emissive = expression(material, unreal.MaterialExpressionMultiply, 1620, 500)
    link(color, "", emissive, "A")
    link(shade, "", emissive, "B")
    if not unreal.MaterialEditingLibrary.connect_material_property(
            emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        report["failed"].append("emissive")

    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)
    report["material"] = MATERIAL_PATH
    log(f"material built: {MATERIAL_PATH}")


def load_mesh(package_path):
    """Assets imported in this same commandlet need a registry refresh first."""
    asset = unreal.EditorAssetLibrary.load_asset(package_path)
    if asset is not None:
        return asset
    try:
        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        registry.scan_paths_synchronous([package_path.rsplit("/", 1)[0]], True)
    except Exception as error:  # noqa: BLE001
        log(f"registry scan failed: {error}")
    asset = unreal.EditorAssetLibrary.load_asset(package_path)
    if asset is not None:
        return asset
    return unreal.EditorAssetLibrary.load_asset(f"{package_path}.{package_path.rsplit('/', 1)[-1]}")


def asset_path_from_export(export):
    relative = export.replace("\\", "/").split("Export/")[-1]
    return "/Game/Art/" + relative.rsplit(".", 1)[0]


def read_manifest(path, key):
    with open(path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    return data[key] if key in data else [data]


def texture_size(asset_key):
    texture = unreal.EditorAssetLibrary.load_asset(f"{VAT_DEST}/{asset_key}_VAT")
    if not isinstance(texture, unreal.Texture2D):
        return None
    try:
        return [texture.blueprint_get_size_x(), texture.blueprint_get_size_y()]
    except Exception:  # noqa: BLE001 - older API spelling
        return None


def inspect_mesh(path, group, label):
    budget = BUDGETS[group]
    mesh = load_mesh(path)
    if not isinstance(mesh, unreal.StaticMesh):
        report["failed"].append(f"{group} mesh missing: {path}")
        return

    lods = mesh.get_num_lods()
    tris = [mesh.get_num_triangles(level) for level in range(lods)]
    slots = len(mesh.get_editor_property("static_materials"))
    record = {
        "label": label,
        "group": group,
        "mesh": path,
        "lodCount": lods,
        "tris": tris,
        "baseTris": tris[0] if tris else 0,
        "materialSlots": slots,
        "budget": budget,
    }
    warnings = []
    if lods < budget["minLods"]:
        warnings.append(f"{label}: only {lods} LODs (need >= {budget['minLods']})")
    if tris and tris[0] > budget["maxTris"]:
        warnings.append(f"{label}: {tris[0]} tris > {budget['maxTris']} budget")
    if group in VAT_LIMITS:
        size = texture_size(path.rsplit("/", 1)[-1])
        limit = VAT_LIMITS[group]
        record["vatTexture"] = size
        record["vatLimits"] = limit
        if size and (size[0] > limit["maxWidth"] or size[1] > limit["maxHeight"]):
            warnings.append(f"{label}: VAT {size[0]}x{size[1]} exceeds "
                            f"{limit['maxWidth']}x{limit['maxHeight']}")
    record["warnings"] = warnings
    report["lods"].append(record)
    report["budgets"].append({"label": label, "group": group,
                              "baseTris": record["baseTris"],
                              "maxTris": budget["maxTris"],
                              "budgetOk": record["baseTris"] <= budget["maxTris"],
                              "lodCount": lods})
    for warning in warnings:
        report["warnings"].append(warning)
        log(f"WARNING {warning}")
    log(f"mesh {path}: lods={lods} tris={tris} slots={slots}")


def validate_content():
    hero = read_manifest(HERO_MANIFEST, "hero")[0]
    inspect_mesh(asset_path_from_export(hero["export"]), "hero", hero.get("asset", "hero"))

    enemies = read_manifest(ALL_MANIFEST, "enemies")
    for entry in enemies:
        exports = entry.get("exports") or []
        if not exports:
            report["failed"].append(f"enemy without export: {entry.get('asset')}")
            continue
        inspect_mesh(asset_path_from_export(exports[0]), "enemy", entry.get("asset", "enemy"))

    bosses = read_manifest(BOSS_MANIFEST, "bosses")
    for entry in bosses:
        inspect_mesh(asset_path_from_export(entry["fbx"]), "boss", entry.get("asset", "boss"))

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    kit_assets = []
    for data in registry.get_assets_by_path(ARENA_ROOT, recursive=True):
        name = str(data.asset_name)
        if "_LOD" in name:
            continue
        kit_assets.append(data)
    for data in sorted(kit_assets, key=lambda item: str(item.package_name)):
        inspect_mesh(str(data.package_name), "kit", str(data.asset_name))

    counts = {"hero": 1, "enemy": len(enemies), "boss": len(bosses), "kit": len(kit_assets)}
    for group, count in counts.items():
        expected = BUDGETS[group]["expected"]
        if count != expected:
            report["warnings"].append(f"{group}: expected {expected} assets, found {count}")

    report["summary"] = {
        "assets": sum(counts.values()),
        "counts": counts,
        "lods": len(report["lods"]),
        "warnings": len(report["warnings"]),
        "failed": len(report["failed"]),
        "budgets": BUDGETS,
    }
    # Keep the older LOD report path in sync: it used to be a failed attach
    # attempt, and a stale warning list would contradict vat_report.json.
    lod_report = os.path.join(PROJECT, "Saved", "WS2", "lod_report.json")
    with open(lod_report, "w", encoding="utf-8") as handle:
        json.dump({"assets": report["lods"], "failed": report["failed"],
                   "warnings": report["warnings"], "source": "vat_report.json"},
                  handle, indent=2)
    log(f"content validated: {report['summary']}")


def write_runtime_vat_data():
    """Ship the atlas metadata the runtime needs (bounds, frames, fps)."""
    entries = {}
    for name in sorted(os.listdir(VAT_SOURCE)):
        if not name.lower().endswith(".json"):
            continue
        with open(os.path.join(VAT_SOURCE, name), "r", encoding="utf-8") as handle:
            meta = json.load(handle)
        entries[meta["asset"]] = {
            "frames": meta["frames"],
            "fps": meta["fps"],
            "atlasWidth": meta["atlasWidth"],
            "vertexCounts": meta.get("vertexCounts", []),
            "extentMeters": meta["extent"],
            "extentUnits": [value * 100.0 for value in meta["extent"]],
            "clips": meta.get("clips", {}),
        }
    out = os.path.join(PROJECT, "Content", "Data", "vat.json")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8") as handle:
        json.dump(entries, handle, indent=2)
    report["vatData"] = out
    log(f"vat.json written with {len(entries)} atlases")


def main():
    import_textures()
    build_material()
    write_runtime_vat_data()
    try:
        validate_content()
    except Exception as error:  # noqa: BLE001 - keep the texture/material result
        report["failed"].append(f"validate_content: {error}")
        log(f"validate_content error: {error}")
    output = os.path.join(PROJECT, "Saved", "WS2", "vat_report.json")
    os.makedirs(os.path.dirname(output), exist_ok=True)
    with open(output, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2)
    log(f"done: textures={len(report['textures'])} lods={len(report['lods'])} "
        f"warnings={len(report['warnings'])} failed={len(report['failed'])} -> {output}")


main()
