# Auto-Scroll Algorithm

## Simple Forward-Only Scroll

### Rule

Only **voice 0** drives scrolling. When voice 0's highlight goes **below the viewport**, scroll it to the top. Never scroll up.

### Algorithm

1. Get voice 0's highlight bounding rect
2. If `bottom <= viewport height` → **do nothing** (note is visible)
3. If `bottom > viewport height` → scroll so note's top is at 10px from viewport top

### That's it

- No trigger line, no target line, no comfort zone
- No multi-voice logic
- Never scrolls up — only forward when the note leaves the bottom
- ~5 lines of JavaScript
