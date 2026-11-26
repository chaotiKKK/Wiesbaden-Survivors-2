"""Compile M_WS2VAT inside a real editor session, then quit.

The commandlet path saves the material without an SM5 shader map, so the game
falls back to the default material. Running this with the editor open builds the
shaders for the active (preview) platform and stores them in the asset.

UnrealEditor.exe <project> -ExecutePythonScript="Tools/compile_vat_in_editor.py" -windowed
"""
import unreal

material = unreal.EditorAssetLibrary.load_asset("/Game/Art/M_WS2VAT")
if material is None:
    unreal.log_error("[WS2 vat] M_WS2VAT missing")
else:
    ok = unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.log(f"[WS2 vat] recompile_material -> {ok}")
    unreal.EditorAssetLibrary.save_loaded_asset(material, False)
    unreal.log("[WS2 vat] material saved after editor compile")

unreal.log("[WS2 vat] quitting editor")
unreal.SystemLibrary.quit_editor()
