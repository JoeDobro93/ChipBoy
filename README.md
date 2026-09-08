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

![The ChipBoy window](docs/screenshots/main-instrument.png)

## Two plugins

| | |
|---|---|
| **ChipBoy** | The chip. One instance is one complete DMG APU: four channels, stereo out, and the instrument bank. Plays MIDI directly if it is the only thing loaded. |
| **ChipBoy Voice** | A control surface for one channel, on its own DAW track. Produces no audio; sends notes and parameters to a linked ChipBoy instance so each channel gets its own piano roll and its own automation lanes. |

Four voices per instance, always. If you want more, load another instance.

## Status

**v1 is built.** Everything the workshop decided ships: the cycle-exact APU with the
DMG and CGB chip variants, the measured analog stage with a RAW bypass, the LSDj-shaped
bank (instruments, tables, waves and frames, kits), a driver with its own tick, a
tracker that follows the host transport and records, two plugins linked through shared
memory, the Hardware panel's options, and the window from the mockup with its
visualizer. It has been compiled and tested on Linux (61 core tests, the link
integration test, VST3 and Standalone builds); the Windows and macOS builds run in CI
but have not yet been played in a DAW.

**Revised on 2026-09-07:** the channel is now a visible tracker row — an instrument, a
table and two command slots — instead of a row of lanes that silently overrode the
instrument, and the driver's tick is fixed at 24 to the beat with one switch for whose
beat it is, the host's or the song's. The design is
[`docs/COMMANDS_AND_TEMPO.md`](docs/COMMANDS_AND_TEMPO.md) and it supersedes the spec
where they differ; projects and banks from before it are not migrated.

Expect first-run bugs; the documents remain the source of truth:

- [`docs/CHIPBOY_SPEC.md`](docs/CHIPBOY_SPEC.md) — the build specification, and the source of truth.
- [`docs/COMMANDS_AND_TEMPO.md`](docs/COMMANDS_AND_TEMPO.md) — commands, the channel's lanes and the tempo model; supersedes the spec where they differ.
- [`docs/UI_DESIGN.md`](docs/UI_DESIGN.md) — the interface, its reasons, and the decisions taken.
- [`docs/HARDWARE_REFERENCE.md`](docs/HARDWARE_REFERENCE.md) — DMG APU registers, timing, and the measured analog behaviour the emulation has to reproduce.
- [`docs/LICENSING.md`](docs/LICENSING.md) — third-party obligations and the licence decision still to be made.
- [`docs/CAPTURE_GUIDE.md`](docs/CAPTURE_GUIDE.md) — step-by-step procedure for measuring a real DMG and CGB.
- [`CHANGES.md`](CHANGES.md) — every departure from the spec, with reasons, and what is deferred.
- [`Demo/`](Demo/) — a Reaper project and a MIDI file that play a short tune through all four channels.

Open decisions are collected in spec §18 and marked `[DECIDE]` throughout.

## Building the plugins

Requirements: CMake 3.24+, a C++20 compiler (Visual Studio 2022 on Windows, Xcode 15+ on
macOS, GCC 13 / Clang 16 on Linux) and git. The first configure fetches JUCE 8.0.15
from GitHub, so it needs the network once.

**Windows** (a Developer Command Prompt or PowerShell):

```
git clone https://github.com/JoeDobro93/ChipBoy.git
cd ChipBoy
cmake -S . -B build -DCHIPBOY_BUILD_PLUGIN=ON -DCHIPBOY_BUILD_TESTS=OFF
cmake --build build --config Release --target ChipBoy_VST3 ChipBoyVoice_VST3 ChipBoy_Standalone
```

The outputs:

| Target | Path |
|---|---|
| ChipBoy VST3 | `build\ChipBoy_artefacts\Release\VST3\ChipBoy.vst3` |
| ChipBoy Voice VST3 | `build\ChipBoyVoice_artefacts\Release\VST3\ChipBoy Voice.vst3` |
| Standalone | `build\ChipBoy_artefacts\Release\Standalone\ChipBoy.exe` |

