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
