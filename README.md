# ChipBoy — Game Boy DMG sound chip instrument

ChipBoy is a **Game Boy sound chip**, not a chiptune synth. Every sound it makes is
one an original DMG could make, produced by the same mechanism: a cycle-accurate
emulation of the four APU channels feeding a model of the DMG's analog output stage —
the 4-bit DACs, the shared summing amp, and the coupling capacitor that puts the
droop on every square wave and the click on every note.

The restrictions are the product. Four monophonic voices. Volume in sixteen steps with
eight envelope rates. Pitch as an 11-bit period, out of tune at the top of the range.
Panning that is left, right, both, or off. There is no fine gain control inside the
chip, because there isn't one in the chip.

Sound design follows LSDj's vocabulary — instruments, tables, waves and frames, kits —
with values shown in base 10 instead of hex, and the same hardware constraints behind
them.

## Two plugins

| | |
|---|---|
| **ChipBoy** | The chip. One instance is one complete DMG APU: four channels, stereo out, and the instrument bank. Plays MIDI directly if it is the only thing loaded. |
| **ChipBoy Voice** | A control surface for one channel, on its own DAW track. Produces no audio; sends notes and parameters to a linked ChipBoy instance so each channel gets its own piano roll and its own automation lanes. |

Four voices per instance, always. If you want more, load another instance.

## Status

**Planning.** No code yet. The specification is the deliverable:

- [`docs/CHIPBOY_SPEC.md`](docs/CHIPBOY_SPEC.md) — the build specification, and the source of truth.
- [`docs/HARDWARE_REFERENCE.md`](docs/HARDWARE_REFERENCE.md) — DMG APU registers, timing, and the measured analog behaviour the emulation has to reproduce.
- [`docs/LICENSING.md`](docs/LICENSING.md) — third-party obligations and the licence decision still to be made.
- [`docs/CAPTURE_GUIDE.md`](docs/CAPTURE_GUIDE.md) — step-by-step procedure for measuring a real DMG and CGB.
- [`CHANGES.md`](CHANGES.md) — every departure from the spec, with reasons.

The hardware capture tooling in [`tools/capture/`](tools/capture/) is built and tested:
a probe ROM, an SM83 interpreter that verifies it, a loopback calibration generator, and
an analyser that turns a recording into measured constants.

Open decisions are collected in spec §18 and marked `[DECIDE]` throughout.

## Planned targets

JUCE 8 / C++20 / CMake. VST3, AU (macOS) and Standalone on Windows 10+ x64 and
macOS 11+ (universal arm64 + x86_64). CLAP under consideration.

## License

Not yet chosen — see [`docs/LICENSING.md`](docs/LICENSING.md). The repository is
private and the code is written so that every option stays open: the emulation core
links no JUCE and no copyleft code, and nothing derived from LSDj enters the tree.
