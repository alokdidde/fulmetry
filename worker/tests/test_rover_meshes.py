"""The rover fixture's checked-in meshes match its manifest, and its CAD sources still build to those bounds.

The first group needs nothing but the standard library: every part with a source has a meshFile, the file is a
valid GLB in mm with materials, and its vertex extents equal the manifest bounds (0.5 mm). The second group
(build123d only) rebuilds each source and compares its exact bounds with the manifest.
"""
from __future__ import annotations

import json
import struct
from pathlib import Path

import pytest

ROVER = Path(__file__).resolve().parents[2] / "fixtures" / "rover"
MANIFEST = json.loads((ROVER / "fulmetry.project.json").read_text(encoding="utf-8"))
PARTS = [p for o in MANIFEST["objects"] for p in o["parts"]]
WITH_SOURCE = [p for p in PARTS
               if (ROVER / "cad" / p["id"][5:].split("/")[0] / f"{p['id'].split('/')[-1]}.py").is_file()]
MAX_BYTES = 2 * 1024 * 1024


def read_glb(path: Path) -> dict:
    data = path.read_bytes()
    magic, version, length = struct.unpack_from("<4sII", data, 0)
    assert magic == b"glTF" and version == 2 and length == len(data)
    json_len, json_type = struct.unpack_from("<I4s", data, 12)
    assert json_type == b"JSON"
    return json.loads(data[20:20 + json_len])


def test_every_part_with_a_source_has_a_mesh():
    assert len(WITH_SOURCE) >= 13
    for p in WITH_SOURCE:
        assert p.get("meshFile"), f"{p['id']} has a CAD source but no meshFile"


@pytest.mark.parametrize("part", [p for p in PARTS if p.get("meshFile")], ids=lambda p: p["id"])
def test_mesh_matches_manifest_bounds(part):
    path = ROVER / part["meshFile"]
    assert path.is_file(), path
    assert path.stat().st_size < MAX_BYTES
    gltf = read_glb(path)
    assert gltf["asset"]["extras"]["fulmetry"] == {"units": "mm", "up": "+Z", "frame": "part"}
    assert gltf["materials"], "no materials"
    for m in gltf["materials"]:
        pbr = m["pbrMetallicRoughness"]
        assert len(pbr["baseColorFactor"]) == 4 and 0 <= pbr["roughnessFactor"] <= 1
    lo, hi = [1e9] * 3, [-1e9] * 3
    for prim in gltf["meshes"][0]["primitives"]:
        acc = gltf["accessors"][prim["attributes"]["POSITION"]]
        lo = [min(a, b) for a, b in zip(lo, acc["min"])]
        hi = [max(a, b) for a, b in zip(hi, acc["max"])]
    for got, want in ((lo, part["bounds"]["min"]), (hi, part["bounds"]["max"])):
        assert got == pytest.approx(want, abs=0.5), part["id"]


def test_material_hints_cover_every_mesh():
    hints = json.loads((ROVER / "meshes" / "materials.json").read_text(encoding="utf-8"))
    for p in PARTS:
        if p.get("meshFile"):
            assert hints[p["id"]]["meshFile"] == p["meshFile"]
            assert all({"key", "color", "baseColor", "roughness", "metallic"} <= set(m)
                       for m in hints[p["id"]]["materials"])


@pytest.mark.parametrize("part", WITH_SOURCE, ids=lambda p: p["id"])
def test_source_builds_to_manifest_bounds(part):
    pytest.importorskip("build123d")
    from fulmetry_worker import cad

    src = cad.source_path(ROVER, part["id"])
    module = cad._load(src, src.read_bytes())
    shape = module.build(dict(module.PARAMS))
    assert shape.is_valid
    bb = shape.bounding_box(optimal=True)
    assert list(bb.min) == pytest.approx(part["bounds"]["min"], abs=0.05)
    assert list(bb.max) == pytest.approx(part["bounds"]["max"], abs=0.05)
