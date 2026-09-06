# CHANGES

Every departure from [`docs/CHIPBOY_SPEC.md`](docs/CHIPBOY_SPEC.md): what changed, why,
and what was considered. Deviating is expected. Deviating silently is not.

Also the place to record implementation status, so the spec stays a description of the
intended product rather than a progress report.

---

## Implementation status

| Milestone | Status |
|---|---|
| M0 — repository, spec, decisions | spec written; §18 decisions open |
| M1 — APU core + test-ROM harness | not started |
| M2 — analog stage + renderer | not started |
| M3 — main plugin shell | not started |
| M4 — bank + driver | not started |
| M5 — Voice plugin + link | not started |
| M6 — waves, frames, kits | not started |
| M7 — interface | not started |
| M8 — hardware validation | not started |

---

## Spec revisions

### 2026-09-05 — DMG *and* CGB ship in v1 (§6.5, §16.6, §17; `HARDWARE_REFERENCE.md` §11)

**Changed:** CGB moved from post-v1 to v1, resolving `[DECIDE] D8`. Added spec §6.5 on
model selection. Rewrote §16.6 around the actual capture rig. Named the host targets in
§3.1 and §16.5.

**Why:** both a DMG and a CGB are available to measure, which was the only reason CGB was
deferred. It is also the largest audible difference between models, and it is not only a
filter coefficient — CGB wave RAM is live-writable, so the wave channel *behaves*
differently: frame changes and kit streaming click on DMG and need not on CGB.

**Considered:** shipping DMG only and adding CGB later. Rejected — the capture session
costs nothing extra while the rig is set up, and retrofitting a second model after the
wave channel is built around DMG's re-trigger requirement would be more expensive than
designing for both now.

### 2026-09-05 — hardware capture tooling built (§16.6)

**Added:** `tools/capture/` — probe ROM, SM83 verifier, loopback generator, analyser,
and a synthetic-capture generator for testing the analyser; `docs/CAPTURE_GUIDE.md`.

**Notable during development**, all caught before any recording session:

- `WaveRamp` clobbered `DE`, the take interpreter's bytecode pointer, so the ROM ran
  off into garbage after take 110 and never terminated. Found by executing the ROM
  rather than trusting that it assembled.
- The original marker tones (3000/1500/750 Hz) sat exactly on harmonics of the probe
  tones (1000.6/500.3 Hz squares), so payload audio decoded as markers. Marker tones
  moved into the gaps between harmonics, and the analyser now matches candidates
  against the known take order.
- Takes 130–133 originally used a sustained DC level on the wave channel, which is
  invisible after the coupling capacitor. Rewritten as an AC ladder.
- The analyser derived DAC step sign from edge direction, which inverted every wave
  value below 7.5 — where the DAC output is genuinely negative. It now takes the sign
  from the data, and measures only DAC-ON edges.

**Known limitation:** equivalent-time sampling improves time resolution, not bandwidth.
If the amplifier's corner is above the converter's ~90 kHz limit the analyser reports a
lower bound and says so. This is audibly irrelevant and cosmetically relevant to the
oscilloscope display only.

### 2026-09-06 — capture guide: exact Reaper routing, 192 kHz, 4th Gen notes

**Changed:** `docs/CAPTURE_GUIDE.md` Steps 1-5 rewritten with the real rig.

- **192 kHz confirmed despite the DSP mixer being unavailable above 96 kHz.** The mixer
  is an input-to-output path we never needed, and losing it means the interface cannot
  route an input to an output at all — removing the only hardware feedback path.
- **Feedback is now prevented by construction, not by care:** the playback track routes
  to hardware outputs only with Master send unticked, and the record track sends nowhere
  with monitoring off. No input reaches any output regardless of what is plugged in.
- **4th Gen specifics:** Clip Safe, Auto Gain and Air must be off (all three change the
  signal during or between takes). The 4th Gen *virtual* Loopback input must NOT be used
  for calibration — it never leaves the digital domain and so skips every stage the
  calibration exists to measure, yielding a correction of ~zero that looks legitimate.

**Considered:** dropping to 96 kHz to keep the mixer. Rejected — the mixer has no role
here, and 96 kHz would halve the usable bandwidth for the edge-shape measurement.

---

## Departures from the spec

*None yet — no code has been written.*

### Format

```
### YYYY-MM-DD — short title (spec §N)

**Changed:** what the code does instead.
**Why:** the reason.
**Considered:** the alternatives, and why they lost.
```
