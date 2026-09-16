# Plan: any instrument on any channel, read as LSDj reads it (§197)

## Why

LSDj keeps every instrument as sixteen bytes and lets a channel read whatever instrument a
cell names: a WAV instrument on PU1 is read as a pulse (byte 1 its envelope, byte 7 its duty),
a pulse on NOI as a noise instrument, and songs use this for effects. ChipBoy's instruments
carry a type, and until now the driver replaced a mismatched instrument with the channel's
default; the importer worked around it by making a per-channel copy ("variant") in a spare
slot. The user's decision: ChipBoy supports the behaviour itself, with LSDj's byte mapping, and
the copies go.

## Data

- `InstrumentCore::lsdjFormat` (`int8_t`, -1 none) and `lsdjBytes` (16 bytes): the instrument as
  its save held it, set by the importer. In the file as `lsdjFormat` and a 32-digit `lsdjBytes`.
- Nothing else changes for a native instrument.

## Functions

- `lsdj::decodeInstrumentBytes(b, kind, model, bank, tickMs, out, notes, name)` in
  `core/Import/LsdjInstrument.cpp`: the importer's `buildInstrument` body for kinds 0 (pulse),
  1 (wave) and 3 (noise), moved; kind 2 fills the shared fields and leaves the kit to the
  importer (kits need the ROM). The envelope law moves with it (`EnvLaw`).
- `lsdj::encodeInstrumentBytes(core, out)`: the 9.4.2 layout for a native instrument, lossy
  where ChipBoy has more than LSDj (a Shaped envelope encodes as its Chip fields).
- `Driver::crossKind(ch, core)`: when `typeFits` fails, decode the instrument's own bytes
  (imported) or its encoding (native) as the channel's kind (PU1/PU2 pulse, WAV wave, NOI
  noise), under `lsdjModelForFormat(lsdjFormat)` or the latest model.
- The importer: `slotFor(ins, ch)` is `ins + 1`; `variantSlot`, `nextVariant` and the variant
  loop go; the bank's noise map is set up front for a Map model so a cross-kind noise decode
  finds it.

## Edge cases

- A kit on PU1/PU2/NOI decodes as pulse/noise from its bytes (kit numbers unused there).
- A pulse or noise instrument on WAV is a wave: byte 3 (length / shape) becomes synth and
  frame under the 9.x layout, byte 9 PLAY, byte 10 length, byte 11 speed.
- A native instrument's encoding uses the 9.4.2 layout whatever the bank's other instruments.
- The Instrument tab keeps showing the instrument's own fields; the driver's view is derived.

## Tests

- Driver: a native pulse on WAV writes NR30/NR32 (level from its envelope byte) and no NR12; a
  wave with imported bytes on PU1 writes NR12 from its byte 1.
- Import: a WAV instrument named on PU1 keeps its own slot in the cell, no copy is made, the
  instrument carries its format and bytes.
- The `X942_*` probes (a WAV/KIT/NOI instrument on PU1, a pulse on WAV and NOI, a WAV on NOI)
  against the 9.4.2 ROM, now through the driver's decode.
