# KINETIC — User Manual

KINETIC is a sample-accurate, pure MIDI FX step sequencer for macOS. It generates MIDI inside
the host's audio callback from the host's own transport position; nothing is scheduled by a UI
timer. It makes no audio of its own and does not listen to incoming MIDI.

- **Version documented:** 0.5.0
- **Formats:** Audio Unit MIDI Effect (`aumi`) and VST3 (instrument-class, silent stereo output)
- **Platform:** macOS 11 or later, Apple Silicon by default (Universal build opt-in)
- **Licence:** GPLv3

---

## 1. What it does, and what it does not do

KINETIC holds a pattern of up to eight lanes and 64 steps. Each lane walks its own step list in
its own time, decides whether a step fires, and emits note-on/note-off pairs (or CC messages)
into the host's MIDI stream with sample-accurate offsets derived from host PPQ.

It does **not**:

- produce audio — the VST3 declares a stereo output that is never written to, purely so hosts
  can host it as an instrument and route its MIDI onward;
- respond to MIDI input — notes played on a keyboard or drawn in the host's piano roll are
  ignored. The pattern in the editor is the only source of notes;
- talk to the network, load samples, or use the filesystem.

---

## 2. Installation

Both formats are installed into the per-user plug-in folders:

| Format | Location | Identifier |
| --- | --- | --- |
| AU MIDI FX | `~/Library/Audio/Plug-Ins/Components/KINETIC.component` | type `aumi`, subtype `Kntk`, manufacturer `Kntc` |
| VST3 | `~/Library/Audio/Plug-Ins/VST3/KINETIC.vst3` | `com.kineticmidi.kinetic.vst3` |

From source, every build signs and installs both bundles automatically:

```bash
Scripts/build.sh Release            # native arm64
Scripts/build.sh Release universal  # arm64 + x86_64
KINETIC_INSTALL_PLUGINS=OFF Scripts/build.sh Release   # build only, no install
```

The install step ad-hoc signs the bundle, verifies the seal, copies it with `rsync --delete` and
verifies the installed copy. `KINETIC_INSTALL_PLUGINS` (default `ON`) controls it; CI sets it off.

After installing a new build, hosts that already loaded the previous binary keep it mapped.
**Quit the host fully and reopen it** — rescanning plug-ins is not enough, because the running
process holds the old image.

---

## 3. Getting started

1. Insert KINETIC on a MIDI track. In Logic Pro, use the MIDI FX slot of an instrument track
   (the AU is a MIDI effect). In Ableton Live, use the VST3 on a MIDI track.
2. Open the editor. Lane 1 is switched on automatically the first time the editor opens; the
   other seven lanes start disabled.
3. Click at least one cell in the `TRIG` row. A fresh instance has an empty pattern and is
   therefore silent — this is intended.
4. Start the host's transport. KINETIC only plays while the transport is running.

Every lane starts at its own step 1 the moment the transport starts, wherever the playhead
happens to be; there is no alignment to bar lines. The same phase reset happens on any locate or
loop wrap.

### Routing the MIDI out (Ableton Live)

Live ignores a plug-in's generated MIDI unless the receiving track's input chooser names the
plug-in:

1. Track A hosts KINETIC. Track B hosts the instrument that should play the notes.
2. On track B, set **MIDI From** to track A.
3. In the **lower** chooser directly underneath, select `KINETIC` — not `Post FX` and not
   `Pre FX`. This is the step that is easy to miss: `Post FX` means "MIDI that arrived at track
   A", which does not include what KINETIC generates.
4. Set track B's **Monitor** to `In`.

Live merges channels when routing internally, so the lane's MIDI channel is not preserved
between tracks; all generated notes arrive on one channel. The AU format cannot be used for this
at all — AU plug-ins have no direct MIDI out in Live. Use the VST3.

---

## 4. The editor

The window is resizable from 860×470 up to 1600×900 (default 1040×580).

