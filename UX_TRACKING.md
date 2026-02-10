# Tracking Settings (@UX_TRACKING)

The **Tracking** sidebar section controls cursor tracking and auto-scroll behavior during audio playback.

## Controls

### Tracking (checkbox)
Mirrors the toolbar Tracking button (and keyboard shortcut `T`). When off, the cursor is hidden and auto-scroll is unticked by default.

### Auto-scroll (checkbox)
When on, the viewport automatically scrolls to keep the cursor visible. When off, the cursor still moves through the score but the view stays put.

Auto-scroll can be enabled independently of tracking. When auto-scroll is on but tracking is off, an invisible cursor is used — the cursor position is still computed and drives scrolling, but nothing is drawn on the score. This is a transient override: by default, turning tracking off unticks auto-scroll. However, if the user manually ticks auto-scroll while tracking is off, it stays on for the rest of the session (until the user unticks it).

Persisted in `settings.json` under `tracking.autoScroll`.

### Show trigger line (checkbox)
Displays a red dashed horizontal line across the score at the trigger line position. Useful for tuning the two parameters below. This is a temporary visual aid — not persisted across restarts.

### Trigger line (5–95%, default 60%)
How far down the viewport the cursor is allowed to drift before auto-scroll fires. For example, at 60% the view scrolls once the cursor passes 60% of the way down the visible area.

Persisted in `settings.json` under `tracking.triggerLine`.

### Scroll amount (10–100%, default 88%)
How much of the space above the cursor gets scrolled away when auto-scroll fires.

- **100%** = cursor jumps to the very top of the viewport
- **80%** = cursor lands at roughly `trigger * (1 - 0.80)` from the top (e.g. 12% with a 60% trigger)
- Lower values produce gentler, more frequent scrolls

Formula used: `targetY = viewportHeight * triggerPoint * (1 - scrollAmount)`

Persisted in `settings.json` under `tracking.scrollAmount`.

### Cursor anchor (combo: Top / Center / Bottom, default Center)
Which part of the cursor rectangle is compared against the trigger line. For example, with "Center" the auto-scroll fires when the vertical midpoint of the cursor crosses the trigger line. "Top" uses the top edge, "Bottom" uses the bottom edge.

Persisted in `settings.json` under `tracking.cursorAnchor` (0=Top, 1=Center, 2=Bottom).

## Enable/disable hierarchy

```
Auto-scroll off  →  spinboxes + show trigger + cursor anchor disabled
Auto-scroll on   →  all controls enabled
```

Auto-scroll is always interactable regardless of tracking state. When tracking is turned off, auto-scroll is unticked automatically unless the user has previously forced it on during the session.

## Persistence

Settings are stored in `settings.json` (next to the binary) under the `"tracking"` key:

```json
{
  "tracking": {
    "autoScroll": true,
    "triggerLine": 60,
    "scrollAmount": 88,
    "cursorAnchor": 1
  }
}
```

`Tracking`, `Show trigger line`, and the "auto-scroll without tracking" override are transient — always reset on launch.
