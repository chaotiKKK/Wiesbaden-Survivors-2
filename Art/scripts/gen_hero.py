"""Hero generator.  blender -b -P Art/scripts/gen_hero.py -- --hero leonidas"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy

import lib_body
import lib_head
import ws2_common as C


def main():
    args = C.parse_args()
    hero_id = args.get("hero", "leonidas")

    heroes = C.load_data("characters.json") or []
    hero = next((h for h in heroes if h.get("id") == hero_id), None)
    if hero is None:
        hero = {
            "id": hero_id,
            "name": "Leonidas",
            "skin": {"build": "kid", "chest": "#ffcf4a", "head": "#f2d5b0", "hat": "fire", "prop": "guitar"},
        }

    skin = hero.get("skin", {}) or {}
    C.reset_scene()

    parts, head_z, head_r, height, m_skin, m_accent = lib_body.build_body(
        skin.get("build", "normal"),
        skin.get("chest", "#ffcf4a"),
        "#e2564a",
        skin.get("head", "#f2d5b0"),
    )

    m_hair = C.material("hair", "#2a1a12", roughness=0.75)
    parts += lib_head.build_head(head_z, head_r, m_skin, m_hair)

    m_hat = C.material("hat_primary", skin.get("chest", "#ffcf4a"), metallic=0.2, roughness=0.4)
    m_hat_accent = C.material("hat_accent", "#e2564a", metallic=0.35, roughness=0.35)
    parts += lib_head.build_hat(skin.get("hat", "fire"), head_z, head_r, m_hat, m_hat_accent)

    m_prop = C.material("prop_wood", "#8a5a2a", roughness=0.6)
    parts += lib_body.build_prop(skin.get("prop", "none"), 0.55 * height / 1.78,
                                 height * 0.46, m_prop, m_hat_accent, height)

    asset_name = (hero.get("name") or hero_id).replace(".", "").replace(" ", "")
    mesh = C.join(parts, f"SM_{asset_name}")

    rig = C.build_rig(asset_name, height, broad=1.0)
    C.rigid_skin(mesh, rig)

    C.save_blend(f"hero_{hero_id}")
    fbx = C.export_fbx(f"SM_{asset_name}", [mesh, rig], "Heroes")
    paths, sheet = C.render_views(f"hero_{hero_id}", [mesh], 640)

    print(f"[gen_hero] hero={hero_id} asset=SM_{asset_name} tris={len(mesh.data.polygons)} "
          f"verts={len(mesh.data.vertices)} fbx={fbx} sheet={sheet}")


main()
