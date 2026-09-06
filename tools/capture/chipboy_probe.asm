; ChipBoy hardware probe ROM
; -------------------------------------------------------------------------
; Drives the DMG/CGB APU through a fixed sequence of "takes", each preceded
; by an audio sync marker that encodes the take id. Recorded output is
; segmented and measured by tools/capture/analyse.py.
;
; Nothing here measures DIGITAL behaviour -- blargg's dmg_sound and SameSuite
; do that far better than audio can. Every take exists to measure an ANALOG
; property: DAC transfer, coupling time constant, amplifier bandwidth,
; clipping, and the noise floor.
;
; Build:  rgbasm -o probe.o chipboy_probe.asm
;         rgblink -o chipboy_probe.gb probe.o
;         rgbfix -v -p 0xFF -t "CHIPBOYPROBE" chipboy_probe.gb
; -------------------------------------------------------------------------

DEF rNR10 EQU $FF10
DEF rNR11 EQU $FF11
DEF rNR12 EQU $FF12
DEF rNR13 EQU $FF13
DEF rNR14 EQU $FF14
DEF rNR30 EQU $FF1A
DEF rNR32 EQU $FF1C
DEF rNR33 EQU $FF1D
DEF rNR34 EQU $FF1E
DEF rNR50 EQU $FF24
DEF rNR51 EQU $FF25
DEF rNR52 EQU $FF26
DEF rWAVE EQU $FF30
DEF rLCDC EQU $FF40
DEF rSTAT EQU $FF41
DEF rSCY  EQU $FF42
DEF rSCX  EQU $FF43
DEF rLY   EQU $FF44
DEF rBGP  EQU $FF47
DEF rIE   EQU $FFFF

; Inner-loop iterations for one millisecond.
; Inner iteration = dec bc(8) + ld a,b(4) + or c(4) + jr nz(12) = 28 T-cycles.
; 4194304 / 1000 / 28 = 149.8
DEF MS_ITERS EQU 150

; Marker tone periods:  f_reg = 2048 - 131072/Hz
; Chosen to sit in the GAPS between harmonics of the probe tones (1000.6 Hz
; and 500.3 Hz). A square wave is harmonic-rich, so a marker tone landing on
; 2x or 3x a probe tone makes payloads look like markers to the analyser.
DEF TONE_HI  EQU 1990   ; ~2260 Hz  preamble  (harmonics near: 2001, 2501)
DEF TONE_ONE EQU 1943   ; ~1248 Hz  bit = 1   (harmonics near: 1001, 1501)
DEF TONE_NIL EQU 1873   ; ~ 749 Hz  bit = 0   (harmonics near:  500, 1001)

; Take bytecode opcodes
DEF OP_END       EQU 0
DEF OP_REG       EQU 1   ; db reg_lo, value
DEF OP_WAVE_FILL EQU 2   ; db nibble
DEF OP_WAVE_RAMP EQU 3
DEF OP_WAIT      EQU 4   ; dw milliseconds
DEF OP_TOGGLE    EQU 5   ; db reps, on_ms, off_ms   (toggles NR30 bit 7)
DEF OP_LCD       EQU 6   ; db 0=off 1=on
DEF OP_APU       EQU 7   ; db 0=off 1=on
DEF OP_HUSH      EQU 8   ; all four DACs off, channel registers cleared

SECTION "Header", ROM0[$100]
    nop
    jp   Start
    ds   $150 - @, 0          ; logo + header, written by rgbfix

SECTION "Main", ROM0[$150]

Start:
    di
    ld   sp, $FFFE

    call LcdOff
    ; clear VRAM so the screen is a flat field
    ld   hl, $8000
    ld   bc, $2000
.clear:
    xor  a
    ld   [hl+], a
    dec  bc
    ld   a, b
    or   c
    jr   nz, .clear

    xor  a
    ldh  [rSCY], a
    ldh  [rSCX], a
    ldh  [rBGP], a            ; index 0 -> shade 0 (white)
    ldh  [rIE], a
    call LcdOn
    call ApuOn

    ; Lead-in: 3 s of quiet so the analyser can measure the capture's own
    ; noise floor and settle the input stage before anything happens.
    call Hush
    ld   hl, 3000
    call WaitMs

    ld   de, TakeTable

