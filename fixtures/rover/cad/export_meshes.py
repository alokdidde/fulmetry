"""Build every part of the rover fixture and write one binary glTF per part, plus material hints.

    python fixtures/rover/cad/export_meshes.py                 # all parts that have a source
    python fixtures/rover/cad/export_meshes.py wheel-l motor-l # only these slugs (any object)
    python fixtures/rover/cad/export_meshes.py --manifest      # also write meshFile + bounds into the manifest

Needs the worker's cad extra (`pip install -e 'worker[cad]'`). Output: fixtures/rover/meshes/<object>/<slug>.glb in
the part's own frame, mm, +Z up (see worker/fulmetry_worker/mesh.py), and fixtures/rover/meshes/materials.json with
each part's materials (base colour, roughness, metallic), triangle count, size and bounds.
"""
from __future__ import annotations

import json
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]            # fixtures/rover (the project root)
REPO = ROOT.parents[1]
sys.path.insert(0, str(REPO / "worker"))

from fulmetry_worker import cad, mesh  # noqa: E402

MESHES = ROOT / "meshes"
MANIFEST = ROOT / "fulmetry.project.json"
LINEAR, ANGULAR = 0.05, 0.2  # mm, rad: the worker's cad.build tolerances


def main(argv: list[str]) -> int:
    write_manifest = "--manifest" in argv
    only = {a for a in argv if not a.startswith("--")}
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    hints_path = MESHES / "materials.json"
    hints = json.loads(hints_path.read_text(encoding="utf-8")) if hints_path.exists() else {}
    total = 0
    for obj in manifest["objects"]:
        for part in obj["parts"]:
            o, slug = cad.parse_part(part["id"])
            src = cad.source_path(ROOT, part["id"])
            if not src.is_file() or (only and slug not in only and part["id"] not in only):
                continue
            t0 = time.time()
            module = cad._load(src, src.read_bytes())
            shape = module.build(dict(module.PARAMS))
            if not shape.is_valid:
                print(f"!! {part['id']}: build() returned an invalid shape", file=sys.stderr)
                return 1
            bb = shape.bounding_box(optimal=True)
            bounds = {"min": [round(v, 2) + 0.0 for v in bb.min], "max": [round(v, 2) + 0.0 for v in bb.max]}
            out = MESHES / o / f"{slug}.glb"
            out.parent.mkdir(parents=True, exist_ok=True)
            default = mesh.material_from_color(part.get("material") or part["name"], part.get("color", "#c9ccd1"))
            info = mesh.write_glb(shape, out, default, linear=LINEAR, angular=ANGULAR, name=slug)
            rel = out.relative_to(ROOT).as_posix()
            hints[part["id"]] = {"meshFile": rel, "units": "mm", "up": "+Z", "bounds": bounds, **info}
            total += info["bytes"]
            print(f"{part['id']:28s} {info['triangles']:7d} tris {info['bytes'] / 1024:8.1f} KB "
                  f"{time.time() - t0:5.1f} s  bounds {bounds['min']} {bounds['max']}")
            if write_manifest:
                part["meshFile"] = rel
                part["bounds"] = bounds
    hints_path.parent.mkdir(parents=True, exist_ok=True)
    hints_path.write_text(json.dumps(dict(sorted(hints.items())), indent=2) + "\n", encoding="utf-8")
    all_bytes = sum(p.stat().st_size for p in MESHES.rglob("*.glb"))
    print(f"written this run: {total / 1024:.0f} KB; all meshes: {all_bytes / 1024:.0f} KB")
    if write_manifest:
        MANIFEST.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