Copy the two `.vst3` folders into `C:\Program Files\Common Files\VST3\` and rescan in
the DAW (FL Studio: Options → Manage plugins → Find plugins; Reaper: Preferences →
Plug-ins → VST → Re-scan). Configuring with `-DCHIPBOY_COPY_PLUGINS=ON` copies them
after every build instead, which on Windows needs a prompt with write access to that
folder. Visual Studio is a multi-configuration generator: always pass `--config Release`
to the build command, or you get a slow Debug build in `build\...\Debug\`.

**macOS**:

```
cmake -S . -B build -DCHIPBOY_BUILD_PLUGIN=ON -DCHIPBOY_BUILD_TESTS=OFF
cmake --build build --config Release --target ChipBoy_VST3 ChipBoy_AU ChipBoyVoice_VST3 ChipBoyVoice_AU ChipBoy_Standalone
```

Outputs land in the same `*_artefacts/Release/` folders; VST3s go to
`~/Library/Audio/Plug-Ins/VST3`, components to `~/Library/Audio/Plug-Ins/Components`
(`-DCHIPBOY_COPY_PLUGINS=ON` does this). The build signs ad hoc; Logic may need
`killall -9 AudioComponentRegistrar` and a restart to see a new component.

**Linux**: install `libasound2-dev libfreetype6-dev libfontconfig1-dev libx11-dev
libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libgl1-mesa-dev
libcurl4-openssl-dev`, then the same commands; the VST3 goes to `~/.vst3`.

Two console tools come with the plugin build: `chipboy_paramdump` prints the
host-visible parameter list in index order (the demo generator checks itself against
it), and `chipboy_linktest` runs both plugins in one process through a link region and
checks the whole path (`ctest --test-dir build -C Release -R linktest`).

## Playing it

![The Phrases tab](docs/screenshots/main-phrases.png)

1. Put **ChipBoy** on a track and play. PU1 answers every MIDI channel (omni); PU2, WAV
   and NOI answer MIDI channels 2, 3 and 4. Change any channel's source in its strip.
2. **A channel is a tracker row**, and its automation lanes are that row: *Instrument*,
   *Table*, *Level*, *Pan*, *Transpose*, two **command slots**, *Live follow*,
   *Velocity* and *Keyswitches*. That is the whole list. The instrument holds the sound —
   there are no lanes quietly overriding its duty, envelope, sweep, wave, frame, vibrato,
   arpeggio or detune. Each note takes the values in force when it starts, or follows
   them live with *Live follow*.
3. **The two command slots** are LSDj's letters with base-10 arguments: `W` duty on a
   pulse channel and the wave slot on WAV, `E` envelope, `V` vibrato, `A` table, `F`
   frame, `C` chord, `L` slide, `R` retrigger, `P` pitch offset, `O` pan, `S` PU1's
   sweep, `K` kill, `D` delay, `M` master volume, `G` groove, `T` tempo, `Z` random. A
   slot is three parameters — the letter, `x` and `y` — and it is *in force*, not
   momentary: it fires at the next tick when one of the three changes, and again at every
   note-on after the instrument and its table. Put the letter back to *none* and what it
   changed reverts to the instrument's own value. The strip shows the meaning
   ("vol 12 · down 3"), never a packed byte, with the running state under it, so an
   automation move is visible as the slot changing and the state following.
4. Velocity sets the envelope's start volume — or selects an instrument, or is ignored,
   per channel — the mod wheel sets vibrato depth, and pitch bend moves the period.
5. **Tempo.** Ticks, which tables, vibrato, wave frames and tracker steps all run on, are
   always 24 to the beat. *Tempo source* (in the header bar) chooses whose beat:
   **Host**, the default, where a tick sits at every multiple of 1/24 of the host's beat
   and scrubbing is exact; or **Song**, where the plugin keeps its own *Song tempo*
   (40–255 BPM, automatable) with `T` commands over it, and the host's bars are only a
   ruler. *Quantize* (default off) holds note-ons and note-offs until the next tick, for
   the tracker's feel; bends and controllers are never quantised, and tracker cells are
   always on ticks.
6. For one track per voice: put a **ChipBoy Voice** on another track, turn **Link mode**
   on in ChipBoy's Link tab (the host re-compensates for one block of latency), and
   pick the instance and channel in the Voice. The Voice's track stays silent; the audio
   comes out of the ChipBoy track. Its parameters are the same set, as automation lanes
   where you expect them.
7. **Keyswitches** (per channel, off by default): notes 24–35 on a pulse channel and
   12–23 on the wave and noise channels select instrument slots 1–12 without sounding.
8. The **Phrases** tab is a tracker on that same clock. Set a channel to *Trk* to play
   its lane; arm *Rec* to write what you play — the note, the instrument, the table and
   both command slots as they stand at each step — into the cells, so a recorded song
   carries its own tempo and groove.
9. The **Hardware** tab holds the model switch (DMG / CGB / RAW), the hardware states
   (headphone noise, LCD line, CGB bass mod, volume writes at edges) and the two
   departures (de-click, soften master pops), which light the MODIFIED badge.
10. `Demo/ChipBoy Demo.rpp` opens in Reaper with the tune and its automation, and
    `Demo/ChipBoy Demo (song tempo).rpp` runs the same track on the song's clock at 150
    BPM with a `T` that drops it to 100 for four bars; the tune alone is in
    `Demo/chipboy_demo.mid` for any other host (see `Demo/README.md`).

## Building the core and its tests

The emulation core has no dependency on JUCE and builds on its own. Test ROMs are
fetched at configure time into the git-ignored `TestRoms/`; SameSuite is assembled from
source if RGBDS is on the `PATH`, and skipped with a message otherwise.

```
cmake -S . -B build-core -DCMAKE_BUILD_TYPE=Release
cmake --build build-core --config Release --parallel
ctest --test-dir build-core -C Release --output-on-failure
```

`build-core/chipboy_runrom <rom.gb> [seconds]` runs one test ROM and prints what it
reports.

## Hearing it without a DAW

```
build-core/chipboy_demo tune.wav                 # a built-in tune, DMG, noise floor on
build-core/chipboy_demo tune.wav --cgb           # the same tune through the CGB analog constants
build-core/chipboy_demo tune.wav --no-noise      # Headphone Noise off
build-core/chipboy_runrom game.gb 20 --wav out.wav   # any ROM's audio, run headless
```

Output is 16-bit stereo at 48 kHz (`--rate` changes it). The tune ends every note the
way a DMG driver has to — by disabling the DAC — so what you hear at each re-trigger is
the hardware's own click, not an effect.

## Troubleshooting

- **The JUCE fetch fails** (a proxy, no network): clone JUCE 8.0.15 yourself and pass
  `-DFETCHCONTENT_SOURCE_DIR_JUCE=<path>` to the configure step.
- **The DAW does not find the plugin**: check the folder above, rescan, and on Windows
  make sure you built `Release` (the Debug build goes elsewhere).
- **A Voice shows "waiting" forever**: Link mode must be on in the ChipBoy instance, and
  both plugins must run as the same user. The link lives in the temp folder under
  `chipboy-link/`; after a host crash a stale `.cbl` file there can be deleted.
- **A Voice shows "channel busy"**: another Voice holds that channel; the first keeps it.
- **It pops when I change volume**: it is supposed to; turn on *Volume writes at edges*
  or, for a departure from the hardware, *De-click*.

## Planned targets

JUCE 8 / C++20 / CMake. VST3, AU (macOS) and Standalone on Windows 10+ x64 and
macOS 11+ (universal arm64 + x86_64). CLAP under consideration.

## License

Not yet chosen — see [`docs/LICENSING.md`](docs/LICENSING.md). The repository is
private and the code is written so that every option stays open: the emulation core
links no JUCE and no copyleft code, and nothing derived from LSDj enters the tree.
