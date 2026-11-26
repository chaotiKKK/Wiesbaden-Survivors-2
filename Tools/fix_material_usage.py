"""Enable the InstancedStaticMeshes usage flag on the blockout material.

UE silently substitutes the default material for an instanced mesh when the material
does not carry that usage flag, which is why the horde rendered grey.

UnrealEditor-Cmd.exe <project> -run=pythonscript -script="Tools/fix_material_usage.py" -unattended -nop4 -nosplash -NullRHI
"""
import unreal

PATH = "/Game/Art/M_WS2Color"

material = unreal.EditorAssetLibrary.load_asset(PATH)
if material is None:
    raise RuntimeError(f"missing {PATH}")

material.set_editor_property("used_with_instanced_static_meshes", True)
unreal.MaterialEditingLibrary.recompile_material(material)
unreal.EditorAssetLibrary.save_loaded_asset(material)

enabled = bool(material.get_editor_property("used_with_instanced_static_meshes"))
print(f"[WS2 material] instanced usage flag = {enabled}")
if not enabled:
    raise RuntimeError("usage flag did not stick")
