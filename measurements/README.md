# Measurements

Analyser output from real hardware. These files are the evidence behind every
constant in `docs/HARDWARE_REFERENCE.md` §12 and are tracked deliberately — the
recordings they came from are not (hundreds of MB each; keep those elsewhere).

| Date | Rig | Notes |
|---|---|---|
| 2026-09-07 | DMG (brick) + CGB, batteries, volume max and mid; Focusrite Scarlett 18i20 4th Gen, 192 kHz, line inputs 7/8; loopback via outputs 5/6 | Second capture set. The first was invalidated by a probe-ROM fault (wave trigger delay, see `CHANGES.md`). |

Regenerate with `tools/capture/analyse.py`; see `docs/CAPTURE_GUIDE.md`.
