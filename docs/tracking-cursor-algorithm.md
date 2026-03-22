# Tracking Cursor Algorithm (Verovio Engine)

## Overview

A semi-transparent blue vertical line tracks the playback position across the score, interpolating smoothly between note positions. It handles silent measures via proportional cross-system splitting and system height caching.

## Data Flow

```
YouTube player → positionChanged(seconds)
    ↓
App::onPositionChanged(seconds)
    ↓ adjusts for interpretation start offset
SyncTimer::setTime(adjusted)
    ↓ binary search in beatTimes, interpolates to tick
    ↓ stores m_lastTick (always set, even on early-return paths)
App reads m_syncTimer->currentTick()
    ↓ adds 80ms forward compensation for web view IPC latency
ScoreWidget::setCursorTick(tick)
    ↓ runJavaScript("setCursorTick(tick); [getCursorX(), getCursorSysBounds()]")
    ↓ reads back cursor X + system bounds for game scoring
JS: setCursorTick(tick)
    ↓ binary search in note timemap
    ↓ interpolates x between note elements
    ↓ positions blue div at interpolated x, spanning cached system height
```

## Building the Timemap

### Note timemap (from SVG)

In `renderAllPagesHtml()`, after rendering all SVG pages:

1. Call `RenderToMIDI()` AFTER `RenderToSVG()` so MIDI data references the same IDs
2. Parse each rendered SVG for `<g class="note">` elements only (rests excluded — `GetMIDIValuesForElement` returns empty for rests)
3. For each note, call `GetMIDIValuesForElement(id)` to get its MIDI onset time
4. Store `{qstamp, elementId}` where `qstamp = midiTime / 500.0`
5. Sort by qstamp

### Tick conversion

- **Verovio MIDI**: 500 ticks per quarter note
- **SyncTimer**: 480 ticks per quarter note
- **Conversion**: `qstamp = verovioTick / 500.0`, then `tick = qstamp * 480`

### Timemap injection

The note timemap JS array is sent via `loadFinished` callback with a version counter (`m_timemapVersion`) to cancel stale loads.

## SyncTimer: Time → Tick

1. Binary search `beatTimes` to find surrounding beats
2. Linear interpolation: `t = (seconds - prevBeatTime) / (nextBeatTime - prevBeatTime)`
3. `interpTick = prevBeatTick + t * (nextBeatTick - prevBeatTick)`
4. `m_lastTick` is always set — empty data → 0, before first beat → first beat tick

## JavaScript: Tick → Cursor Position

### `setCursorTick(tick)` algorithm

1. **Binary search** the note timemap for surrounding entries `lo` and `hi`
2. **Find SVG elements** via `document.getElementById(timemap[lo].id)` and `timemap[hi].id`
3. **Determine systems**: `e1.closest('.system')` and `e2.closest('.system')`
4. **Store cursor position**: `_cursorX`, `_cursorSysTop`, `_cursorSysBottom` for game scoring

### Three interpolation cases

#### Same system (normal case)
Both notes are in the same system. Linearly interpolate x:
```
t = (tick - lo.tick) / (hi.tick - lo.tick)
x = x1 + (x2 - x1) * t
```

#### Cross-system transition (proportional split)
Notes are in different systems. Time is split **proportionally** based on remaining pixel distance to each system edge:

```
distEnd = rightEdge - currentX     // remaining distance on old system
distStart = noteX2 - startX2       // distance from content start to target on new system
split = distEnd / (distEnd + distStart)
```

**First phase (f < split)** — exiting old system:
- Cursor glides from the last note toward the right edge
- Handles trailing silent measures: cursor travels through them at proportional speed
```
x = currentX + (rightEdge - currentX) * (f / split)
```

**Second phase (f ≥ split)** — entering new system:
- `startX2` = first `.note, .rest, .mRest` element on the new system (skips clef/key/time signature)
- Cursor enters at the first musical content, not at the system's left edge
```
f2 = (f - split) / (1 - split)
x = startX2 + (noteX2 - startX2) * f2
```

#### End of piece
When `lo === hi`, cursor stays at the last note position.

### Silent measure handling

The timemap only has entries for notes. Silent measures (rests) have no entries. This is handled entirely by the proportional cross-system split:

- When the last note on system 1 is followed by silent measures, `distEnd` (from last note to right edge) is large, so the cursor spends more time traveling to the right edge — appearing to move through the silent measures.
- When system 2 starts with silent measures, `startX2` is the first content element (note or rest), and `distStart` accounts for the distance from there to the target note.

No synthetic elements or measure maps are needed.

## Cursor Height & Positioning

### System height caching

All system heights are measured **once at page load** (300ms after load, before any highlight rects exist) and stored in `_sysHeights` map. During playback, only the `top` position is recomputed from the live bounding rect (changes with scrolling). The height stays fixed.