TakeLoop:
    ld   a, [de]
    inc  de
    cp   $FF
    jr   z, AllDone
    ld   b, a                 ; b = take id
    call EmitMarker
    call RunTake
    jr   TakeLoop

AllDone:
    call ApuOn
    call Hush
    call LcdOn
.forever:
    ld   a, 3
    ldh  [rBGP], a
    ld   hl, 400
    call WaitMs
    xor  a
    ldh  [rBGP], a
    ld   hl, 400
    call WaitMs
    jr   .forever

; -------------------------------------------------------------------------
; Take interpreter.  DE -> bytecode, advanced past OP_END on return.
; -------------------------------------------------------------------------
RunTake:
    xor  a
    ldh  [rBGP], a            ; white while the payload plays
.next:
    ld   a, [de]
    inc  de
    and  a
    ret  z                    ; OP_END

    cp   OP_REG
    jr   z, .op_reg
    cp   OP_WAVE_FILL
    jr   z, .op_wave_fill
    cp   OP_WAVE_RAMP
    jr   z, .op_wave_ramp
    cp   OP_WAIT
    jr   z, .op_wait
    cp   OP_TOGGLE
    jr   z, .op_toggle
    cp   OP_LCD
    jr   z, .op_lcd
    cp   OP_APU
    jr   z, .op_apu
    cp   OP_HUSH
    jr   z, .op_hush
    jr   .next                ; unknown opcode: ignore

.op_reg:
    ld   a, [de]
    inc  de
    ld   c, a
    ld   a, [de]
    inc  de
    ldh  [c], a
    jr   .next

.op_wave_fill:
    ld   a, [de]
    inc  de
    call WaveFill
    jr   .next

.op_wave_ramp:
    call WaveRamp
    jr   .next

.op_wait:
    ld   a, [de]
    inc  de
    ld   l, a
    ld   a, [de]
    inc  de
    ld   h, a
    call WaitMs
    jr   .next

.op_toggle:
    ld   a, [de]
    inc  de
    ld   b, a                 ; reps
    ld   a, [de]
    inc  de
    ld   c, a                 ; on ms
    ld   a, [de]
    inc  de
    push de
    ld   d, a                 ; off ms
    call ToggleWaveDac
    pop  de
    jr   .next

.op_lcd:
    ld   a, [de]
    inc  de
    and  a
    jr   z, .lcd_off
    call LcdOn
    jr   .next
.lcd_off:
    call LcdOff
    jr   .next

.op_apu:
    ld   a, [de]
    inc  de
    and  a
    jr   z, .apu_off
    call ApuOn
    jr   .next
.apu_off:
    xor  a
    ldh  [rNR52], a
    jr   .next

.op_hush:
    call Hush
    jr   .next

; -------------------------------------------------------------------------
; ToggleWaveDac -- B reps of (NR30=$80 for C ms, NR30=$00 for D ms)
; Used both for DC step measurements and for equivalent-time edge sampling.
; -------------------------------------------------------------------------
; NR33/NR34 are driven to f = 2047 here, NOT f = 0. On trigger the wave
; channel's sample buffer holds its PREVIOUS value until the first timer
; expiry, and that period is (2048 - f) * 2 cycles. At f = 0 that is 976 us,
; so the DAC-on step lands on the stale buffer and the real sample only
; appears a millisecond later -- identical for every wave value, which
; destroys the transfer measurement. At f = 2047 the delay is 0.5 us.
ToggleWaveDac:
.loop:
    push bc
    push de
    ld   a, $80
    ldh  [rNR30], a
    ld   a, $87               ; trigger, frequency MSB = 7
    ldh  [rNR34], a
    ld   h, 0
    ld   l, c
    call WaitMs
    pop  de
    push de
    xor  a
    ldh  [rNR30], a
    ld   h, 0
    ld   l, d
    call WaitMs
    pop  de
    pop  bc
    dec  b
    jr   nz, .loop
    ret

