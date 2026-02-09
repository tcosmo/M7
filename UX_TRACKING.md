# Tracking Settings (@UX_TRACKING)

The **Tracking** sidebar section controls cursor tracking and auto-scroll behavior during audio playback.

## Controls

### Tracking (checkbox)
Mirrors the toolbar Tracking button (and keyboard shortcut `T`). When off, the cursor is hidden and all settings below are disabled.

### Auto-scroll (checkbox)
When on, the viewport automatically scrolls to keep the cursor visible. When off, the cursor still moves through the score but the view stays put.

Persisted in `settings.json` under `tracking.autoScroll`.

### Show trigger line (checkbox)
Displays a red dashed horizontal line across the score at the trigger line position. Useful for tuning the two parameters below. This is a temporary visual aid — not persisted across restarts.

### Trigger line (5–95%, default 60%)
How far down the viewport the cursor is allowed to drift before auto-scroll fires. For example, at 60% the view scrolls once the cursor passes 60% of the way down the visible area.

Persisted in `settings.json` under `tracking.triggerPoint`.

### Scroll amount (10–100%, default 88%)
How much of the space above the cursor gets scrolled away when auto-scroll fires.

- **100%** = cursor jumps to the very top of the viewport
- **80%** = cursor lands at roughly `trigger * (1 - 0.80)` from the top (e.g. 12% with a 60% trigger)
- Lower values produce gentler, more frequent scrolls

Formula used: `targetY = viewportHeight * triggerPoint * (1 - scrollAmount)`

Persisted in `settings.json` under `tracking.scrollAmount`.

## Enable/disable hierarchy

```
Tracking off  →  everything disabled
Tracking on   →  Auto-scroll enabled
                  Auto-scroll off  →  spinboxes + show trigger disabled
                  Auto-scroll on   →  all controls enabled
```

## Persistence

Settings are stored in `settings.json` (next to the binary) under the `"tracking"` key:

```json
{
  "tracking": {
    "autoScroll": true,
    "triggerPoint": 60,
    "scrollAmount": 88
  }
}
```

`Tracking` and `Show trigger line` are transient — always reset to on/off respectively on launch.
