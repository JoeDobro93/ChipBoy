# ChipBoy

A JUCE 8 / C++20 VST3 / AU / Standalone plugin: a cycle-exact DMG / CGB Game Boy APU
with measured analog colouring, an LSDj-shaped bank, a tracker whose channels keep
their own time, and a second plugin (ChipBoy Voice) linked through shared memory.
`Source/core/` is the engine (links nothing), `Source/plugin/` the JUCE layer,
`tools/` the console checks and generators, `Tests/` the Catch2 tests, `docs/` the
design. Read `docs/HANDOFF.md` first: current state, open questions, next steps.

# Commands

- Gate (build + every check, short output): `tools/gate.sh` — `core`, `plugin` or
  `all` (default); `tools/gate.sh core -t driver` runs one test area (a Catch2 tag) while working.
  Full logs in `build-*/gate.log`. Set `CHIPBOY_JUCE_DIR` to a local JUCE 8.0.15 clone.
- LSDj import from the command line: `chipboy_recordtest --import-sav SAV NAME|working OUT.cbsong`
  (the same code as *Import .sav…*; the save stays outside the tree).
- Screenshots: `chipboy_uishot` under Xvfb (`--song`, `--shaped`, `--hex`,
  `--scope-check`, `--tab-switch`).
- LSDj parity: configure with `-DCHIPBOY_LSDJREF=ON` and `CHIPBOY_LSDJ_ROM` (the ROM
  stays outside the tree; tests skip without it).
- Review a change: `/review [base] [§section]`.

The gate runs here, on Linux; GitHub Actions is not a feedback loop (a push runs only
the L1 rule check; platform builds are started by hand at the end of a round). The
user builds Windows and macOS locally and reports errors.

# Working rules

- Do the work in the main session: design, code, tests, docs, the push. Do not spawn
  subagents to write or edit code.
- Delegate only a search across more than five files (Explore, Haiku) or a bounded
  side task on Sonnet that touches files you are not editing (a docs sync, a demo or
  screenshot regeneration). No nested agents, no background watchers, no process left
  behind; builds run in the foreground with a timeout. A subagent on the session's
  own model needs the user's permission and a reason.
- Read only the files the task needs; do not survey the repo. Subagent reports under
  300 words, never file contents.
- One change at a time: edit, `tools/gate.sh core -t <tag>` while working, the full
  gate once before the push. Ask before a broad refactor.
- A design change gets a numbered section in `docs/COMMANDS_AND_TEMPO.md` before the
  code; a large feature (the exporter) gets `docs/plan-<feature>.md` first: data
  layouts, function signatures, edge cases, the tests to add.
- Every departure from the spec goes in `CHANGES.md` (why, what was considered).
  Regenerate what a change invalidates (`make_demo.py`, `make_songs.py`,
  `--write-song`, `--write-state`, screenshots). The 76-parameter table is
  cross-checked by the demo generator: do not change it casually.
- When the change is done: update `docs/HANDOFF.md` (Done / Open issues / Next
  steps), push `main` once, stop.

# Write for Windows and macOS while building on Linux

- `long` is 32-bit on Windows: fixed-width types or `size_t`; no `ssize_t`, VLAs,
  `alloca`, `__attribute__`.
- Narrowing is an error in braces and a warning elsewhere: cast `char`, `uint8_t`,
  `float` explicitly. `std::min<int>(…)` beside `windows.h`; `M_PI` via
  `juce::MathConstants`. Pick the overload of `std::abs`/`std::pow`.
- 1 MB stacks on Windows' main and message threads: no `Bank`, `Song`,
  `InstanceRegion` or large `std::array` as a stack local, no `cond ? *ptr : T{}`.
  Console tools link with `/STACK:8MB`; plugins cannot.
- Paths through `juce::File`, text through `juce::String`. Warning-free with
  `-Wall -Wextra` here; assume `/W4` there.

# Licensing and layout rules

L1 `Source/core` links nothing and includes only `<std>` and `"core/..."`; L2 no
GPL/MPL code copied; L3 no LSDj content (ROMs, samples, manual text; behaviour may be
checked and paraphrased); L4 every dependency in `docs/LICENSING.md`; L5 fonts under the
OFL with their texts; L6 the repository stays private until D1. Design lives in
`docs/CHIPBOY_SPEC.md`, `docs/COMMANDS_AND_TEMPO.md`, `docs/UI_DESIGN.md`.

# Compact instructions

Keep: the current task, files changed, failing tests, decisions made, what is left.
Drop: file contents, passing test output, exploration results, build logs.