### `_getSysBounds(sys)`

Returns `{top, height}` using the system's live `getBoundingClientRect().top` + cached height from `_sysHeights`.

### Cursor div properties

- `position: absolute` (moves with document scroll)
- `background: rgba(50, 100, 255, 0.35)` (semi-transparent blue)
- `width: 4px`
- `height: cached system height`
- `z-index: 999` (above score content)

## Game Scoring Integration

`setCursorTick` stores `_cursorX` and cursor system bounds which are read back asynchronously:

```js
setCursorTick(tick); [getCursorX(), getCursorSysBounds()]
```

### Hit/Miss computation

`ScoreWidget::highlightCursorDistance(voice)`:
1. Gets highlight note center X from overlay
2. Gets cursor X from stored `m_lastCursorX`
3. If note Y is outside cursor system bounds → **automatic hit** (system break grace)
4. Otherwise returns `|noteX - cursorX|` in pixels

### System break grace

When the note and cursor are on different systems (note Y outside `[cursorSysTop, cursorSysBottom]`), the distance is 0.0 (automatic hit). This handles the case where the highlight has moved to the next system but the cursor hasn't crossed the line break yet. Safe because `recordHit` is only called when `playerIsPlaying()` — can't cheat by tapping ahead while paused.

### Scoring thresholds

- `distance ≤ 12px` → Hit (accuracy unchanged)
- `distance > 12px` → Miss (accuracy = hits/total taps, drops accordingly)

## Tracking Button Connection

- Cursor position always updates (needed for game scoring even with cursor hidden)
- Cursor visibility controlled by `_cursorVisible` JS flag: `hideCursor()` / `showCursor()`
- Toggling tracking off hides the cursor div but position data (`_cursorX`, system bounds) still updates
- `resetScoreState()` hides cursor, resets sync timer, resets highlights

## Restart (Cmd+R) / Level Entry

`resetScoreState()` (single path for all resets):
1. Cursor hidden, scroll to top
2. Sync timer reset to time 0
3. Play-along synth position reset
4. Voice highlights moved to first note of each voice
5. Game bar stats reset

First play in a level (`m_hasPlayedInLevel` flag) resets highlights to beginning instantly in `togglePlayPause`.

## Latency Compensation

Web view IPC adds ~50-80ms latency. Compensated by computing cursor tick at `adjusted + 0.08` seconds:

```cpp
m_syncTimer->setTime(adjusted + 0.08);
m_scoreWidget->setCursorTick(m_syncTimer->currentTick());
m_syncTimer->setTime(adjusted); // restore
```

## Note Click → Highlight Jump

Clicking a note in the score moves the voice highlighter:
1. JS `click` handler finds nearest `.note` element (direct hit or within 30px)
2. `ScoreWidget::eventFilter` detects mouse release, queries `getClickedNote()` from JS
3. `noteClicked` signal fires → App searches `m_vrvVoices` to find voice + index
4. Calls `setNextNoteIndex(voice, index)` + `overlayHighlight(voice, elementId)`

### Focus-click suppression

When the app window is not focused and the user clicks on the score to bring it back, that click should not move a voice highlighter. This is handled by `ScoreWidget::eventFilter`:

1. Event filter installed on **both** the web view's focus proxy (for mouse events) and the top-level window (for activation events)
2. `WindowActivate` event on the window → sets `m_ignoreNextClick = true`
3. Next `MouseButtonPress` on the web view focus proxy → eaten, flag cleared
4. Matching `MouseButtonRelease` → also eaten (flag still active for the release)

No timers — purely event-driven. The flag is set on activation and consumed by the very next mouse press.

## Arrow Key Navigation

Left/right arrow keys move voice 0's highlighter one note at a time:
- Clamped to `[0, lastNoteIndex]`
- Updates both `PlayAlongSynth::setNextNoteIndex(0, idx)` and `overlayHighlight(0, elementId)`
- Only active when `m_playModeActive` is true

## Focus Management

YouTube's `QWebEngineView` steals keyboard focus when the user clicks on the video player.
Once Chromium has focus, `App::keyPressEvent` never fires, breaking arrow keys and other shortcuts.

**Solution:** `setFocus()` is called on the main window whenever the user presses a letter key
for play-along. This reclaims focus from the YouTube web view without interfering with YouTube
controls (no global key interception, respecting YouTube iframe ToS).

Score web view clicks also reclaim focus via `window()->setFocus()` in `ScoreWidget::eventFilter`.

## beatsOffset

The `"beatsOffset"` field in `sources.json` shifts the beat data timing for interpretations
where the beat data was recorded from a different video. Applied in `onPositionChanged`:

```
adjusted = max(0, videoSeconds - interpStart - beatsOffset)
```

Positive offset = music starts later in the new video than the beat data expects.
