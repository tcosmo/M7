# Navigation & Video UX

## Core Principle

**YouTube only exists inside a level. Navigation has no video.**

The YouTube player is created fresh when entering a level and destroyed when leaving.
No sidebar video, no expand/collapse toggle, no video preview in navigation.

## Video Lifecycle

### App Startup
1. Worlds load, level browser appears
2. No YouTube player exists — no video anywhere
3. Volume slider hidden, no interpretation combo in sidebar

### Selecting an Interpretation (Navigation View)
1. User clicks interpretation thumbnail in level browser
2. `m_preselectedInterpretation` is updated
3. No video is loaded — selection is stored for when the user enters a level

### Entering a Level (Play Button)
1. `loadLevel()` destroys any existing YouTube player (`delete m_youtubePlayer`)
2. Clears `m_currentYoutubeUrl`
3. Sets `m_needsSeekOnPlay = true`
4. Toolbar seek slider resets to 0:00
5. In the deferred lambda: `loadSources()` -> `loadYouTube()` creates a fresh player
6. `loadYouTube()` places the video widget directly into `m_expandedVideoContainer`
7. The container is inserted into the central splitter (above the score)
8. `m_videoExpanded = true` — video starts expanded, no toggle needed
9. Tracking enabled/disabled based on beat data availability

### User Presses Play (First Time in Level)
1. `playerPlay()` checks `m_needsSeekOnPlay`
2. Clears flag, seeks to `m_interpStart`, then calls `play()`
3. `playbackStarted` handler updates toolbar text and score playing state
4. If `m_interpStart > 0` and current time is before it, seeks again (safety net)

### User Presses Play (Subsequent)
1. `m_needsSeekOnPlay` is already false
2. Normal play/pause toggle

### Spacebar in Navigation View
- No video exists, spacebar is consumed silently (event accepted, no action)

### Switching Interpretation (Within a Level)
1. `switchInterpretation()` destroys old YouTube player **first** (stops 60fps position timer)
2. Hides Verovio cursor (`hideCursor()`)
3. Clears beat data **before** `setTime(0)` so stale data can't move cursor
4. Resets toolbar (slider, time label, play button), play-along state, seek flag
5. Loads new beat data → `setTime(0)` resets `m_lastTick` to first beat tick
6. Creates fresh YouTube player for new interpretation

### Exiting a Level (Escape / World Card Click)
1. `showWorldBrowser()` or world card click destroys the YouTube player entirely
2. `delete m_youtubePlayer; m_youtubePlayer = nullptr`
3. `m_expandedVideoContainer` is hidden
4. `m_videoExpanded = false`
5. Speed button disabled, URL cleared, `m_useYouTube = false`
6. Full cursor/state reset: cursor rect cleared, beat data cleared, toolbar reset, play-along reset

### No Resume
- Every level click goes through `levelSelected` -> `loadLevel`
- The level browser button always says "Play", never "Resume"
- `resumeRequested` signal exists in worldbrowser.h but routes to `loadLevel` too

## Key State Variables

| Variable | Purpose |
|----------|---------|
| `m_needsSeekOnPlay` | Set in `loadLevel()`, cleared in `playerPlay()`. Guards first-play seek. |
| `m_interpStart` | Start time in seconds for current interpretation (from sources.json `"start"` field). |
| `m_interpEnd` | End time for current interpretation. |
| `m_preselectedInterpretation` | Index chosen in level browser thumbnails, used when entering a level. |
| `m_activeInterpretation` | Index of currently loaded interpretation (set by `loadSources()`). |
| `m_currentYoutubeUrl` | URL of loaded video (cleared on level exit, set in `loadYouTube()`). |
| `m_lastTick` (SyncTimer) | Last resolved tick position. Reset by `setTime()` on empty beat data or time-before-first-beat. |

## Keyboard Isolation in Navigation View

When `m_centralStack->currentIndex() == 0` (navigation view), ALL keyboard events are
blocked (spacebar consumed silently). This prevents play-along synth key events from
firing while browsing levels.

## FluidSynth / Soundfont Thread Safety

The miniaudio audio callback runs on a separate thread, calling `fluid_synth_write_float` at ~60fps.
FluidSynth's `sfload`/`sfunload` functions compete for the same internal mutex → **deadlock**.

**Solution:** `static std::atomic<bool> g_synthMuted` in `playalongsynth.cpp`. Before any sfload/sfunload,
set to `true`. The audio callback checks this and outputs silence instead of calling `write_float`.
Set back to `false` after the operation.

`loadSoundfont` also caches `m_currentSfontPath` to skip reloading the same soundfont.
`ensureSoundfont` (multi-voice) caches in `m_loadedSfonts` map.

## What Was Removed

- Sidebar video (`setVideoWidget`, sidebar video container)
- Expand/Collapse button (`m_videoExpandButton`, `toggleVideoExpand()`)
- Sidebar interpretation combo (interpretation selection only via level browser thumbnails)
- Volume slider at startup (no video = no volume)
- Same-URL early return in `loadYouTube()` (always create fresh)
- Resume functionality (every click is a fresh load)
- Auto-load of first interpretation video at startup in `loadWorlds()`

## YouTube Player Keyboard Isolation

The YouTube `QWebEngineView` and all its Chromium child widgets have `Qt::NoFocus`
to prevent keypresses (spacebar, etc.) meant for play-along from being captured by
YouTube's embedded player. This is set in the `YouTubePlayer` constructor and
reinforced on `loadFinished`.