```
┌──────────────────────────────────────────────────────────────┐
│ KINETIC                                              [ VST3 ] │
│ − + OCT 3    SCALE ON    KEY C ▾    MODE MAJOR ▾              │
│ [1][2][3][4][5][6][7][8]                            [ ON ]     │
│ Loop length [ 16 ] 1/16 ▾ STRAIGHT ▾ FORWARD ▾                │
│  1   2   3   4   5   6   7   8   9  10 …                      │
│ TRIG  █       █                                               │
│ PITCH                                                         │
│ VEL   ▓▓      ▓▓                                              │
│ MOD                                                           │
│ GATE  ▓       ▓                                               │
│ RATCH                                                         │
│ SLIDE                                                         │
│ PPQ 12.34   120.0 BPM   PLAYING   held 1   gen 96   [ PANIC ] │
└──────────────────────────────────────────────────────────────┘
```

### 4.1 Header

The badge on the right shows the format the instance was built as (`AU MIDI FX` or `VST3`).

### 4.2 Lane buttons

`1`–`8` select which lane the grid edits. Only one lane is shown at a time; selection is stored
with the session.

`ON` / `OFF` enables or disables the lane being edited. **A disabled lane never sounds**, and its
step position does not advance while it is off. Switching it back on while the transport is
running joins at the current position and carries on from the step it stopped at; the steps that
went by while it was off are skipped, not replayed.

### 4.3 Transpose, scale and lane controls

The row above the lane buttons holds the controls that apply to the whole pattern: `−` and `+`
transpose the root note by octaves with the `OCT` readout beside them, then the scale aware switch
with the key and mode it snaps to. The controls below the lane buttons apply to the lane being
edited. None of them carry a text label; they are identified by position, left to right: the
step-length number box, then the division, modifier and direction choosers. Their tooltips say the
same as the table below.

| Control | Range | Meaning |
| --- | --- | --- |
| Transpose (`−` `+`, `OCT`) | 0–127, default 60 | The pattern's root note, shared by all eight lanes. `+` and `−` move it one octave up or down and `OCT` shows the octave it lands in (`OCT 3` is C3); they never change its note letter. |
| Scale aware (`SCALE ON` / `SCALE OFF`) | on / off, default off | Snaps every generated note to the key and mode beside it. |
| Key | `C` … `B` | Key of the scale. |
| Mode | `CHROMATIC`, `MAJOR`, `MINOR`, `DORIAN`, `PHRYGIAN DOM`, `PENTATONIC` | Scale the notes are snapped to; `CHROMATIC` allows all twelve pitch classes and so changes nothing. |
| Length (leftmost number box with +/−) | 1–64 | The lane's loop length. The grid always shows at least 16 columns; columns past the loop length are dimmed and are not played. |
| Division | `1/1`, `1/2`, `1/4`, `1/8`, `1/16`, `1/32`, `1/64` | Duration of one step. `1/16` is a sixteenth note, `1/4` a quarter. |
| Modifier | `STRAIGHT`, `DOTTED`, `TRIPLET` | Multiplies the step duration by 1, 1.5 or 2/3. |
| Direction | `FORWARD`, `REVERSE`, `PING-PONG`, `PENDULUM`, `RANDOM WALK` | Play order, applied to all four sub-lanes of the lane. |

Step duration in quarter notes is `4 × numerator / denominator`, scaled by the modifier:
`1/16` = 0.25, `1/4` = 1.0, `1/64` = 0.0625.

Direction details:

- **Forward** — 0,1,2,… and wrap.
- **Reverse** — last to first and wrap.
- **Ping-pong** — reverses without repeating the end steps (0,1,2,1,0…).
- **Pendulum** — reverses while repeating the end steps (0,1,2,2,1,0…).
- **Random walk** — steps +1 or −1 at random, staying inside the loop.

### 4.4 The grid

Seven rows, one column per step. `VEL`, `MOD` and `GATE` are knobs; `PITCH` is a note picker;
`TRIG`, `RATCH` and `SLIDE` are clicked.