; -------------------------------------------------------------------------
; Sync marker.  B = take id.
;   preamble: 2 x (3 kHz for 40 ms, silence 40 ms)
;   payload:  8 bits MSB first, 1500 Hz = 1 / 750 Hz = 0, 25 ms each,
;             separated by 15 ms of silence
;   trailer:  120 ms of silence, so the take starts from a settled baseline
; -------------------------------------------------------------------------
EmitMarker:
    push bc
    ld   a, 3
    ldh  [rBGP], a            ; black while the marker plays
    call ApuOn

    ld   c, 2
.preamble:
    push bc
    ld   bc, TONE_HI
    ld   a, 40
    call ToneMs
    ld   hl, 40
    call MarkerGap
    pop  bc
    dec  c
    jr   nz, .preamble

    pop  bc
    push bc
    ld   c, 8
.bits:
    push bc
    ld   a, b
    and  $80
    jr   z, .zero
    ld   bc, TONE_ONE
    jr   .emit
.zero:
    ld   bc, TONE_NIL
.emit:
    ld   a, 25
    call ToneMs
    ld   hl, 15
    call MarkerGap
    pop  bc
    sla  b
    dec  c
    jr   nz, .bits

    ld   hl, 120
    call MarkerGap
    pop  bc
    ret

; Play PU1 at frequency BC for A milliseconds, then silence it.
ToneMs:
    push af
    ld   a, $80
    ldh  [rNR12], a           ; keep DAC alive across the retrigger
    ld   a, $80
    ldh  [rNR11], a           ; duty 50%, length 0
    ld   a, $F0
    ldh  [rNR12], a           ; volume 15, no envelope
    ld   a, c
    ldh  [rNR13], a
    ld   a, b
    and  $07
    or   $80                  ; trigger
    ldh  [rNR14], a
    pop  af
    ld   h, 0
    ld   l, a
    call WaitMs
    xor  a
    ldh  [rNR12], a           ; DAC off -> silence
    ldh  [rNR14], a
    ret

MarkerGap:
    call WaitMs
    ret

; -------------------------------------------------------------------------
; Helpers
; -------------------------------------------------------------------------

; Power the APU up, all channels routed both sides, master volume 7/7.
ApuOn:
    xor  a
    ldh  [rNR52], a           ; off first: clears every register
    ld   a, $80
    ldh  [rNR52], a
    ld   a, $FF
    ldh  [rNR51], a
    ld   a, $77
    ldh  [rNR50], a
    ret

; Silence every channel by clearing its DAC.
Hush:
    xor  a
    ldh  [rNR12], a
    ldh  [$FF17], a           ; NR22
    ldh  [rNR30], a
    ldh  [$FF21], a           ; NR42
    ldh  [rNR14], a
    ldh  [$FF19], a           ; NR24
    ldh  [rNR34], a
    ldh  [$FF23], a           ; NR44
    ret

; Fill all 32 wave samples with the nibble in A.
WaveFill:
    and  $0F
    ld   b, a
    swap a
    or   b
    ld   hl, rWAVE
    ld   c, 16
.loop:
    ld   [hl+], a
    dec  c
    jr   nz, .loop
    ret

; Fill wave RAM with a 32-step triangle: 0..15, 15..0.
; DE is the take interpreter's bytecode pointer, so it is preserved here.
WaveRamp:
    push de
    ld   hl, rWAVE
    ld   de, WaveRampData
    ld   c, 16
.loop:
    ld   a, [de]
    inc  de
    ld   [hl+], a
    dec  c
    jr   nz, .loop
    pop  de
    ret

WaveRampData:
    db $01,$23,$45,$67,$89,$AB,$CD,$EF
    db $FE,$DC,$BA,$98,$76,$54,$32,$10

