"""Headless preview of the assembled fixture meshes (a check for people, not used by the app).

    python fixtures/rover/cad/render_preview.py                       # -> fixtures/rover/meshes/preview.png
    python fixtures/rover/cad/render_preview.py --object remote --out /tmp/remote.png
    python fixtures/rover/cad/render_preview.py --part wheel-l --out /tmp/wheel.png   # one part, own frame
    python fixtures/rover/cad/render_preview.py --views --out /tmp/views.png         # 2x2: hero, side, front, top
    python fixtures/rover/cad/render_preview.py --hide top-shell,main-board          # look inside

Needs trimesh, pyrender and an EGL or OSMesa GL (PYOPENGL_PLATFORM defaults to egl). Places every part's meshFile
at its manifest placement (position mm, rotation quaternion w x y z). pyrender has no environment lighting, so metals
are previewed with metallic capped at 0.6.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path

os.environ.setdefault("PYOPENGL_PLATFORM", "egl")

import numpy as np  # noqa: E402

if not hasattr(np, "infty"):  # pyrender 0.1.45 still uses the NumPy 1 alias
    np.infty = np.inf
import pyrender  # noqa: E402
import trimesh  # noqa: E402
from PIL import Image  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]


def quat_matrix(q):
    w, x, y, z = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def look_at(eye, target, up=(0, 0, 1)):
    eye, target, up = map(np.asarray, (eye, target, up))
    if abs(np.dot((target - eye) / np.linalg.norm(target - eye), up)) > 0.99:
        up = np.array([1.0, 0.0, 0.0])  # looking straight down: +X (forward) points up the image
    f = target - eye
    f = f / np.linalg.norm(f)
    s = np.cross(f, up)
    s = s / np.linalg.norm(s)
    u = np.cross(s, f)
    m = np.eye(4)
    m[:3, 0], m[:3, 1], m[:3, 2], m[:3, 3] = s, u, -f, eye
    return m


def load_part(path: Path, pose: np.ndarray, scene: pyrender.Scene):
    tm = trimesh.load(path, force="scene", process=False)
    for name, geom in tm.geometry.items():
        mat = geom.visual.material
        base = np.array(getattr(mat, "baseColorFactor", None) if getattr(mat, "baseColorFactor", None) is not None
                        else [200, 200, 200, 255], dtype=float)
        if base.max() > 1.0:
            base = base / 255.0
        # glTF colours are linear; pyrender does no output gamma, so preview in sRGB.
        base[:3] = np.clip(base[:3], 0, 1) ** (1 / 2.2)
        metal = float(getattr(mat, "metallicFactor", 0.0) or 0.0)
        rough = float(getattr(mat, "roughnessFactor", 0.6) or 0.6)
        emissive = getattr(mat, "emissiveFactor", None)
        pm = pyrender.MetallicRoughnessMaterial(baseColorFactor=base, metallicFactor=min(metal, 0.6),
                                                roughnessFactor=max(rough, 0.25),
                                                emissiveFactor=emissive if emissive is not None else (0, 0, 0))
        node_tf = tm.graph.get(name)[0] if name in tm.graph.nodes else np.eye(4)
        for node_name in tm.graph.nodes_geometry:
            tf, g = tm.graph[node_name]
            if g == name:
                node_tf = tf
        m = pyrender.Mesh.from_trimesh(geom, material=pm, smooth=True)
        scene.add(m, pose=pose @ node_tf)


def placed_parts(manifest, object_slug, hide, part=None):
    """Parts with a mesh. Before --manifest has run, falls back to meshes/<object>/<slug>.glb and its bounds."""
    hints_path = ROOT / "meshes" / "materials.json"
    hints = json.loads(hints_path.read_text(encoding="utf-8")) if hints_path.exists() else {}
    for obj in manifest["objects"]:
        if object_slug and obj["id"] != f"obj:{object_slug}":
            continue
        for p in obj["parts"]:
            slug = p["id"].split("/")[-1]
            if not p.get("meshFile") and p["id"] in hints:
                p = {**p, "meshFile": hints[p["id"]]["meshFile"], "bounds": hints[p["id"]]["bounds"]}
            if slug in hide or not p.get("meshFile"):
                continue
            if part and slug != part:
                continue
            pose = np.eye(4)
            if not part:
                pose[:3, :3] = quat_matrix(p["placement"]["rotation"])
                pose[:3, 3] = p["placement"]["position"]
            yield p, pose


def render(scene_parts, eye, target, size, fov=0.5, floor=True):
    scene = pyrender.Scene(bg_color=[0.97, 0.97, 0.98, 1.0], ambient_light=[0.14, 0.14, 0.16])
    lo, hi = np.full(3, 1e9), np.full(3, -1e9)
    for p, pose in scene_parts:
        load_part(ROOT / p["meshFile"], pose, scene)
        b = np.array([p["bounds"]["min"], p["bounds"]["max"]]) + pose[:3, 3]
        lo, hi = np.minimum(lo, b[0]), np.maximum(hi, b[1])
    if floor:
        ground = trimesh.creation.box((2000, 2000, 1))
        ground.apply_translation((0, 0, min(lo[2], 0) - 0.5))
        scene.add(pyrender.Mesh.from_trimesh(ground, material=pyrender.MetallicRoughnessMaterial(
            baseColorFactor=[0.93, 0.93, 0.94, 1], roughnessFactor=0.9, emissiveFactor=[0.35, 0.35, 0.36])))
    cam = pyrender.PerspectiveCamera(yfov=fov, aspectRatio=size[0] / size[1], znear=5, zfar=5000)
    scene.add(cam, pose=look_at(eye, target))
    key = look_at(np.array(target) + [120, 260, 700], target)
    scene.add(pyrender.DirectionalLight(color=[1, 0.98, 0.95], intensity=2.4), pose=key)
    # Fill and rim are point lights so only the key light casts (directional) shadows. Units are mm.
    for offset, color, power in (([500, -600, 300], [0.92, 0.95, 1.0], 1.6), ([-600, 300, 500], [1, 1, 1], 1.0),
                                 ([-300, -500, 150], [1, 1, 1], 0.6)):
        pose = np.eye(4)
        pose[:3, 3] = np.array(target) + offset
        dist2 = float(np.dot(offset, offset))
        scene.add(pyrender.PointLight(color=color, intensity=power * dist2 / 1.0), pose=pose)
    r = pyrender.OffscreenRenderer(size[0], size[1])
    flags = pyrender.RenderFlags.SHADOWS_DIRECTIONAL
    color, _ = r.render(scene, flags=flags)
    r.delete()
    return Image.fromarray(color), (lo, hi)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--object", default="rover")
    ap.add_argument("--part")
    ap.add_argument("--hide", default="")
    ap.add_argument("--views", action="store_true")
    ap.add_argument("--out", default=str(ROOT / "meshes" / "preview.png"))
    ap.add_argument("--size", default="1600x1100")
    a = ap.parse_args()
    manifest = json.loads((ROOT / "fulmetry.project.json").read_text(encoding="utf-8"))
    hide = set(filter(None, a.hide.split(",")))
    parts = list(placed_parts(manifest, a.object, hide, a.part))
    lo = np.min([np.array(p["bounds"]["min"]) + pose[:3, 3] for p, pose in parts], axis=0)
    hi = np.max([np.array(p["bounds"]["max"]) + pose[:3, 3] for p, pose in parts], axis=0)
    c = (lo + hi) / 2
    d = float(np.linalg.norm(hi - lo)) * 1.7
    w, h = map(int, a.size.split("x"))
    hero = c + d * np.array([0.68, -0.6, 0.38])
    if not a.views:
        img, _ = render(parts, hero, c, (w, h), floor=not a.part)
    else:
        views = [hero, c + d * np.array([0.0, -1.0, 0.12]), c + d * np.array([1.0, 0.0, 0.15]),
                 c + d * np.array([0.0, 0.0, 1.0])]
        tiles = [render(parts, v, c, (w // 2, h // 2), floor=not a.part)[0] for v in views]
        img = Image.new("RGB", (w // 2 * 2, h // 2 * 2))
        for i, t in enumerate(tiles):
            img.paste(t, ((i % 2) * (w // 2), (i // 2) * (h // 2)))
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    img.save(a.out)
    print(a.out)


if __name__ == "__main__":
    main()
