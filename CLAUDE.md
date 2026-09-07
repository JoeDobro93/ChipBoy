
## Working agreement: who does what

The session model orchestrates: it plans, writes briefs, reviews results and
integrates. The parts (implementation, docs, tests, investigations) are built
by subagents on **Opus** (`model: "opus"`), or Sonnet/Haiku for small lookups.
Never spawn a subagent on the orchestrator's own model without asking the user
first. `.claude/settings.json` enforces this with a PreToolUse hook on the
Agent tool; the same rule belongs in `~/.claude/settings.json` for other
projects.
