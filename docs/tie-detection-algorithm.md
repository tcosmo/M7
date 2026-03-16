# Tie Detection Algorithm (Verovio Engine)

## Problem

In play-along mode, tied notes (held across bar lines) should not require a separate keypress. The highlight should skip from the first note of a tie chain directly to the first note after it.

Verovio does not expose tie information via `GetElementAttr` (no `@tie` attribute on MEI notes imported from MusicXML). The SVG `<g class="tie">` elements have no `startid`/`endid` linking to note IDs. So we parse the original MusicXML source.

## Algorithm

### 1. Find the correct part in the MusicXML

`m_selectedParts` stores the 1-based part numbers passed to `selectParts()` (e.g. `{7}` for Oboe I, or `{17, 4}` for Continuo + Timpani).

For `partIndex` N in the filtered score, the original part is `m_selectedParts[N] - 1` (0-based index into the MusicXML `<part-list>`).

### 2. Walk the MusicXML `<part>` and accumulate time

```
currentTime = 0   (in Verovio MIDI ticks)
divisions = 1     (from <attributes><divisions>)

For each <measure>:
  For each child element:
    <attributes>  → update divisions
    <forward>     → currentTime += duration * 500 / divisions
    <backup>      → currentTime -= duration * 500 / divisions
    <note>:
      if <chord> child → don't advance time (shares beat with previous note)
      if <rest> child  → skip (not a pitched note)
      otherwise:
        compute midiPitch from <pitch> (<step>, <octave>, <alter>)
        check for <tie type="stop"/> → record (currentTime, midiPitch)
        if not chord → currentTime += duration * 500 / divisions
```

The `500` constant is Verovio's MIDI ticks per quarter note.

### 3. Match tie-stops to SVG notes

Each SVG note has `midiTime` and `pitch` from Verovio's `GetMIDIValuesForElement()`. For each SVG note, check if `(midiTime, pitch)` exists in the tie-stop set. If yes, mark `tiedBack = true`.

### 4. Skip in play-along

`PlayAlongSynth::playNextNote()` already handles this:

```cpp
v.nextIndex++;
while (v.nextIndex < v.notes.size() && v.notes[v.nextIndex].tiedBack) {
    v.nextIndex++;
}
```

The highlight jumps directly to the first note after the tie chain.

## Why other approaches failed

| Approach | Problem |
|----------|---------|
| `GetElementAttr` `@tie` | Verovio doesn't export `@tie` for MusicXML imports |
| SVG `<g class="tie">` | No `startid`/`endid` attributes linking to note IDs |
| Same MIDI time heuristic | Verovio gives each tied fragment its own MIDI onset |
| Consecutive same-pitch heuristic | Too many false positives (repeated notes != ties) |
| MusicXML index matching | Note counts differ (345 XML vs 505 SVG) due to voice/layer splitting |
| `GetTimesForElement` `scoreTimeTiedDuration` | Returns fraction arrays, not simple doubles; values were all zero |

## Key implementation detail

`selectParts()` must store the selected part numbers in `m_selectedParts`. Without this, the tie detection looked up the wrong part (e.g. Tromba I instead of Oboe I) because `m_parts[].visible` flags were never updated by `selectParts()`.
