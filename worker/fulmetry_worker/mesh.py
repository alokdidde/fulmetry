"""Tessellate a build123d shape into a binary glTF (.glb) with one PBR material per labelled child.

Convention (the same for cad.build output and the fixture meshes in fixtures/rover/meshes/):

- Vertex data is in **mm, +Z up, in the part's own frame** (the frame of the manifest's `bounds`). There is no root
  transform: the app applies the part's placement and its own Z-up -> renderer conversion. `asset.extras.fulmetry`
  says so: `{"units": "mm", "up": "+Z"}`. (Plain glTF viewers will show the part 1000x too big and lying down.)
- A shape whose children carry a `label` that names a key of `PALETTE` gets one mesh primitive per material.
  Children with other labels, or a bare solid, use the `default` material passed in (e.g. the manifest colour).
- Normals come from the B-rep surfaces, so curved faces render smooth and edges stay sharp.

Only numpy and OCP (through build123d) are needed; no trimesh at build time.
"""
from __future__ import annotations

import json
import struct
from pathlib import Path
from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:  # numpy comes with the cad extra; the SolidCraft path only needs PALETTE
    import numpy as np

# Material hints: base colour (sRGB hex; written to glTF as linear RGB), roughness, metallic. Keys are what part
# sources put in each child's `label`.
PALETTE: dict[str, dict[str, Any]] = {
    "petg-white":    {"name": "White PETG",          "color": "#efeeea", "roughness": 0.55, "metallic": 0.0},
    "tpu-black":     {"name": "Matte black TPU",     "color": "#1e1f22", "roughness": 0.9,  "metallic": 0.0},
    "pla-white":     {"name": "White PLA",           "color": "#f2f1ed", "roughness": 0.5,  "metallic": 0.0},
    "pla-red":       {"name": "Red PLA",             "color": "#c92a20", "roughness": 0.5,  "metallic": 0.0},
    "pla-grey":      {"name": "Grey PLA",            "color": "#8b8f96", "roughness": 0.55, "metallic": 0.0},
    "servo-blue":    {"name": "Servo case (blue ABS)", "color": "#2f6fe8", "roughness": 0.45, "metallic": 0.0},
    "nylon-white":   {"name": "Horn / connector nylon", "color": "#f2f0e8", "roughness": 0.6, "metallic": 0.0},
    "plastic-black": {"name": "Black plastic",       "color": "#222326", "roughness": 0.55, "metallic": 0.0},
    "pcb-green":     {"name": "Green PCB",           "color": "#1d7a4e", "roughness": 0.4,  "metallic": 0.0},
    "pcb-trace":     {"name": "PCB track under mask", "color": "#2c9a64", "roughness": 0.35, "metallic": 0.0},
    "pcb-blue":      {"name": "Blue PCB",            "color": "#1d4fa0", "roughness": 0.4,  "metallic": 0.0},
    "pcb-black":     {"name": "Black PCB",           "color": "#1a1b1e", "roughness": 0.4,  "metallic": 0.0},
    "ic-black":      {"name": "IC epoxy",            "color": "#151517", "roughness": 0.7,  "metallic": 0.0},
    "tin":           {"name": "Tin shield",          "color": "#c9cbce", "roughness": 0.3,  "metallic": 1.0},
    "steel":         {"name": "Brushed steel",       "color": "#b4b8bd", "roughness": 0.35, "metallic": 1.0},
    "chrome":        {"name": "Chrome",              "color": "#e6e8ea", "roughness": 0.08, "metallic": 1.0},
    "aluminium":     {"name": "Aluminium",           "color": "#c9ccd1", "roughness": 0.4,  "metallic": 1.0},
    "brass":         {"name": "Brass",               "color": "#c8a24a", "roughness": 0.35, "metallic": 1.0},
    "gold":          {"name": "Gold plating",        "color": "#e0b04a", "roughness": 0.25, "metallic": 1.0},
    "copper":        {"name": "Copper",              "color": "#b8733a", "roughness": 0.35, "metallic": 1.0},
    "ceramic":       {"name": "MLCC ceramic",        "color": "#a88b62", "roughness": 0.6,  "metallic": 0.0},
    "terminal-green": {"name": "Screw terminal",     "color": "#2f9a52", "roughness": 0.5,  "metallic": 0.0},
    "wrap-yellow":   {"name": "Yellow PVC wrap",     "color": "#f2b705", "roughness": 0.35, "metallic": 0.0},
    "wire-red":      {"name": "Red wire",            "color": "#c62020", "roughness": 0.5,  "metallic": 0.0},
    "wire-black":    {"name": "Black wire",          "color": "#18181a", "roughness": 0.5,  "metallic": 0.0},
    "wire-brown":    {"name": "Brown wire",          "color": "#5a3a22", "roughness": 0.5,  "metallic": 0.0},
    "wire-orange":   {"name": "Orange wire",         "color": "#ee7a12", "roughness": 0.5,  "metallic": 0.0},
    "glass":         {"name": "Lens glass",          "color": "#0d1420", "roughness": 0.03, "metallic": 0.2},
    "led-amber":     {"name": "Amber LED",           "color": "#ffb21a", "roughness": 0.2,  "metallic": 0.0,
                      "emissive": "#6b4300"},
    "led-green":     {"name": "Green LED",           "color": "#3ddc6a", "roughness": 0.2,  "metallic": 0.0,
                      "emissive": "#0f5a22"},
    "oled":          {"name": "OLED glass",          "color": "#07080a", "roughness": 0.05, "metallic": 0.0},
}


