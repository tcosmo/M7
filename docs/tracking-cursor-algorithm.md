# Tracking Cursor Algorithm (Verovio Engine)

## Overview

A semi-transparent blue vertical line tracks the playback position across the score, interpolating smoothly between note and measure positions. It handles silent measures using beat data from the continuo part.

## Data Flow

```
YouTube player → positionChanged(seconds)
    ↓
App::onPositionChanged(seconds)
    ↓ adjusts for interpretation start offset
SyncTimer::setTime(adjusted)
    ↓ binary search in beatTimes, interpolates to tick
    ↓ stores m_lastTick
App reads m_syncTimer->currentTick()
    ↓ adds 80ms forward compensation for web view IPC latency
ScoreWidget::setCursorTick(tick)
    ↓ runJavaScript("setCursorTick(tick); [getCursorX(), getCursorSysBounds()]")
    ↓ reads back cursor X + system bounds for game scoring
JS: setCursorTick(tick)
    ↓ binary search in timemap (notes + synthetic measure entries)
    ↓ interpolates x between elements
    ↓ positions blue div at interpolated x, spanning precomputed staff height
```

## Building the Timemap

### Note timemap (from SVG)

In `renderAllPagesHtml()`, after rendering all SVG pages:

1. Call `RenderToMIDI()` AFTER `RenderToSVG()` so MIDI data references the same IDs
2. Parse each rendered SVG for `<g class="note">` elements
3. For each note, call `GetMIDIValuesForElement(id)` to get its MIDI onset time
4. Store `{qstamp, elementId}` where `qstamp = midiTime / 500.0`
5. Sort by qstamp

### Measure map (from beat data)

After the web view loads, `setMeasureTicks(ticks)` is called with measure-start ticks derived from the beat data (continuo part, covers every measure including silent ones):

1. Pairs each tick with the corresponding SVG `.measure` element's bounding rect
2. For ticks that don't already have a note entry (within 20-tick tolerance), creates a synthetic `<div>` at the measure's left edge
3. Merges into and re-sorts the `_timemap` array

This fills gaps in the note-only timemap so the cursor has positions in silent measures.

### Tick conversion

- **Verovio MIDI**: 500 ticks per quarter note
- **SyncTimer**: 480 ticks per quarter note
- **Conversion**: `qstamp = verovioTick / 500.0`, then `tick = qstamp * 480`

### Timemap injection

The note timemap JS array is sent via `loadFinished` callback with a version counter (`m_timemapVersion`) to cancel stale loads. The measure map is sent 500ms after `setEngine()` to let the page fully render.

## SyncTimer: Time → Tick

1. Binary search `beatTimes` to find surrounding beats
2. Linear interpolation: `t = (seconds - prevBeatTime) / (nextBeatTime - prevBeatTime)`
3. `interpTick = prevBeatTick + t * (nextBeatTick - prevBeatTick)`
4. `m_lastTick` is always set — including on early-return paths (empty data → 0, before first beat → first beat tick)

## JavaScript: Tick → Cursor Position

### `setCursorTick(tick)` algorithm

1. **Binary search** the merged timemap for surrounding entries `lo` and `hi`
2. **Find elements** via `document.getElementById()` — either SVG note elements or synthetic measure `<div>`s
3. **Determine systems**: `e1.closest('.system')` and `e2.closest('.system')`
4. **Store cursor position**: `_cursorX`, `_cursorSysTop`, `_cursorSysBottom` for game scoring

### Three interpolation cases

#### Same system (normal case)
Both elements are in the same system. Linearly interpolate x:
```
t = (tick - lo.tick) / (hi.tick - lo.tick)
x = x1 + (x2 - x1) * t
```

#### Cross-system transition (proportional split)
Elements are in different systems. Time is split proportionally based on remaining pixel distance to each system edge (not 50/50):

```
distEnd = rightEdge - currentX     // remaining distance on old system
distStart = noteX2 - startX2       // distance from content start to target on new system
split = distEnd / (distEnd + distStart)
```

**First half (f < split)** — exiting old system:
```
x = currentX + (rightEdge - currentX) * (f / split)
```

**Second half (f ≥ split)** — entering new system:
- `startX2` = first `.note, .rest, .mRest` element's left edge on the new system (skips clef/key/time signature)
```
f2 = (f - split) / (1 - split)
x = startX2 + (noteX2 - startX2) * f2
```

#### End of piece
When `lo === hi`, cursor stays at the last element position.

## Cursor Height & Positioning

### Precomputed height (`initCursorHeight`)

Called once after page load (500ms delay, same timer as measure ticks):
- Measures the first `.staffLines` element's height (line 1 to line 5)
- Sets `_fixedCursorH = staffLinesHeight * 2.0` (50% padding on each side)

### Per-system centering

`_getSysBounds(sys)`:
- If `_fixedCursorH` is set: finds the system's `.staffLines`, centers on its vertical midpoint, uses the fixed height
- Fallback: uses system `getBoundingClientRect()` with cached height from page load

### Cursor div properties

- `position: absolute` (moves with document scroll)
- `background: rgba(50, 100, 255, 0.35)` (semi-transparent blue)
- `width: 4px`
- `height: precomputed staff-based height (fixed for all systems)`
- `z-index: 999` (above score content)

## Game Scoring Integration

`setCursorTick` stores `_cursorX` and cursor system bounds (`_cursorSysTop`, `_cursorSysBottom`) which are read back asynchronously by C++ for game mode scoring:

```js
setCursorTick(tick); [getCursorX(), getCursorSysBounds()]
```

The game bar compares `_cursorX` with the voice highlight's center X to determine Hit/Miss. System break detection: if the note Y falls outside cursor system bounds, it's an automatic hit.

## Tracking Button Connection

- Cursor only updates when `m_trackingAction->isChecked()` is true
- Toggling tracking off calls `hideCursor()` in JS
- `resetScoreState()` hides cursor, resets sync timer, resets highlights

## Restart (Cmd+R) / Level Entry

On restart or level entry, `resetScoreState()` is called (single path):
1. Cursor hidden, scroll to top
2. Sync timer reset to time 0
3. Play-along synth position reset
4. Voice highlights moved to first note of each voice
5. Game bar stats reset

First play in a level (`m_hasPlayedInLevel` flag) resets highlights to beginning.

## Latency Compensation

Web view IPC adds ~50-80ms latency. Compensated by computing cursor tick at `adjusted + 0.08` seconds:

```cpp
m_syncTimer->setTime(adjusted + 0.08);
m_scoreWidget->setCursorTick(m_syncTimer->currentTick());
m_syncTimer->setTime(adjusted); // restore
```

## Silent Measure Handling

- **Note timemap**: only has entries for notes (rests filtered — `GetMIDIValuesForElement` returns empty for rests)
- **Measure map**: synthetic entries from beat data fill silent-measure gaps
- **Cross-system transition**: proportional time split based on pixel distance, not arbitrary 50/50
- **System entry**: `startX2` positioned at first `.note, .rest, .mRest` element (skips clef/key/time sig)
