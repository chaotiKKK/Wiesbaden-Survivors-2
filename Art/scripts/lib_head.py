"""Head, hair and headgear modules. Silhouette-first: hats read from above."""
import math

import bpy

from ws2_common import cone, cube, cylinder, material, sphere, torus


def build_head(center_z, radius, skin_mat, hair_mat, scale=1.0):
    parts = []
    parts.append(sphere("Head", radius, (0.0, 0.0, center_z), skin_mat, segments=18, rings=12,
                        scale=(1.0 * scale, 0.92 * scale, 1.05 * scale)))
    parts.append(sphere("Nose", radius * 0.18, (0.0, -radius * 0.95, center_z - radius * 0.05), skin_mat,
                        segments=10, rings=6))
    parts.append(sphere("Hair", radius * 1.02, (0.0, radius * 0.10, center_z + radius * 0.18), hair_mat,
                        segments=16, rings=10, scale=(1.0, 1.0, 0.8)))
    return parts


def build_hat(kind, center_z, radius, primary_mat, accent_mat):
    """Return the headgear meshes for the hero's `skin.hat` id."""
    parts = []
    top = center_z + radius * 0.72

    if kind == "fire":
        parts.append(sphere("HelmetDome", radius * 0.92, (0.0, 0.0, top), primary_mat,
                            segments=16, rings=10, scale=(1.05, 1.0, 0.72)))
        parts.append(cylinder("HelmetBrim", radius * 1.18, radius * 0.14, (0.0, 0.0, top - radius * 0.16),
                              accent_mat, verts=18))
        parts.append(cube("HelmetCrest", None, (0.0, radius * 0.1, top + radius * 0.42), accent_mat,
                          bevel=0.01, scale=(radius * 0.16, radius * 0.7, radius * 0.22)))
    elif kind in ("pony", "lockenzopf"):
        parts.append(sphere("HairBack", radius * 0.86, (0.0, radius * 0.72, center_z), primary_mat,
                            segments=14, rings=8, scale=(0.9, 1.0, 1.1)))
        if kind == "lockenzopf":
            parts.append(cylinder("Braid", radius * 0.22, radius * 1.1, (radius * 0.2, radius * 0.95, center_z - radius * 1.0),
                                  primary_mat, verts=10, rot=(0.2, 0.0, 0.0)))
    elif kind == "goggles":
        parts.append(torus("GoggleStrap", radius * 1.0, radius * 0.08, (0.0, 0.0, center_z + radius * 0.2),
                           accent_mat, rot=(1.2, 0.0, 0.0)))
        for sign in (-1, 1):
            parts.append(cylinder("GoggleLens", radius * 0.22, radius * 0.14,
                                  (sign * radius * 0.35, -radius * 0.85, center_z + radius * 0.18),
                                  accent_mat, verts=12, rot=(1.5708, 0.0, 0.0)))
    elif kind == "hood":
        parts.append(cone("Hood", radius * 1.18, radius * 1.25, (0.0, radius * 0.1, center_z + radius * 0.5),
                          primary_mat, verts=14))
    elif kind == "headphones":
        parts.append(torus("Headband", radius * 1.05, radius * 0.07, (0.0, 0.0, center_z + radius * 0.25),
                           accent_mat, rot=(0.0, 1.5708, 0.0)))
        for sign in (-1, 1):
            parts.append(cylinder("EarCup", radius * 0.3, radius * 0.16,
                                  (sign * radius * 1.0, 0.0, center_z + radius * 0.05),
                                  accent_mat, verts=14, rot=(0.0, 1.5708, 0.0)))
    elif kind == "spiky":
        parts.append(sphere("HairBase", radius * 1.0, (0.0, 0.0, center_z + radius * 0.15), primary_mat,
                            segments=14, rings=8, scale=(1.0, 1.0, 0.7)))
        for index in range(5):
            angle = index / 5.0 * math.tau
            parts.append(cone("Spike", radius * 0.22, radius * 0.7,
                              (radius * 0.55 * math.cos(angle), radius * 0.55 * math.sin(angle),
                               center_z + radius * 0.55),
                              primary_mat, verts=6))
    elif kind == "cap":
        parts.append(sphere("CapDome", radius * 0.98, (0.0, 0.0, center_z + radius * 0.2), primary_mat,
                            segments=14, rings=8, scale=(1.0, 1.0, 0.55)))
        parts.append(cube("CapBrim", None, (0.0, -radius * 0.95, center_z + radius * 0.1), primary_mat,
                          bevel=0.01, scale=(radius * 0.9, radius * 0.55, radius * 0.06)))
    elif kind == "hard":
        parts.append(sphere("HardHat", radius * 0.95, (0.0, 0.0, center_z + radius * 0.3), primary_mat,
                            segments=14, rings=8, scale=(1.05, 1.0, 0.8)))
        parts.append(cylinder("HardHatBrim", radius * 1.12, radius * 0.08, (0.0, 0.0, center_z + radius * 0.12),
                              primary_mat, verts=16))
    elif kind == "antenna":
        parts.append(cylinder("AntennaRod", radius * 0.05, radius * 1.4, (radius * 0.4, 0.0, center_z + radius * 1.0),
                              accent_mat, verts=6))
        parts.append(sphere("AntennaTip", radius * 0.12, (radius * 0.4, 0.0, center_z + radius * 1.7),
                            accent_mat, segments=10, rings=6))
    elif kind == "rock":
        parts.append(cone("RockHair", radius * 1.05, radius * 0.55, (0.0, 0.0, center_z + radius * 0.55),
                          primary_mat, verts=10))
        parts.append(sphere("RockBack", radius * 0.7, (0.0, radius * 0.75, center_z - radius * 0.1),
                            primary_mat, segments=12, rings=8))
    elif kind == "beret":
        parts.append(sphere("Beret", radius * 1.05, (radius * 0.1, 0.0, center_z + radius * 0.5), primary_mat,
                            segments=14, rings=8, scale=(1.1, 1.0, 0.35)))
    elif kind == "flatcap":
        parts.append(sphere("FlatCap", radius * 0.98, (0.0, -radius * 0.1, center_z + radius * 0.32), primary_mat,
                            segments=14, rings=8, scale=(1.05, 1.1, 0.5)))
        parts.append(cube("FlatCapBrim", None, (0.0, -radius * 1.0, center_z + radius * 0.1), primary_mat,
                          bevel=0.01, scale=(radius * 0.9, radius * 0.5, radius * 0.05)))
    elif kind == "gasmask":
        parts.append(sphere("MaskShell", radius * 1.02, (0.0, -radius * 0.05, center_z), accent_mat,
                            segments=14, rings=10, scale=(1.0, 1.05, 1.0)))
        parts.append(cylinder("MaskFilter", radius * 0.28, radius * 0.4,
                              (0.0, -radius * 1.05, center_z - radius * 0.2), primary_mat, verts=12,
                              rot=(1.5708, 0.0, 0.0)))
    elif kind == "helm":
        parts.append(sphere("PoliceHelmet", radius * 0.98, (0.0, 0.0, center_z + radius * 0.25), primary_mat,
                            segments=14, rings=8, scale=(1.0, 1.05, 0.85)))
        parts.append(cylinder("PoliceBrim", radius * 1.16, radius * 0.1, (0.0, -radius * 0.2, center_z + radius * 0.02),
                              primary_mat, verts=16))
    return parts