def hex_rgb(h: str) -> list[float]:
    """sRGB hex -> linear RGB 0..1, as glTF's baseColorFactor and emissiveFactor expect."""
    h = h.lstrip("#")
    out = []
    for i in (0, 2, 4):
        c = int(h[i:i + 2], 16) / 255
        out.append(round(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4, 5))
    return out


def material_from_color(name: str, color: str, roughness: float = 0.6, metallic: float = 0.0) -> dict:
    return {"name": name, "color": color, "roughness": roughness, "metallic": metallic}


def _triangulate(shape: Any, linear: float, angular: float) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Positions, normals (float32) and triangle indices (uint32) for every face of `shape`."""
    import numpy as np

    from OCP.BRep import BRep_Tool
    from OCP.BRepLib import BRepLib_ToolTriangulatedShape
    from OCP.BRepMesh import BRepMesh_IncrementalMesh
    from OCP.TopAbs import TopAbs_FACE, TopAbs_REVERSED
    from OCP.TopExp import TopExp_Explorer
    from OCP.TopLoc import TopLoc_Location
    from OCP.TopoDS import TopoDS

    wrapped = shape.wrapped
    BRepMesh_IncrementalMesh(wrapped, linear, False, angular, True)
    pos, nrm, idx = [], [], []
    base = 0
    exp = TopExp_Explorer(wrapped, TopAbs_FACE)
    while exp.More():
        face = TopoDS.Face(exp.Current()) if hasattr(TopoDS, "Face") else TopoDS.Face_s(exp.Current())
        exp.Next()
        loc = TopLoc_Location()
        tri = BRep_Tool.Triangulation_s(face, loc)
        if tri is None or tri.NbTriangles() == 0:
            continue
        if not tri.HasNormals():
            BRepLib_ToolTriangulatedShape.ComputeNormals_s(face, tri)
        trsf = loc.Transformation()
        n = tri.NbNodes()
        p = np.empty((n, 3), np.float64)
        q = np.empty((n, 3), np.float64)
        for i in range(1, n + 1):
            pt = tri.Node(i).Transformed(trsf)
            p[i - 1] = (pt.X(), pt.Y(), pt.Z())
            d = tri.Normal(i).Transformed(trsf)
            q[i - 1] = (d.X(), d.Y(), d.Z())
        t = np.empty((tri.NbTriangles(), 3), np.int64)
        for i in range(1, tri.NbTriangles() + 1):
            a, b, c = tri.Triangle(i).Get()
            t[i - 1] = (a - 1, b - 1, c - 1)
        if face.Orientation() == TopAbs_REVERSED:
            t = t[:, ::-1]
            q = -q
        ln = np.linalg.norm(q, axis=1, keepdims=True)
        q = np.divide(q, ln, out=np.zeros_like(q), where=ln > 0)
        pos.append(p)
        nrm.append(q)
        idx.append(t + base)
        base += n
    if not pos:
        return np.zeros((0, 3), np.float32), np.zeros((0, 3), np.float32), np.zeros((0, 3), np.uint32)
    return (np.concatenate(pos).astype(np.float32), np.concatenate(nrm).astype(np.float32),
            np.concatenate(idx).astype(np.uint32))


def split_by_material(shape: Any) -> list[tuple[str | None, Any]]:
    """[(label, shape)]: the labelled children of a compound, or [(None, shape)] for a bare solid."""
    children = list(getattr(shape, "children", ()) or ())
    if not children:
        return [(getattr(shape, "label", None) or None, shape)]
    return [(c.label or None, c) for c in children]


def write_glb(shape: Any, path: str | Path, default: dict, *, linear: float = 0.05, angular: float = 0.2,
              name: str = "part") -> dict:
    """Writes `shape` to `path` as GLB; returns {"materials": [...], "triangles": n, "bytes": size}."""
    import numpy as np

    groups: dict[str, list[Any]] = {}
    for label, child in split_by_material(shape):
        key = label if label in PALETTE else "_default"
        groups.setdefault(key, []).append(child)

    bin_chunks: list[bytes] = []
    offset = 0
    buffer_views, accessors, meshes_prims, materials = [], [], [], []
    tris_total = 0

    def add_view(data: bytes, target: int) -> int:
        nonlocal offset
        pad = (-len(data)) % 4
        buffer_views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(data), "target": target})
        bin_chunks.append(data + b"\0" * pad)
        offset += len(data) + pad
        return len(buffer_views) - 1

    for key, shapes in groups.items():
        mat = PALETTE[key] if key != "_default" else default
        parts = [_triangulate(s, linear, angular) for s in shapes]
        p = np.concatenate([x[0] for x in parts]) if parts else np.zeros((0, 3), np.float32)
        q = np.concatenate([x[1] for x in parts])
        bases = np.cumsum([0] + [len(x[0]) for x in parts[:-1]])
        t = np.concatenate([x[2] + b for x, b in zip(parts, bases)])
        if len(t) == 0:
            continue
        tris_total += len(t)
        itype, idata = (5123, t.astype(np.uint16)) if len(p) < 65536 else (5125, t.astype(np.uint32))
        pv = add_view(p.tobytes(), 34962)
        nv = add_view(q.tobytes(), 34962)
        iv = add_view(idata.tobytes(), 34963)
        accessors.append({"bufferView": pv, "componentType": 5126, "count": len(p), "type": "VEC3",
                          "min": p.min(0).tolist(), "max": p.max(0).tolist()})
        accessors.append({"bufferView": nv, "componentType": 5126, "count": len(q), "type": "VEC3"})
        accessors.append({"bufferView": iv, "componentType": itype, "count": t.size, "type": "SCALAR"})
        a = len(accessors)
        pbr = {"baseColorFactor": hex_rgb(mat["color"]) + [1.0], "metallicFactor": mat["metallic"],
               "roughnessFactor": mat["roughness"]}
        m = {"name": mat["name"], "pbrMetallicRoughness": pbr, "extras": {"key": key, "color": mat["color"]}}
        if "emissive" in mat:
            m["emissiveFactor"] = hex_rgb(mat["emissive"])
        materials.append(m)
        meshes_prims.append({"attributes": {"POSITION": a - 3, "NORMAL": a - 2}, "indices": a - 1,
                             "material": len(materials) - 1, "mode": 4})

    binary = b"".join(bin_chunks)
    gltf = {
        "asset": {"version": "2.0", "generator": "fulmetry-worker mesh.py",
                  "extras": {"fulmetry": {"units": "mm", "up": "+Z", "frame": "part"}}},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": name, "mesh": 0}],
        "meshes": [{"name": name, "primitives": meshes_prims}],
        "materials": materials,
        "accessors": accessors,
        "bufferViews": buffer_views,
        "buffers": [{"byteLength": len(binary)}],
    }
    js = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    js += b" " * ((-len(js)) % 4)
    total = 12 + 8 + len(js) + 8 + len(binary)
    out = Path(path)
    with open(out, "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, total))
        f.write(struct.pack("<I4s", len(js), b"JSON") + js)
        f.write(struct.pack("<I4s", len(binary), b"BIN\0") + binary)
    hints = [{"key": m["extras"]["key"], "name": m["name"], "color": m["extras"]["color"],
              "baseColor": m["pbrMetallicRoughness"]["baseColorFactor"],
              "roughness": m["pbrMetallicRoughness"]["roughnessFactor"],
              "metallic": m["pbrMetallicRoughness"]["metallicFactor"]} for m in materials]
    return {"materials": hints, "triangles": tris_total, "bytes": total}
