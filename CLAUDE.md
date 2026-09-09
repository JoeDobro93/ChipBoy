
## Working agreement: the session model does the work

The session model does the work itself — design, code, tests, docs, the
merge and the push — in the main checkout, with incremental builds and one
full gate per change (see below). That is the default; it proved faster and
cheaper on the 5-hour limit than orchestrating subagents, which pay for a
cold context, a brief, a full rebuild in a worktree, a merge and a second
gate for every stage.

Subagents are the exception, used only when they clearly pay:

- a **bounded Sonnet task** (`model: "sonnet"`) that can run while the
  session model codes and touches files it is not editing: a documentation
  sync, screenshot regeneration, a mechanical rename, a demo regeneration;
- a **measurement or research run** with a fixed budget (about an hour) and
  a rule against open-ended fitting or tuning — measured numbers go in as
  lookup tables, and the agent reports at the budget with what is left;
- **Opus** (`model: "opus"`) only for a self-contained piece the session
  model would otherwise have to serialise behind other work, with the same
  budget and never two in parallel.

A subagent on the session model's own model needs the user's permission
first, with the reason; `.claude/settings.json` enforces this with a
PreToolUse hook on the Agent tool (`.claude/hooks/subagent_model_guard.py`).
No subagent starts a background watcher (`until grep … sleep`, a Monitor on a
build log) or leaves a process behind; builds run in the foreground with a
timeout; the session model checks `pgrep -af "until grep|sleep"` after a
stage and ends what is left.

Sessions stay short: `docs/HANDOFF.md` carries the project's state, the open
questions and the exact verification commands, and is updated at the end of
every change, so a new chat starts from this file and that one rather than
from a long transcript.

## Verification runs here, not in Actions

The gate for every merge is local, on this Linux container:

```
cmake -S . -B build-core -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
cmake --build build-core --parallel 4 && ctest --test-dir build-core --output-on-failure
cmake -S . -B build-plugin -DCHIPBOY_BUILD_PLUGIN=ON -DCHIPBOY_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DFETCHCONTENT_SOURCE_DIR_JUCE=<local JUCE 8.0.15 clone>
cmake --build build-plugin --config Release --parallel 4 --target ChipBoy_VST3 ChipBoyVoice_VST3 ChipBoy_Standalone chipboy_linktest chipboy_recordtest chipboy_paramdump chipboy_uishot
ctest --test-dir build-plugin -C Release -R "linktest|recordtest" --output-on-failure
python3 tools/demo/make_demo.py --paramdump build-plugin/chipboy_paramdump_artefacts/Release/chipboy_paramdump
(ulimit -s 1024; ./build-plugin/chipboy_linktest_artefacts/Release/chipboy_linktest)   # a Windows-sized stack
```

Warning-free on the ChipBoy targets. `ccache` is installed here and shares
its cache across worktrees, so a second build of JUCE costs seconds; pass the
launcher flags in every configure. GitHub Actions is not a feedback loop: a
push to `main` runs only the L1 rule check. The full build on Windows and
macOS runs when asked for (Actions → CI → Run workflow, or a `v*` tag), at
most once at the end of a round — the user builds Windows and macOS locally
and reports errors.

## Write for Windows and macOS while building on Linux

MSVC and clang differ from GCC in ways that only show up when the user builds.
Keep to this list and the platform builds stay boring:

- `long` is 32-bit on Windows: use fixed-width or `size_t`/`int64_t`; no
  `ssize_t`, no VLAs, no `alloca`, no `__attribute__`.
- Declare then `resize()` a vector instead of `std::vector<T> v(size_t(n))`
  (the most vexing parse); no designated initialisers out of order.
- Narrowing is an error in braces and a warning elsewhere: cast `char`,
  `uint8_t` and `float` explicitly (C4244 was the first Windows failure).
- `std::min`/`std::max` clash with `windows.h` macros in JUCE builds: write
  `std::min<int>(...)` or bracket the name; `M_PI` needs
  `juce::MathConstants` or `_USE_MATH_DEFINES`.
- The main thread of a Windows executable has a 1 MB stack and a host's
  message thread the same: no `Bank`, `Song`, `InstanceRegion` or large
  `std::array` as a stack local, no `cond ? *ptr : T{}` (it materialises `T`
  on the stack). The console tools link with `/STACK:8MB`; plugins cannot.
- Paths and files through `juce::File`; text through `juce::String`; no
  `char*` arithmetic on UTF-8 from the OS.
- `std::abs`/`std::pow` overloads: pick the type; `std::signal` names differ.
- Keep every compile-keeping edit warning-free with `-Wall -Wextra` here and
  assume `/W4` there.

## Licensing and layout rules

L1 Source/core links nothing and includes only `<std>` and `"core/..."`;
L2 no GPL/MPL code copied; L3 no LSDj content (ROMs, samples, manual text —
behaviour may be checked against the manual and paraphrased); L4 every
dependency in docs/LICENSING.md; L5 fonts under the OFL with their texts;
L6 the repository stays private until D1. Design lives in docs/CHIPBOY_SPEC.md,
docs/COMMANDS_AND_TEMPO.md and docs/UI_DESIGN.md; every departure goes in
CHANGES.md with why and what was considered.
