
## Working agreement: who does what

The session model orchestrates: it plans, writes briefs, reviews results and
integrates. The parts (implementation, docs, tests, investigations) are built
by subagents. **Opus** (`model: "opus"`) does the demanding parts: audio
engine, driver, tracker, anything that needs judgement. **Sonnet**
(`model: "sonnet"`) does the simpler, well-specified parts when that saves
time and tokens: documentation sync, screenshots, small UI wiring, lookups.
A subagent on the orchestrator's own model is possible but needs the user's
permission first, with the reason. `.claude/settings.json` enforces this with a PreToolUse hook on the
Agent tool; the same rule belongs in `~/.claude/settings.json` for other
projects.
