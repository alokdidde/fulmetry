"""cad.build: run a part's build123d source and export STEP + GLB, cached by a hash of source and params.

Convention: part `part:<object>/<slug>` has its source at `<projectRoot>/cad/<object>/<slug>.py`, which defines
`PARAMS: dict` and `build(params: dict) -> build123d Part`. Outputs go to
`<projectRoot>/.fulmetry/build/<object>/<slug>.{step,glb,json}`; the .json records the hash and bounds.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import sys
import types
from pathlib import Path
from typing import Any

from .errors import BUILD_FAILED, ENGINE_MISSING, INVALID_PARAMS, SOURCE_NOT_FOUND, RpcError

PART_ID = re.compile(r"^part:([a-z0-9][a-z0-9-]*)/([a-z0-9][a-z0-9-]*)$")

# Mesh tolerances in mm: fine enough to see the tread, small enough to load fast.
LINEAR_DEFLECTION = 0.05
ANGULAR_DEFLECTION = 0.2
# Colour of children without a material label (see mesh.PALETTE).
DEFAULT_COLOR = "#c9ccd1"


def engines() -> dict[str, str]:
    """Installed CAD engines and their versions, without importing them (OCP takes seconds to load)."""
    from importlib.metadata import PackageNotFoundError, version
    try:
        return {"build123d": version("build123d")}
    except PackageNotFoundError:
        return {}


def parse_part(part: str) -> tuple[str, str]:
    m = PART_ID.match(part)
    if not m:
        raise RpcError(INVALID_PARAMS, f"part id must look like part:<object>/<slug>, got {part!r}")
    return m.group(1), m.group(2)


def source_path(project_root: Path, part: str) -> Path:
    obj, slug = parse_part(part)
    return project_root / "cad" / obj / f"{slug}.py"


def source_hash(source: bytes, params: dict) -> str:
    h = hashlib.sha256(source)
    h.update(json.dumps(params, sort_keys=True, separators=(",", ":")).encode("utf-8"))
    return "sha256:" + h.hexdigest()


def _load(path: Path, source: bytes) -> Any:
    try:
        import build123d  # noqa: F401
    except ImportError as e:
        raise RpcError(ENGINE_MISSING, "build123d is not installed; install the worker's 'cad' extra") from e
    module = types.ModuleType(f"fulmetry_part_{path.stem.replace('-', '_')}")
    module.__file__ = str(path)
    try:
        exec(compile(source, str(path), "exec"), module.__dict__)
    except Exception as e:
        raise RpcError(BUILD_FAILED, f"{path.name} failed to load: {type(e).__name__}: {e}") from e
    if not callable(getattr(module, "build", None)) or not isinstance(getattr(module, "PARAMS", None), dict):
        raise RpcError(BUILD_FAILED, f"{path.name} must define PARAMS (dict) and build(params)")
    return module


def _write_atomic(path: Path, write) -> None:
    tmp = path.with_name(path.name + ".tmp")
    if not write(tmp) or not tmp.exists():
        raise RpcError(BUILD_FAILED, f"export of {path.name} failed")
    os.replace(tmp, path)


def build(project_root: str, part: str, overrides: dict | None = None) -> dict:
    """Builds `part` (params = its PARAMS updated by `overrides`) and returns the cad.build result."""
    root = Path(project_root).resolve()
    src = source_path(root, part)
    if not src.is_file():
        raise RpcError(SOURCE_NOT_FOUND, f"no CAD source for {part}", {"expected": str(src)})
    source = src.read_bytes()
    module = _load(src, source)
    params = {**module.PARAMS, **(overrides or {})}
    digest = source_hash(source, params)

    obj, slug = parse_part(part)
    out = root / ".fulmetry" / "build" / obj
    step, glb, meta = out / f"{slug}.step", out / f"{slug}.glb", out / f"{slug}.json"
    result = {"stepFile": str(step), "meshFile": str(glb), "sourceHash": digest}

    try:
        cached = json.loads(meta.read_text(encoding="utf-8"))
        if cached.get("sourceHash") == digest and step.is_file() and glb.is_file():
            return {**result, "bounds": cached["bounds"]}
    except (OSError, ValueError, KeyError):
        pass

    from build123d import export_step

    from .mesh import material_from_color, write_glb

    try:
        shape = module.build(dict(params))
    except Exception as e:
        raise RpcError(BUILD_FAILED, f"{src.name} build() raised {type(e).__name__}: {e}") from e
    if shape is None or not hasattr(shape, "bounding_box") or not shape.is_valid:
        raise RpcError(BUILD_FAILED, f"{src.name} build() did not return a valid solid")

    bb = shape.bounding_box(optimal=True)
    bounds = {"min": [round(v, 4) for v in bb.min], "max": [round(v, 4) for v in bb.max]}

    out.mkdir(parents=True, exist_ok=True)
    meta.unlink(missing_ok=True)  # never leave a hash that points at half-written files
    _write_atomic(step, lambda p: export_step(shape, p))
    default = material_from_color(slug, DEFAULT_COLOR)
    _write_atomic(glb, lambda p: write_glb(shape, p, default, linear=LINEAR_DEFLECTION, angular=ANGULAR_DEFLECTION,
                                           name=slug) is not None)
    meta.write_text(json.dumps({"part": part, "sourceHash": digest, "params": params, "bounds": bounds},
                               indent=2), encoding="utf-8")
    return {**result, "bounds": bounds}


if __name__ == "__main__":  # python -m fulmetry_worker.cad <projectRoot> <part>
    print(json.dumps(build(sys.argv[1], sys.argv[2]), indent=2))
