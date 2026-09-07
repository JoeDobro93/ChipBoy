#!/usr/bin/env python3
"""PreToolUse hook: subagents run on Opus, Sonnet or Haiku only.

The orchestrating session decides and reviews; the parts are built by
cheaper models. A subagent on the orchestrator's own model needs the user's
explicit permission, so a spawn without an allowed `model` is refused and
the refusal tells the caller to ask first.
"""
import json
import sys

ALLOWED = {"opus", "sonnet", "haiku"}

try:
    data = json.load(sys.stdin)
except Exception:
    sys.exit(0)

if data.get("tool_name") != "Agent":
    sys.exit(0)

model = str((data.get("tool_input") or {}).get("model", "")).strip().lower()
if model in ALLOWED:
    sys.exit(0)

print(
    "Subagents must be spawned with model 'opus' (or 'sonnet' / 'haiku'); "
    f"got {model or 'none, which inherits the orchestrator'}. "
    "Spawning a subagent on the orchestrator's model is only allowed after the user has said yes.",
    file=sys.stderr,
)
sys.exit(2)
