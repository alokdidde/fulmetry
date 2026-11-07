"""cad.build: run a part's CAD source and export STEP + GLB, cached by a hash of source and params.

Convention: part `part:<object>/<slug>` has its source at `<projectRoot>/cad/<object>/<slug>.rhai` or `<slug>.py`.
A `.rhai` script (`params()` and `build(p)`) is built by SolidCraft's kernel through the `fulmetry-cad` binary
(cad-rs/; found as $FULMETRY_CAD_BIN or on PATH). A `.py` source defines `PARAMS: dict` and
`build(params: dict) -> build123d Part`. Outputs go to
`<projectRoot>/.fulmetry/build/<object>/<slug>.{step,glb,json}`; the .json records the hash and bounds.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import subprocess
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


def solidcraft_bin() -> str | None:
    return os.environ.get("FULMETRY_CAD_BIN") or shutil.which("fulmetry-cad")


def engines() -> dict[str, str]:
    """Installed CAD engines and their versions, without importing them (OCP takes seconds to load)."""
    from importlib.metadata import PackageNotFoundError, version
    found = {}
    try:
        found["build123d"] = version("build123d")
    except PackageNotFoundError:
        pass
    if solidcraft_bin():
        try:
            found["solidcraft"] = _solidcraft(["version"])["solidcraft"]
        except RpcError:
            pass
    return found


def parse_part(part: str) -> tuple[str, str]:
    m = PART_ID.match(part)
    if not m:
        raise RpcError(INVALID_PARAMS, f"part id must look like part:<object>/<slug>, got {part!r}")
    return m.group(1), m.group(2)


def source_path(project_root: Path, part: str) -> Path:
    """The part's .rhai source if it has one, else its .py path (which may not exist)."""
    obj, slug = parse_part(part)
    rhai = project_root / "cad" / obj / f"{slug}.rhai"
    return rhai if rhai.is_file() else rhai.with_suffix(".py")


def _solidcraft(args: list[str], request: dict | None = None) -> dict:
    exe = solidcraft_bin()
    if not exe:
        raise RpcError(ENGINE_MISSING, "fulmetry-cad (SolidCraft) is not installed; build cad-rs/ or set FULMETRY_CAD_BIN")
    try:
        r = subprocess.run([exe, *args], input=json.dumps(request) if request is not None else "", capture_output=True,
                           text=True, timeout=600)
    except (OSError, subprocess.TimeoutExpired) as e:
        raise RpcError(ENGINE_MISSING, f"fulmetry-cad failed to run: {e}") from e
    if r.returncode != 0:
        raise RpcError(BUILD_FAILED, r.stderr.strip() or f"fulmetry-cad exited with {r.returncode}")
    return json.loads(r.stdout)


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
    solidcraft = src.suffix == ".rhai"
    if solidcraft:
        module = None
        params = {**_solidcraft(["params", str(src)]), **(overrides or {})}
    else:
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

    out.mkdir(parents=True, exist_ok=True)
    meta.unlink(missing_ok=True)  # never leave a hash that points at half-written files
    from .mesh import material_from_color
    default = material_from_color(slug, DEFAULT_COLOR)
    if solidcraft:
        bounds = _build_solidcraft(src, overrides or {}, step, glb, default, slug)
    else:
        bounds = _build_build123d(src, module, params, step, glb, default, slug)
    meta.write_text(json.dumps({"part": part, "sourceHash": digest, "params": params, "bounds": bounds},
                               indent=2), encoding="utf-8")
    return {**result, "bounds": bounds}


def _build_solidcraft(src: Path, overrides: dict, step: Path, glb: Path, default: dict, name: str) -> dict:
    from .mesh import PALETTE
    tmp_step, tmp_glb = step.with_name(step.name + ".tmp"), glb.with_name(glb.name + ".tmp")
    r = _solidcraft(["build"], {"source": str(src), "overrides": overrides, "step": str(tmp_step), "glb": str(tmp_glb),
                                "name": name, "palette": PALETTE, "default": default, "chord": LINEAR_DEFLECTION,
                                "angle": ANGULAR_DEFLECTION})
    for tmp, final in ((tmp_step, step), (tmp_glb, glb)):
        if not tmp.is_file():
            raise RpcError(BUILD_FAILED, f"export of {final.name} failed")
        os.replace(tmp, final)
    return r["bounds"]


def _build_build123d(src: Path, module: Any, params: dict, step: Path, glb: Path, default: dict, name: str) -> dict:
    from build123d import export_step

    from .mesh import write_glb

    try:
        shape = module.build(dict(params))
    except Exception as e:
        raise RpcError(BUILD_FAILED, f"{src.name} build() raised {type(e).__name__}: {e}") from e
    if shape is None or not hasattr(shape, "bounding_box") or not shape.is_valid:
        raise RpcError(BUILD_FAILED, f"{src.name} build() did not return a valid solid")

    bb = shape.bounding_box(optimal=True)
    bounds = {"min": [round(v, 4) for v in bb.min], "max": [round(v, 4) for v in bb.max]}
    _write_atomic(step, lambda p: export_step(shape, p))
    _write_atomic(glb, lambda p: write_glb(shape, p, default, linear=LINEAR_DEFLECTION, angular=ANGULAR_DEFLECTION,
                                           name=name) is not None)
    return bounds


if __name__ == "__main__":  # python -m fulmetry_worker.cad <projectRoot> <part>
    print(json.dumps(build(sys.argv[1], sys.argv[2]), indent=2))