; Wait HL milliseconds.
WaitMs:
.outer:
    ld   a, h
    or   l
    ret  z
    ld   bc, MS_ITERS
.inner:
    dec  bc
    ld   a, b
    or   c
    jr   nz, .inner
    dec  hl
    jr   .outer

LcdOff:
    ldh  a, [rLCDC]
    and  $80
    ret  z                    ; already off
.wait:
    ldh  a, [rLY]
    cp   144
    jr   c, .wait
    xor  a
    ldh  [rLCDC], a
    ret

LcdOn:
    ld   a, $91               ; LCD on, BG on, tiles at $8000
    ldh  [rLCDC], a
    ret

; -------------------------------------------------------------------------
; Take table
; -------------------------------------------------------------------------

MACRO take
    db \1
ENDM
MACRO reg
    db OP_REG, LOW(\1), \2
ENDM
MACRO wfill
    db OP_WAVE_FILL, \1
ENDM
MACRO wramp
    db OP_WAVE_RAMP
ENDM
MACRO pause
    db OP_WAIT
    dw \1
ENDM
MACRO toggle
    db OP_TOGGLE, \1, \2, \3
ENDM
MACRO lcd
    db OP_LCD, \1
ENDM
MACRO apu
    db OP_APU, \1
ENDM
MACRO hush
    db OP_HUSH
ENDM
MACRO endtake
    db OP_END
ENDM

; ~1000 Hz and ~500 Hz on a pulse channel
DEF F1K   EQU 1917
DEF F500  EQU 1786

TakeTable:

; --- 1..4  noise floor -----------------------------------------------------
    take 1
    apu 0
    pause 2000
    endtake

    take 2
    apu 1
    hush
    pause 2000
    endtake

    take 3
    lcd 0
    apu 0
    pause 2000
    lcd 1
    endtake

    take 4
    lcd 0
    apu 1
    hush
    pause 2000
    lcd 1
    endtake

; --- 10..25  wave DC step: DAC transfer + coupling time constant -----------
FOR i, 0, 16
    take 10 + i
    apu 1
    wfill i
    reg rNR32, $20            ; 100%
    reg rNR33, $FF
    toggle 6, 120, 120
    endtake
ENDR

; --- 30..37  master volume ladder -----------------------------------------
FOR v, 0, 8
    take 30 + v
    apu 1
    reg rNR50, (v << 4) | v
    wfill 15
    reg rNR32, $20
    reg rNR33, $FF
    toggle 4, 150, 150
    endtake
ENDR

; --- 40..43  wave output level (NR32) -------------------------------------
FOR n, 0, 4
    take 40 + n
    apu 1
    wfill 15
    reg rNR32, n << 5
    reg rNR33, $FF
    toggle 4, 150, 150
    endtake
ENDR

; --- 50..65  pulse DAC transfer (volume ladder, envelope disabled) --------
FOR lvl, 0, 16
    take 50 + lvl
    apu 1
    reg rNR11, $80            ; duty 50%
    reg rNR12, lvl << 4         ; volume l, no envelope; l=0 disables the DAC
    reg rNR13, LOW(F1K)
    reg rNR14, $80 | HIGH(F1K)
    pause 600
    hush
    endtake
ENDR

; --- 70..73  duty cycles ---------------------------------------------------
FOR dty, 0, 4
    take 70 + dty
    apu 1
    reg rNR11, dty << 6
    reg rNR12, $F0
    reg rNR13, LOW(F500)
    reg rNR14, $80 | HIGH(F500)
    pause 600
    hush
    endtake
ENDR

; --- 80..86  envelope decay, rates 1..7 -----------------------------------
FOR p, 1, 8
    take 79 + p
    apu 1
    reg rNR11, $80
    reg rNR12, $F0 | p        ; start 15, direction down
    reg rNR13, LOW(F1K)
    reg rNR14, $80 | HIGH(F1K)
    pause 2200
    hush
    endtake
ENDR

