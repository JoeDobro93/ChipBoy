---
name: review
description: Review the current change against its design section. Reports correctness bugs, spec deviations and missing tests as a prioritised list; edits nothing.
disable-model-invocation: true
argument-hint: "[base ref, default origin/main] [design section, e.g. §35]"
allowed-tools: Bash(git diff *) Bash(git log *) Bash(git status *) Read Grep Glob
---
Review the change without editing any file.

1. The diff: `git diff <base> -- . ':!Demo' ':!docs/screenshots'` plus the uncommitted
   working tree, where `<base>` is the first argument or `origin/main`. Demo and
   screenshot regenerations are checked for presence, not read.
2. The design: the section of `docs/COMMANDS_AND_TEMPO.md` the change names (the second
   argument, or the newest section the diff adds), and any `docs/plan-*.md` the change
   references. Do not survey other docs.
3. Report, most severe first, each item with file:line and a one-sentence failure case:
   - correctness bugs (wrong behaviour, races between the audio and message threads,
     stack-sized locals, narrowing, Windows/macOS portability per `CLAUDE.md`);
   - deviations from the design section or the hardware audit (`docs/HARDWARE_DRIVER_AUDIT.md`);
   - missing tests (a behaviour the section specifies that `Tests/` does not pin);
   - missing paperwork (CHANGES entry, HANDOFF update, regenerated demos when the
     parameter table or a song format changed).
4. Under 300 words. No praise, no restating the diff. Do not fix anything.

Arguments: $ARGUMENTS
