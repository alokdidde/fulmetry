#!/usr/bin/env python3
"""A scripted `t3 fulmetry`: the bridge's wire (app/net T3AgentClient.h) with no T3 Code behind it.

Each chat message or comment becomes an agent that is working on the next request and done on the one after,
so a test drives it without sleeping. FAKE_BRIDGE=die exits right after it is ready.
"""
import json
import os
import sys


def send(frame):
    sys.stdout.write(json.dumps(frame) + "\n")
    sys.stdout.flush()


def event(**e):
    send({"event": e})


HARNESSES = [
    {"id": "raven", "driver": "acpRegistry", "name": "Raven", "maker": "EverMind", "status": "ready",
     "canSignIn": False, "models": [{"id": "default", "name": "Default"}], "setup": {"step": "idle", "prompt": "none"}},
    {"id": "claudeAgent", "driver": "claudeAgent", "name": "Claude Code", "maker": "Anthropic", "status": "needsSignIn",
     "canSignIn": True, "models": [{"id": "claude-opus-5-5", "name": "Opus 5.5"}], "setup": {"step": "idle"}},
]

agents = {}  # id -> card


def card(agent_id, state, step, **extra):
    c = {"type": "fulmetry.agentUpdated", "agentId": agent_id, "state": state, "step": step, "provider": "raven",
         "model": "default", **agents[agent_id], **extra}
    event(**c)


def advance():
    """Every request moves each agent one step, the way a poll would see a real thread move."""
    for agent_id, a in list(agents.items()):
        stage = a.pop("_stage", 0)
        if stage == 0:
            card(agent_id, "working", "Editing files")
            if "commentId" in a:
                event(type="fulmetry.commentUpdated", commentId=a["commentId"], status="working", agentId=agent_id)
        elif stage == 1:
            card(agent_id, "done", "Done", preview="Resized the wheels to 70 mm.")
            if "commentId" in a:
                event(type="fulmetry.commentUpdated", commentId=a["commentId"], status="resolved", agentId=agent_id,
                      reply="Resized the wheels to 70 mm.")
            else:
                event(type="fulmetry.chatPosted", author="Raven", text="It is a desk rover.", agentId=agent_id)
        if stage < 2:
            a["_stage"] = stage + 1


def handle(req):
    method, params = req["method"], req.get("params", {})
    if method == "fulmetry.subscribe":
        event(type="fulmetry.harnessesUpdated", harnesses=HARNESSES)
        return {"projectId": "p1", "chatThreadId": None, "defaultHarness": "raven"}
    if method == "fulmetry.sendChat":
        agents["chat"] = {"name": "Project chat", "parts": [], "replies": 0}
        return {"agentId": "chat"}
    if method == "fulmetry.startAgentForComment":
        if params.get("provider") == "claudeAgent":
            raise LookupError("harness_not_ready", "Claude Code is not ready")
        agent_id = "thread-" + params["commentId"]
        agents[agent_id] = {"name": "Make the wheels bigger", "parts": [params["anchor"]["part"]],
                            "commentId": params["commentId"], "replies": 0}
        return {"agentId": agent_id, "threadId": agent_id}
    if method == "harness.signIn":
        if params["harness"] != "claudeAgent":
            raise LookupError("unsupported", "Raven signs in on its own")
        return {}
    if method == "fulmetry.reply":
        return {}
    raise LookupError("unknown_method", method)


send({"event": {"type": "fulmetry.ready", "fulmetryExt": 2}})
print("a stray warning that is not a frame", flush=True)
if os.environ.get("FAKE_BRIDGE") == "die":
    sys.exit(3)
for line in sys.stdin:
    if not line.strip():
        continue
    req = json.loads(line)
    advance()
    try:
        send({"id": req["id"], "result": handle(req)})
    except LookupError as e:
        code, message = e.args
        send({"id": req["id"], "error": {"code": code, "message": message}})
