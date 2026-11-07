"""JSON-RPC 2.0 over stdio, one request per line. Every params and result is checked against /contracts/worker.

Methods are registered with @method; a contract without an implementation answers "not implemented"
so the app and agents can be written against the whole surface from M0.
"""
from __future__ import annotations

import json
import sys
from typing import Any, Callable

from . import __version__
from .contracts import errors, method_contracts, project_schema

Handler = Callable[[dict], dict]
_handlers: dict[str, Handler] = {}

PARSE_ERROR, INVALID_REQUEST, METHOD_NOT_FOUND, INVALID_PARAMS, INTERNAL = -32700, -32600, -32601, -32602, -32603
NOT_IMPLEMENTED = -32001
RESULT_CONTRACT = -32002


def method(name: str) -> Callable[[Handler], Handler]:
    def wrap(fn: Handler) -> Handler:
        if name not in method_contracts():
            raise RuntimeError(f"{name} has no contract in contracts/worker")
        _handlers[name] = fn
        return fn
    return wrap


@method("worker.ping")
def ping(_: dict) -> dict:
    return {"version": __version__, "engines": {}}


@method("project.validate")
def validate_project(params: dict) -> dict:
    errs = errors(project_schema(), params["manifest"])
    return {"valid": not errs, "errors": errs}


def _error(id_: Any, code: int, message: str, data: Any = None) -> dict:
    err = {"code": code, "message": message}
    if data is not None:
        err["data"] = data
    return {"jsonrpc": "2.0", "id": id_, "error": err}


def handle(request: Any) -> dict | None:
    if not isinstance(request, dict) or request.get("jsonrpc") != "2.0" or not isinstance(request.get("method"), str):
        return _error(None, INVALID_REQUEST, "invalid request")
    id_ = request.get("id")
    name = request["method"]
    params = request.get("params", {})
    contract = method_contracts().get(name)
    if contract is None:
        return _error(id_, METHOD_NOT_FOUND, f"unknown method {name}")
    if errs := errors(contract["params"], params):
        return _error(id_, INVALID_PARAMS, "params do not match the contract", errs)
    fn = _handlers.get(name)
    if fn is None:
        return _error(id_, NOT_IMPLEMENTED, f"{name} is in the contract but not implemented yet (since {contract['since']})")
    try:
        result = fn(params)
    except Exception as e:  # engines fail in many ways; report, never crash the worker
        return _error(id_, INTERNAL, str(e))
    if errs := errors(contract["result"], result):
        return _error(id_, RESULT_CONTRACT, "worker result broke its contract", errs)
    return None if id_ is None else {"jsonrpc": "2.0", "id": id_, "result": result}


def main() -> None:
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            reply = handle(json.loads(line))
        except json.JSONDecodeError:
            reply = _error(None, PARSE_ERROR, "parse error")
        if reply is not None:
            sys.stdout.write(json.dumps(reply) + "\n")
            sys.stdout.flush()


if __name__ == "__main__":
    main()
