"""Loads the shared schemas in /contracts so the worker validates every call against them."""
from __future__ import annotations

import json
from functools import lru_cache
from pathlib import Path

import jsonschema
from referencing import Registry, Resource

ROOT = Path(__file__).resolve().parents[2] / "contracts"


@lru_cache
def project_schema() -> dict:
    return json.loads((ROOT / "schemas" / "project.schema.json").read_text(encoding="utf-8"))


@lru_cache
def method_contracts() -> dict[str, dict]:
    out = {}
    for f in sorted((ROOT / "worker").glob("*.json")):
        c = json.loads(f.read_text(encoding="utf-8"))
        out[c["method"]] = c
    return out


def validator(schema: dict) -> jsonschema.Draft202012Validator:
    return jsonschema.Draft202012Validator(schema, registry=Registry().with_resource(
        "https://fulmetry.dev/schemas/project.schema.json", Resource.from_contents(project_schema())))


def errors(schema: dict, value) -> list[dict]:
    return [{"path": "/" + "/".join(str(p) for p in e.absolute_path), "message": e.message}
            for e in sorted(validator(schema).iter_errors(value), key=lambda e: list(map(str, e.absolute_path)))]
