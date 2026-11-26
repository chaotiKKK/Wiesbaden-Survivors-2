"""Probe the FBX import options for LOD-related switches."""
import json
import os

import unreal

ui = unreal.FbxImportUI()
data = unreal.FbxStaticMeshImportData()
report = {
    "ui_attrs": [a for a in dir(ui) if "lod" in a.lower()],
    "data_attrs": [a for a in dir(data) if "lod" in a.lower()],
}

out = os.path.join(unreal.Paths.project_dir(), "Saved", "WS2", "fbx_probe.json")
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, "w", encoding="utf-8") as handle:
    json.dump(report, handle, indent=2)
unreal.log(f"[WS2 probe] {out}")
