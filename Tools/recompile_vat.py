"""Force a platform compile of M_WS2VAT so the shader error reaches the log."""
import unreal

path = "/Game/Art/M_WS2VAT"
material = unreal.EditorAssetLibrary.load_asset(path)
if material is None:
    unreal.log_error(f"[WS2 vat] missing {path}")
else:
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.log("[WS2 vat] recompile triggered")
    unreal.EditorAssetLibrary.save_loaded_asset(material)
