"""JSON-RPC error codes the worker answers with, and the exception handlers raise to pick one."""
from __future__ import annotations

from typing import Any

PARSE_ERROR, INVALID_REQUEST, METHOD_NOT_FOUND, INVALID_PARAMS, INTERNAL = -32700, -32600, -32601, -32602, -32603
NOT_IMPLEMENTED = -32001
RESULT_CONTRACT = -32002
# -32010..-32019 are reserved for clients (agent/maker-tools WorkerClient: timeout, exited, cannot start).
SOURCE_NOT_FOUND = -32020  # the part has no source file where the convention says it lives
BUILD_FAILED = -32021      # the source raised, or returned something that is not a valid solid
ENGINE_MISSING = -32022    # the engine (e.g. build123d) is not installed in this worker


class RpcError(Exception):
    def __init__(self, code: int, message: str, data: Any = None):
        super().__init__(message)
        self.code, self.message, self.data = code, message, data
