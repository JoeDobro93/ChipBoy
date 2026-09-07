# ChipBoy — Licensing

Companion to [`CHIPBOY_SPEC.md`](CHIPBOY_SPEC.md) §3.3. This document exists because the
licence choice has to be made *before* release but constrains development *from the
start*: a single copy-paste from a GPL emulator core closes the commercial path
permanently.

Nothing here is legal advice. Before any public release, the terms of every row in §1
should be re-read at source — they change — and, for the commercial path, checked with
a lawyer.

---

## 1. Third-party obligations

| Component | Licence | What it obliges |
|---|---|---|
| **JUCE 8** | AGPLv3 **or** commercial | AGPL: the whole plugin becomes AGPLv3 and source must be published. Commercial: a paid licence, with a free tier below a revenue threshold. **Verify the current tiers and threshold at juce.com — they have changed more than once.** |
| **VST3 SDK** (bundled by JUCE) | GPLv3 **or** Steinberg proprietary | GPLv3: the plugin must be GPLv3-compatible (AGPLv3 is, via GPLv3 §13). Proprietary: sign and register Steinberg's VST3 Licensing Agreement. It is free of charge but has compliance terms, including logo and naming requirements. |
| **AudioUnit** (macOS) | Apple SDK, part of Xcode | No fee, no separate agreement. Distribution follows Apple's normal developer terms. |
| **CLAP** | MIT | Nothing. `clap-juce-extensions` is also permissive. **This is the only plugin format with no strings attached** — see §3. |
| **Catch2** | BSL-1.0 | Permissive, and test-only — never linked into a shipped binary. |
| **RGBDS** | MIT | Assembler used at build time for the probe ROM and SameSuite. A tool, never linked; optional (its tests are skipped without it). |
| **nlohmann/json** (if used) | MIT | Retain the notice. |

### Reference material — read, do not copy

| Source | Licence | Rule |
|---|---|---|
| **Pan Docs** (gbdev) | CC BY 4.0 | Facts and register layouts are not copyrightable. Attribute if prose or tables are reproduced verbatim. |
| **SameBoy** | MIT | The best-documented accurate core, and the safest to study. If any code is genuinely copied, retain the MIT notice and record it in this file. |
| **Gambatte** | GPLv2 | **Read only.** Copying any of it closes the commercial path. |
| **mGBA** | MPL-2.0 | **Read only.** File-level copyleft; a copied file stays MPL forever. |
| **blargg's `dmg_sound` test ROMs** | freely distributed | Used for validation. Fetched at configure time from the `retrio/gb-test-roms` mirror into git-ignored `TestRoms/`, never committed (spec §16.1). |
| **SameSuite** | X11/MIT (LIJI32) | Same: fetched as source into `TestRoms/`, assembled with RGBDS, not vendored. |

### LSDj

LSDj is proprietary software by Johan Kotlinski. ChipBoy borrows its *sound-design
vocabulary* — instruments, tables, waves, frames, kits — which is a set of ideas, not
protectable expression.

Two hard rules, which apply now and to any future interoperability work (spec §15):

1. **No LSDj-derived content ships.** Not its factory waves, kits, presets, instrument
   defaults, ROM data, or documentation text. Import reads *the user's own files*.
2. **File formats may be implemented** from public documentation and observation. A
   format is not a copyrightable work. Do not lift code from `lsdpatch` or any other
   GPL-licensed LSDj tool to do it.

---

## 2. The three paths

### Path A — Free and open (AGPLv3)

Ship under AGPLv3, using JUCE's AGPL option and the VST3 SDK under GPLv3.

- No fees, no agreements, no revenue reporting.
- Source must be published, including any future changes.
- Anyone may fork and redistribute; nobody may make it closed-source.
- **Reversible by the copyright holder**, provided the copyright holder is still the
  sole author. See §4.

### Path B — Commercial, closed source

- Requires a **JUCE commercial licence** (free below their revenue threshold, paid
  above it).
- Requires a **signed Steinberg VST3 Licensing Agreement** for the VST3 build.
- Requires that no copyleft code has ever entered the tree — hence §3.
- macOS notarisation and code signing become mandatory in practice (an Apple Developer
  Program membership), and Windows users will hit SmartScreen without an EV certificate.

### Path C — Open core

`Source/core` — the APU, driver, analog stage, renderer and link transport, none of
which touch JUCE — published under a permissive licence (MIT or Apache-2.0). The plugin
shell stays proprietary under Path B, or is published under Path A.

This is the reason the spec's layering rule exists (§3.3 L1). It costs nothing to keep
available and it is the only path that gets the accuracy work reviewed by people who
care about accuracy, which is worth more to this particular project than to most.

**Recommendation: keep Path C available, decide between A and B at release.** The
architecture required for C is the architecture that makes the core unit-testable
anyway, so there is no tax.

---

## 3. Format strategy

VST3 is the only format that carries a licensing obligation, and it is the one nobody
can ship without. Two things follow:

- **CLAP costs nothing and should be included.** It is MIT, needs no agreement, and its
  audience overlaps heavily with the audience for a Game Boy emulation plugin.
- **AAX is out of scope.** It requires Avid approval and PACE signing, both of which are
  disproportionate here.

---

## 4. Development rules

These hold regardless of which path is chosen, because breaking them removes a path.

1. **`Source/core` links no JUCE and no copyleft code.** Plain C++20 and the standard
   library. Enforced by the build: the core target links no JUCE modules, and this is
   checked in CI.
2. **No code is copied from any GPL/LGPL/MPL emulator.** Read them to understand the
   hardware, then implement from documentation and from measurements. When a reference
   implementation resolves a question, cite it in a comment — the citation is the proof
   that the behaviour was understood rather than transplanted.
3. **No LSDj-derived content in the repository or in any binary.**
4. **Every third-party dependency is recorded in §1 before it is added**, with its
   licence. A dependency that is not in that table does not go in.
5. **Outside contributions require a CLA, or are not accepted.** Path A is only
   reversible while there is one copyright holder. A single unassigned pull request
   permanently forecloses Path B.
6. **The repository stays private until the licence is chosen.** Publishing under no
   licence is worse than either path: it grants nothing while forfeiting the ability to
   claim the work was ever confidential.

---

## 5. Decision log

| Date | Decision | Notes |
|---|---|---|
| 2026-09-05 | Deferred. Repo private, all rights reserved. | Rules in §4 adopted so that A, B and C all remain open. Tracked as `[DECIDE] D1` in spec §18. |
