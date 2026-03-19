# Transposing Instruments in Verovio Play-Along

## Problem

Verovio's `GetMIDIValuesForElement()` returns the **written** pitch, not the concert
(sounding) pitch. For transposing instruments, the play-along synth would play notes
at the wrong pitch without manual correction.

### Example: Timpani in D.A.

The MusicXML score has:
```xml
<part-name>Timpani in D.A.</part-name>
...
<transpose>
  <diatonic>1</diatonic>
  <chromatic>2</chromatic>
</transpose>
```

Written C3 (MIDI 48) should sound as D3 (MIDI 50) — 2 semitones up.

## Solution

In `VerovioEngine::getNotesForPart()`, after extracting notes from the SVG:

1. Look up the original part ID using `m_selectedParts` (maps filtered index → 1-based original index)
2. Find the `<part>` element in the original MusicXML
3. Read `<attributes><transpose><chromatic>` from the first measure
4. Add the chromatic value to every note's MIDI pitch

```cpp
// In getNotesForPart(), after collecting all notes:
if (chromaticTranspose != 0) {
    for (auto& n : notes)
        n.pitch += chromaticTranspose;
}
```

## Two Layers of Pitch Correction

| Layer | Source | Applied Where | Granularity |
|-------|--------|--------------|-------------|
| **Transposition** | MusicXML `<transpose><chromatic>` | `getNotesForPart()` — added to MIDI note number | Integer semitones |
| **Interpretation tuning** | sources.json `"tuning"` field | `PlayAlongSynth::setPitchOffset()` — FluidSynth pitch bend | Fractional semitones (e.g. -0.9) |

Both stack: transposition changes the note number, tuning changes the pitch bend.

## Common Transposing Instruments

| Instrument | Chromatic | Example |
|-----------|-----------|---------|
| Timpani in D.A. | +2 | Written C → sounds D |
| Trumpet in D | +2 | Written C → sounds D |
| Trumpet in Bb | -2 | Written C → sounds Bb |
| Horn in F | -7 | Written C → sounds F below |
| Clarinet in Bb | -2 | Written C → sounds Bb |
| Clarinet in A | -3 | Written C → sounds A |

## Notes

- Only the first measure's `<transpose>` is checked (transposition typically doesn't change mid-piece in Baroque/Classical music)
- If a part has no `<transpose>` element, `chromaticTranspose` is 0 (no change)
- The `<diatonic>` element is not used — only `<chromatic>` matters for MIDI pitch
