"""List assets under the slice folders and dump them to Saved/WS2/asset_list.json."""
import json
import os

import unreal

report = {}
for path in ("/Game/Art/Enemies/Slice", "/Game/Art/Heroes/Slice", "/Game/Art/VAT",
             "/Game/Art/Arena/Kurpark", "/Game/Art/Enemies"):
    report[path] = [str(a) for a in unreal.EditorAssetLibrary.list_assets(path, recursive=True,
                                                                        include_folder=False)]

mesh = unreal.EditorAssetLibrary.load_asset("/Game/Art/Enemies/Slice/SM_Runner_LOD1.SM_Runner_LOD1")
report["direct_lod1"] = str(mesh)

out = os.path.join(unreal.Paths.project_dir(), "Saved", "WS2", "asset_list.json")
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, "w", encoding="utf-8") as handle:
    json.dump(report, handle, indent=2)
unreal.log(f"[WS2 list] {out}")