; --- 87..93  envelope rise, rates 1..7 ------------------------------------
FOR p, 1, 8
    take 86 + p
    apu 1
    reg rNR11, $80
    reg rNR12, $08 | p        ; start 0, direction up (DAC stays on)
    reg rNR13, LOW(F1K)
    reg rNR14, $80 | HIGH(F1K)
    pause 2200
    hush
    endtake
ENDR

; --- 100..103  pitch cross-check ------------------------------------------
MACRO pitch_take
    take \1
    apu 1
    reg rNR11, $80
    reg rNR12, $F0
    reg rNR13, LOW(\2)
    reg rNR14, $80 | HIGH(\2)
    pause 500
    hush
    endtake
ENDM
    pitch_take 100, 0
    pitch_take 101, 1024
    pitch_take 102, 1750
    pitch_take 103, 2017

; --- 110..111  wave channel rate cross-check ------------------------------
MACRO wave_take
    take \1
    apu 1
    wramp
    reg rNR32, $20
    reg rNR30, $80
    reg rNR33, LOW(\2)
    reg rNR34, $80 | HIGH(\2)
    pause 500
    hush
    endtake
ENDM
    wave_take 110, 1750
    wave_take 111, 2017

; --- 120..123  noise channel ----------------------------------------------
MACRO noise_take
    take \1
    apu 1
    reg $FF21, $F0            ; NR42 volume 15, no envelope
    reg $FF22, \2             ; NR43
    reg $FF23, $80            ; NR44 trigger
    pause 500
    hush
    endtake
ENDM
    noise_take 120, $00       ; shift 0, 15-bit, divisor 8
    noise_take 121, $40       ; shift 4, 15-bit
    noise_take 122, $48       ; shift 4, 7-bit
    noise_take 123, $07       ; shift 0, divisor 112

; --- 130..134  summing ladder and clipping --------------------------------
; Every source here must be AC. A sustained DC level is invisible after the
; coupling capacitor, so a "wave channel held at 15" take measures nothing.
; The ladder adds one AC source at a time; sub-linear peak growth is clipping.
MACRO ladder_head
    take \1
    apu 1
    reg rNR11, $C0            ; PU1 duty 75%
    reg rNR12, $F0
    reg rNR13, LOW(F1K)
    reg rNR14, $80 | HIGH(F1K)
ENDM
MACRO ladder_pu2
    reg $FF16, $C0            ; NR21 duty 75%
    reg $FF17, $F0            ; NR22
    reg $FF18, LOW(F500)      ; NR23
    reg $FF19, $80 | HIGH(F500)
ENDM
MACRO ladder_noise
    reg $FF21, $F0            ; NR42
    reg $FF22, $40            ; NR43 shift 4
    reg $FF23, $80            ; NR44 trigger
ENDM

    ladder_head 130
    pause 500
    hush
    endtake

    ladder_head 131
    ladder_pu2
    pause 500
    hush
    endtake

    ladder_head 132
    ladder_pu2
    ladder_noise
    pause 500
    hush
    endtake

    ladder_head 133
    ladder_pu2
    ladder_noise
    wramp                     ; wave: triangle at ~250 Hz, an AC source
    reg rNR32, $20
    reg rNR30, $80
    reg rNR33, LOW(F500)
    reg rNR34, $80 | HIGH(F500)
    pause 800
    hush
    endtake

    take 134
    apu 1
    reg rNR50, $00            ; master volume 0 -- confirms it is 1/8, not mute
    wfill 15
    reg rNR32, $20
    reg rNR33, $FF
    toggle 4, 200, 200
    endtake

; --- 140..141  edge shape, equivalent-time sampling -----------------------
    take 140
    apu 1
    wfill 15
    reg rNR32, $20
    reg rNR33, $FF
    toggle 250, 4, 4
    endtake

    take 141
    apu 1
    reg rNR50, $33
    wfill 15
    reg rNR32, $20
    reg rNR33, $FF
    toggle 250, 4, 4
    endtake

    db $FF                    ; end of table
