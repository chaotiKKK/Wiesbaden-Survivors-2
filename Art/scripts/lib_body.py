"""Parametric hero body builder: six builds from the shipping game's skins."""
import bpy

from ws2_common import cube, cylinder, material, sphere

BUILDS = {
    # height, shoulder width, torso depth, head scale
    "kid": (1.35, 0.42, 0.26, 1.18),
    "normal": (1.78, 0.52, 0.30, 1.00),
    "giant": (2.20, 0.70, 0.40, 0.94),
    "slim": (1.80, 0.38, 0.24, 0.96),
    "square": (1.74, 0.60, 0.34, 1.02),
    "wide": (1.68, 0.66, 0.42, 1.04),
}


def build_body(build="normal", chest="#ffcf4a", accent="#e2564a", skin="#f2d5b0", pants="#2a2f3a"):
    height, shoulder, depth, head_scale = BUILDS.get(build, BUILDS["normal"])

    m_skin = material(f"{build}_skin", skin, roughness=0.65)
    m_chest = material(f"{build}_chest", chest, roughness=0.5)
    m_accent = material(f"{build}_accent", accent, roughness=0.45, metallic=0.15)
    m_pants = material(f"{build}_pants", pants, roughness=0.7)
    m_boots = material(f"{build}_boots", "#14171d", roughness=0.6, metallic=0.2)

    hip = height * 0.52
    parts = []

    # torso: two stacked volumes keep the silhouette readable from above
    parts.append(cube("Torso", None, (0.0, 0.0, hip + height * 0.19), m_chest, bevel=0.04,
                      scale=(shoulder, depth, height * 0.30)))
    parts.append(cube("Hip", None, (0.0, 0.0, hip + height * 0.02), m_pants, bevel=0.03,
                      scale=(shoulder * 0.82, depth * 0.95, height * 0.12)))
    parts.append(cube("Vest", None, (0.0, -depth * 0.12, hip + height * 0.2), m_accent, bevel=0.02,
                      scale=(shoulder * 0.55, depth * 0.55, height * 0.18)))

    # legs
    for sign in (-1, 1):
        x = sign * shoulder * 0.24
        parts.append(cylinder("Thigh", shoulder * 0.17, height * 0.24, (x, 0.0, hip - height * 0.10),
                              m_pants, verts=12))
        parts.append(cylinder("Shin", shoulder * 0.15, height * 0.22, (x, 0.0, hip - height * 0.32),
                              m_pants, verts=12))
        parts.append(cube("Boot", None, (x, -shoulder * 0.06, height * 0.045), m_boots, bevel=0.02,
                          scale=(shoulder * 0.30, shoulder * 0.46, height * 0.055)))

    # arms
    for sign in (-1, 1):
        x = sign * shoulder * 0.62
        parts.append(cylinder("UpperArm", shoulder * 0.13, height * 0.22,
                              (x, 0.0, hip + height * 0.245), m_chest, verts=10))
        parts.append(cylinder("ForeArm", shoulder * 0.115, height * 0.21,
                              (x + sign * shoulder * 0.08, -shoulder * 0.05, hip + height * 0.06),
                              m_skin, verts=10, rot=(0.0, sign * 0.25, 0.0)))
        parts.append(sphere("Hand", shoulder * 0.13,
                            (x + sign * shoulder * 0.12, -shoulder * 0.08, hip - height * 0.03),
                            m_skin, segments=10, rings=6))

    # shoulder pads read strongly from the top-down camera
    for sign in (-1, 1):
        parts.append(sphere("ShoulderPad", shoulder * 0.22,
                            (sign * shoulder * 0.56, 0.0, hip + height * 0.325), m_accent,
                            segments=12, rings=8, scale=(1.0, 1.0, 0.6)))

    # neck
    parts.append(cylinder("Neck", shoulder * 0.13, height * 0.06, (0.0, 0.0, hip + height * 0.355),
                          m_skin, verts=10))

    head_center = hip + height * 0.44
    head_radius = height * 0.085 * head_scale
    return parts, head_center, head_radius, height, m_skin, m_accent


def build_prop(kind, hand_x, hand_z, primary_mat, accent_mat, height=1.78):
    """Held props — silhouette punctuation, not detail meshes."""
    parts = []
    if kind == "guitar":
        parts.append(cube("GuitarBody", None, (hand_x, -0.18, hand_z + 0.06), accent_mat, bevel=0.03,
                          scale=(0.10, 0.34, 0.34)))
        parts.append(cylinder("GuitarNeck", 0.035, 0.62, (hand_x, -0.18, hand_z + 0.38), primary_mat,
                              verts=8, rot=(0.35, 0.0, 0.0)))
        parts.append(sphere("GuitarHead", 0.07, (hand_x, -0.30, hand_z + 0.66), accent_mat, segments=8, rings=6))
    elif kind == "wrench":
        parts.append(cylinder("WrenchShaft", 0.03, 0.44, (hand_x, -0.22, hand_z), primary_mat, verts=8,
                              rot=(0.4, 0.0, 0.0)))
        parts.append(cube("WrenchHead", None, (hand_x, -0.34, hand_z + 0.2), primary_mat, bevel=0.01,
                          scale=(0.09, 0.09, 0.05)))
    elif kind == "nunchaku":
        for index in range(2):
            parts.append(cylinder("NunchakuStick", 0.025, 0.30,
                                  (hand_x + index * 0.06, -0.24, hand_z - index * 0.1), primary_mat,
                                  verts=8, rot=(0.6, 0.0, 0.0)))
    elif kind == "none":
        pass
    return parts
