# Auto-Scroll Algorithm

## "Keep All Visible" Algorithm

Simple rule: **only scroll when a highlighted note is actually off-screen.**

### How it works

1. After any note advance (any voice), gather all highlight rectangles (`.hl-v0`, `.hl-v1`)
2. Compute their combined bounding box in viewport coordinates
3. **All visible** (top ≥ margin AND bottom ≤ viewport - margin) → **do nothing**
4. **Any highlight off-screen** → scroll so the topmost highlight is at `margin` from the top
5. Smooth scroll animation

### Why this works

- **Minimal scrolling**: only scrolls when actually necessary (something went off-screen)
- **Multi-voice friendly**: considers ALL voices, not just one. Both voices stay visible as long as they fit
- **No jitter**: won't scroll at system breaks if everything is still on screen
- **Naturally handles single voice**: with one highlight, it simply keeps that one visible
- **Shows maximum context**: by placing the topmost note near the top after scroll, upcoming content is maximized

### Parameters
- `margin` = 50px breathing room from viewport edges

### Trigger
- Called when voice 0 highlight updates (every note advance for voice 0)
- Voice 1 highlight updates don't trigger scroll checks (voice 0 is typically the lead voice)
