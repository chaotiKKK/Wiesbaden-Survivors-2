"""Create the empty slice level headlessly.

The level is only a container: arena geometry, pawns, camera and HUD are all
spawned at runtime by AWS2GameMode.
"""
import unreal

PATH = "/Game/Maps/L_Arena"


def main():
    if unreal.EditorAssetLibrary.does_asset_exist(PATH):
        unreal.log(f"[WS2 map] exists: {PATH}")
        return

    subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    try:
        subsystem.new_level(PATH)
        subsystem.save_current_level()
        unreal.log(f"[WS2 map] created: {PATH}")
    except Exception as error:  # noqa: BLE001
        unreal.log_error(f"[WS2 map] failed: {error}")


main()
