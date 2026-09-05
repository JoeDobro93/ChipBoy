# License

Copyright © 2026 Joe Dobro. All rights reserved.

**No licence is granted at this time.** ChipBoy is pre-release and private. The
distribution licence has not been chosen — see [`docs/LICENSING.md`](docs/LICENSING.md)
for the third-party obligations that constrain the choice, the three viable paths, and
the development rules that keep all three open.

Summary of the position:

- The project is built against **JUCE 8** (AGPLv3 or commercial) and the **VST3 SDK**
  (GPLv3 or Steinberg's proprietary agreement). Distributing binaries requires either
  publishing the source under compatible copyleft terms, or holding a commercial JUCE
  licence and a signed Steinberg VST3 licensing agreement.
- The emulation core (`Source/core`) deliberately links **no JUCE and no copyleft
  code**, so it can be licensed separately from the plugin shell if that becomes
  useful.
- **No code is copied** from any GPL-, LGPL- or MPL-licensed emulator, and **no
  LSDj-derived content** of any kind is present in this repository or in any binary
  built from it.

These are hard rules, not preferences. See spec §3.3.