| Row | Interaction | Range |
| --- | --- | --- |
| `TRIG` | Click or drag toggles | on / off |
| `PITCH` | Click the cell to pick a note of the octave it sits in; the small `−` and `+` at its sides move the note an octave | Any MIDI note, stored as an offset from the pattern root. With scale aware on, the list offers only the notes of the key and mode. |
| `VEL` | Knob, turned by dragging up or down | 0–127, default 100 |
| `MOD` | Knob, turned by dragging up or down | 0–127, default 64 |
| `GATE` | Knob, turned by dragging up or down | 2–100 % of the step, default 50 % |
| `RATCH` | Pointer height; the `-` strip at the bottom of the cell switches it off | 1–8 sub-triggers per step |
| `SLIDE` | Click or drag toggles | on / off |

`TRIG` and `SLIDE` toggle based on the cell you first touched in the gesture, so a drag across a
row paints everything to the same state. A drag never changes row, and a knob drag stays on the
cell it grabbed: pulling past the top or bottom border cannot edit the cell in the row above or
below. A knob takes about 160 px of vertical travel for its full range, so a click alone leaves it
unchanged and a slow drag sets fine values.

The pitch cell is a picker, not a knob: it names the note it plays (the pattern root plus the
step's offset, e.g. `E3`), clicking it lists the twelve notes of that octave with the current one
ticked, and the small `−`/`+` buttons beside it move the note one octave down or up. When cells
are too narrow for those buttons, the list carries `Octave up` and `Octave down` instead, and a
cell too narrow for the octave number shows just the note letter. The painted note is what the
step plays before scale aware quantises it, and the picker only offers in-scale notes while scale
aware is on, so picking a note is enough to land on it.

The columns are shaded in alternating groups of four, so beats are easy to follow. Step numbers
appear in the header strip; when cells get narrow only every fourth number is drawn.

Steps beyond the loop length are still editable and are saved, but they are not played until the
loop length covers them.

### 4.5 Footer

`PPQ 12.34   120.0 BPM   PLAYING   held 1   gen 96`

| Field | Meaning |
| --- | --- |
| `PPQ` | Host position at the start of the last processed block, in quarter notes |
| BPM | Host tempo as reported |
| `PLAYING` / `STOPPED` | Host transport state |
| `held` | Notes KINETIC currently has sounding |
| `gen` | Events generated since the transport last started (resets on each start) |
| `dropped` | Only shown when non-zero: events the internal sink refused (see §9) |

The footer updates 20 times a second. `PANIC` sends note-offs for every note KINETIC believes is
held, then sustain-off (CC64), all-notes-off (CC123) and reset-all-controllers (CC121) on every
channel it has used.

---

## 5. How a step is turned into MIDI

For each step boundary, the lane:

1. decides whether the step fires — from `TRIG`, or from the Euclidean generator when that is
   enabled (see §6.6);
2. applies the step's **cycle condition** and **probability**;
3. applies **micro timing** (±50 % of the step width, moving the note earlier or later);
4. reads `PITCH` and `VEL` from their own sub-lanes (see §5.1), computes the note as
   `root + pitch`, and snaps it to the active scale;
5. emits ratchets (1–8 sub-triggers), each with its own note-on and a release derived from
   `GATE`;
6. applies slide behaviour (see §6.7) and accent (see §6.8).

Gate length is a fraction of the step — or of the ratchet subdivision when ratchets > 1 —
clamped to 2–100 %. A 50 % gate on a 16th at 120 BPM is 62.5 ms, and that is exactly what the
engine produces.

### 5.1 Polymetric sub-lanes

A lane reads four separate step lists, each with its own loop length:

- trigger/gate (`TRIG`, `GATE`, `RATCH`, `SLIDE`, plus velocity, micro timing, probability and
  condition),
- pitch (`PITCH`),
- velocity (`VEL`),
- modulation (`MOD`).

Only the trigger sub-lane's length is exposed in the editor (`Loop length`); pitch, velocity and
modulation lengths default to 16. Because they are independent, a 4-step gate over a 3-step pitch
repeats only every 12 steps.

### 5.2 CC lanes

A lane whose type is `cc` emits `modController` (default CC 74) with the `MOD` value of each
triggered step, instead of notes.

---

## 6. Engine features not yet exposed in the editor

The following exist in the engine, are saved and restored with the session, and are settable
through the state format — but the editor has no controls for them in 0.5.0.

### 6.1 Lane type

`note` (default), `drum` (note-type lane behaving as a note lane) or `cc` (see §5.2).

### 6.2 MIDI channel and root note

The MIDI channel is per lane, 1–16 (default 1), and still state-format only. The root note is one
note for the whole pattern, 0–127 (default 60, i.e. C3 in Live/JUCE naming): the editor's `−`/`+`
buttons transpose it by octaves (§4.3), and the painted pitch of every lane is an offset from it,
so one transpose moves all eight lanes together.

### 6.3 Scales

Scale aware is the switch in the editor's row above the lane buttons. With it on, every generated
note is snapped to the chosen key and mode **when the step fires**; with it off, which is the
default, the painted pitch is played exactly. A note that is already sounding is never retuned, so changing the
key, the mode or the switch cannot make a held note jump — the change takes effect with the next
trigger.

The modes are `chromatic`, `major`, `minor`, `dorian`, `phrygianDominant` and `pentatonic`; the
state format also carries `custom` with a 12-bit mask (C = bit 0, default `0x0fff`) that no editor
control sets yet. The key is one of the twelve pitch classes. Key, mode and switch are pattern-wide
and apply to all eight lanes. Quantisation picks the nearest allowed pitch class and resolves ties
upward, so `F#` in C major becomes `G`.

### 6.4 Sub-lane loop lengths

Pitch, velocity and modulation loops are independently 1–64 (default 16 each).

### 6.5 Modulation controller

CC number for CC lanes, default 74.

### 6.6 Euclidean generator

When enabled it replaces the painted trigger pattern for that lane: `steps` (default 16),
`pulses` (default 4) and `rotation` (default 0), produced as an evenly spaced pattern and rotated.

### 6.7 Slide

Three interpretations, selected per lane:

| Mode | Behaviour |
| --- | --- |
| `legatoOverlap` (default) | The previous note is held past this step's note-on by `slideOverlapMs` (default 12 ms), so the two notes overlap and the synth glides. |
| `portamentoCC65` | Sends CC65 on when a slide step follows a note and off otherwise. |
| `pitchBendRamp` | Keeps the previous note held and ramps pitch bend toward the new note across the step, about one message per millisecond, then lands exactly on the target. Range comes from `bendRangeSemitones` (default 2). |

### 6.8 Accent

An accented step raises velocity by `accentVelocityBoost` (default 25) and sends `accentCC`
(default CC 74) at `accentCCValue` (default 110).

### 6.9 Cycle conditions

Per step, applied per completed pass of the lane's trigger loop:

`always`, `first`, `notFirst`, `fill` (4th of every 4 passes), `1:2`, `2:2`, `1:4`, `3:4`, `4:4`.

### 6.10 Micro timing, probability, ratchets

Per step: micro timing −50…+50 % of the step, probability 0–100 % (evaluated per step), ratchets
1–8 with a velocity ramp of `flat`, `rampUp` or `rampDown` applied across the sub-triggers.

Ratchet counts are edited in the grid's `RATCH` row, where the strip along the bottom of a cell
switches the ratchet off (one ratchet) instead of making you aim at the lowest eighth of the cell.
The strip takes about a third of the row, so it grows into a comfortable target when the editor is
enlarged.
Micro timing, probability and the ratchet velocity ramp are still state-format only.
---

## 7. Timing model

- Every offset comes from the host's PPQ position at the start of the block plus tempo and sample
  rate. Positions are never accumulated across blocks, so timing cannot drift and jitter cannot
  creep in.
- Events are queued with their **on and off positions together**. A note-on can never be emitted
  whose release was not stored, which is what prevents hanging notes.
- On transport stop, locate or loop wrap, every note still held is released at the start of the
  block, before any new generation.
- At transport start, and after a locate or loop wrap, every lane's phase is reset to the head of
  its loop (or its tail in `reverse`).
- If the host provides no transport position at all, the engine free-runs at the last reported
  tempo so the pattern still advances. This is a fallback, not a claim that the position came
  from the host.

---

## 8. Session state

The whole pattern — all eight lanes with all 64 steps each, plus the selected lane — is saved
inside the host's session via the plug-in state. Notes:

- Only non-default step fields are serialised, so an empty pattern is a few hundred bytes.
- A state written by a **different format version is refused** rather than parsed on a guess: the
  pattern is left untouched. The current format version is 1.
- A state that contains no pattern at all still loads; the current pattern remains.
- Pressing play on a device whose state was never saved leaves the pattern in the editor only. If
  the pattern matters, save the host set.

---

## 9. Limits and capacity

| Limit | Value | What happens at the limit |
| --- | --- | --- |
| Lanes | 8 | — |
| Steps per lane | 64 | — |
| Events per block | 1024 | Excess events are counted as `dropped` and shown in the footer |
| Scheduled notes per lane | 32 | A sub-trigger may be skipped rather than emitted without a stored release |
| Ratchets per step | 8 | Clamped |
| Simultaneous notes per channel/note | 1 | A note-on for a note already sounding on that channel is suppressed until it is released |

The 1024-event ceiling is generous: it takes far more than eight fully ratcheted lanes at once to
approach it.

---

## 10. Troubleshooting

**Nothing sounds at all.**
The pattern is empty, the lane is off, or the transport is stopped. Check that at least one `TRIG`
cell is lit, that the lane's `ON` button is lit, and that the host is actually playing. A fresh
instance is silent by design.

**Notes generated but no instrument plays them (Ableton Live).**
Track B's lower MIDI-from chooser is on `Post FX`. Select `KINETIC` there and set Monitor to `In`
(§3).

**It worked, then stopped after I rebuilt the plug-in.**
The host still has the previous binary mapped. Quit the host completely and reopen it; a plug-in
rescan inside the running host is not enough.

**The AU is not in Logic's MIDI FX slot.**
The AU is published as `aumi` (MIDI processor). After installing, Logic needs to be restarted to
rescan Audio Units.

**A note never stops.**
Press `PANIC`. If it recurs, note which lane and step produced it and whether the lane uses slide
or high ratchet counts.

**Steps after a certain column do nothing.**
They are past the lane's loop length. Raise `Loop length` or clear the cells.

**The pattern plays with unwanted notes.**
The scale quantiser is chromatic by default, so pitches land exactly where painted; if the host's
session was saved with a scale or Euclidean settings applied through the state format (§6.6,
§6.3), those settings come back with it.

**`dropped` appears in the footer.**
The engine produced more than 1024 events in a single block. Reduce ratchets or lanes.

---

## 11. Building and testing

```bash
cmake --preset release              # or: Scripts/build.sh Release
cmake --build build/Release-arm64 -j $(sysctl -n hw.ncpu)
```

- Universal build: `-DKINETIC_UNIVERSAL=ON` or `Scripts/build.sh Release universal`.
- Tests: `build/Release-arm64/kinetic_timing_tests_artefacts/Release/kinetic_timing_tests`.
  They cover the timing clock, the sequencer, the lock-free pattern hand-off and the state
  format, and require no host.
- Other options: `KINETIC_BUILD_TESTS` (default `ON`), `KINETIC_INSTALL_PLUGINS` (default `ON`),
  `KINETIC_COPY_PLUGINS` (default `OFF`).

The AU can be validated with `auval -v aumi Kntk Kntc`.

---

## 12. Known limitations

- The AU has no direct MIDI out in Ableton Live (an AU-format restriction, not a KINETIC one);
  use the VST3 there. In Logic, the AU is meant for the MIDI FX slot.
- MIDI input is accepted for host compatibility but ignored.
- Editor controls for lane type, channel, Euclidean generation, slide mode, accent values, the
  non-trigger sub-lane lengths and a custom scale mask are not built yet (§6).
- One pattern per instance; there are no pattern slots or chaining.
- No undo/redo in the editor. Host undo does not cover pattern edits.
- Tempo changes are followed from host PPQ, but there is no independent tempo setting.

---

## 13. Licence

KINETIC is free software under the GNU General Public License v3.0. JUCE 8 is used under the same
terms. See `LICENSE` at the repository root.
