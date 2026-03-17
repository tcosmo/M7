# QPainter Highlight Overlay

## Problem

Note highlights (rounded rectangles showing the next note to play) were originally rendered via JavaScript — inserting SVG `<rect>` elements into the DOM via `runJavaScript()`. This introduced IPC latency (Qt → Chromium process) on every keypress, making highlights feel sluggish especially with multiple voices.

## Solution

A transparent `QWidget` (`WebScoreOverlay`) sits on top of the `QWebEngineView` and draws highlights using `QPainter`. Highlight updates are synchronous with the keypress — no IPC round-trip.

## Architecture

```
Keypress → playNextNote() → overlayHighlight(voice, elementId)
    ↓
WebScoreOverlay::setHighlight(voice, elementId)
    ↓
QWidget::update() → paintEvent() → QPainter draws rounded rect
```

### WebScoreOverlay class

Lives in `scorewidget.h/cpp`, inside `namespace scoretracker`.

```
WebScoreOverlay : QWidget
├── m_notePositions : QHash<QString, QRectF>  // elementId → document-absolute rect
├── m_hlId[2]       : QString                 // currently highlighted element per voice
├── m_scrollY       : double                  // web page scroll offset
├── setNotePositions(positions)               // bulk cache from JS
├── setHighlight(voice, elementId)            // update highlight
├── clearHighlight(voice)                     // remove highlight
├── setScrollY(sy)                            // sync scroll offset
└── paintEvent()                              // QPainter rendering
```

### Widget attributes

```cpp
setAttribute(Qt::WA_TransparentForMouseEvents);  // clicks pass through to web view
setAttribute(Qt::WA_TranslucentBackground);       // transparent background
setAttribute(Qt::WA_NoSystemBackground);          // no system background fill
```

## Position Cache

### Fetching positions from JS

After the web page loads (1200ms delay), `ScoreWidget::fetchNotePositions()` runs JavaScript to collect bounding rects for all note elements:

```javascript
document.querySelectorAll('g.note[id]').forEach(function(el) {
    var bb = el.getBoundingClientRect();
    result[el.id] = [bb.left + window.scrollX, bb.top + window.scrollY, bb.width, bb.height];
});
```

Positions are in **document-absolute coordinates** (viewport position + scroll offset). The result is parsed as JSON and stored in `m_notePositions`.

### Invalidation

Positions are re-fetched:
- After window resize (300ms debounce) — CSS `width: 100%` rescales the SVG
- After `setEngine()` — new HTML page loaded

## Scroll Synchronization

The overlay draws at `(x, y - scrollY)` to convert document-absolute to viewport coordinates. Two mechanisms keep `m_scrollY` in sync:

### 1. Continuous polling (for manual scroll)

A 50ms QTimer polls `window.scrollY` from JavaScript at 20fps. This ensures the overlay follows when the user scrolls the web view manually (trackpad, mouse wheel).

```cpp
auto* scrollTimer = new QTimer(m_overlay);
connect(scrollTimer, &QTimer::timeout, this, [this]() {
    m_webView->page()->runJavaScript("window.scrollY", [this](const QVariant& v) {
        m_overlay->setScrollY(v.toDouble());
    });
});
scrollTimer->start(50);
```

### 2. On-demand sync (for highlight updates)

Each `overlayHighlight()` call also queries `window.scrollY` after triggering auto-scroll, ensuring the overlay is in sync after programmatic scrolling:

```cpp
m_webView->page()->runJavaScript(js, [this](const QVariant& v) {
    m_overlay->setScrollY(v.toDouble());
});
```

Both mechanisms are async but the 50ms polling ensures at most one frame of lag during manual scrolling.

## Paint

```cpp
void WebScoreOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    for (int v = 0; v < 2; ++v) {
        // Look up cached position by element ID
        QRectF r = m_notePositions[m_hlId[v]];
        r.moveTop(r.top() - m_scrollY);  // document → viewport coords
        // Padding + rounded rect
        QRectF padded(r.x() - 3, r.y() - 4, r.width() + 6, r.height() + 8);
        QColor color = (v == 0) ? QColor(50, 120, 255) : QColor(180, 80, 220);
        p.setPen(QPen(color, 2.0));       // same as MuseScore
        p.setBrush(QColor(color, 40));    // alpha=40, same as MuseScore
        p.drawRoundedRect(padded, 3, 3);
    }
}
```

Colors match MuseScore exactly:
- Voice 0: `QColor(50, 120, 255)` pen, `QColor(50, 120, 255, 40)` brush
- Voice 1: `QColor(180, 80, 220)` pen, `QColor(180, 80, 220, 40)` brush

## JS Highlights (Invisible)

The original JS `highlightNotes()` function still runs but with `opacity: 0` — the SVG rects are invisible. They remain in the DOM solely for the auto-scroll algorithm, which uses `document.querySelectorAll('.hl-v0,.hl-v1')` to find highlighted elements and determine their system positions.

## Why not QPainter for the cursor too?

The tracking cursor updates at ~4Hz (YouTube position callback rate). At that frequency, the JS IPC latency (~80ms) is imperceptible — it's well within the 250ms update interval. The QPainter overlay was worth the complexity for highlights because keypresses are latency-sensitive (~20ms perception threshold). The cursor has no such constraint.

## Latency Comparison

| Path | Latency |
|------|---------|
| JS highlights (old) | ~15-30ms (IPC + DOM manipulation + composite) |
| QPainter overlay (new) | <1ms (synchronous QPainter in same process) |
| JS cursor (kept) | ~80ms (acceptable at 4Hz update rate) |
