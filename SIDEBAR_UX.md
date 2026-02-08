## Sidebar UX Spec

### Layout
- The sidebar **overlays** the score view (does not resize it). The score widget is always the full-width central widget.
- The sidebar is positioned to the left of the vertical scrollbar. The scrollbar is always visible at the rightmost edge of the window, to the right of the sidebar.
- A 5px drag handle sits at the left edge of the sidebar for resizing.
- Default width: 250px. Min: 200px, Max: 500px.

### Toggle behavior (Ctrl+B or toolbar button)
- **Closing**: hides the sidebar and calls `zoomToFit` so the score fills the freed space.
- **Opening**: shows the sidebar at its last width. Does NOT refit the score — the sidebar just overlays on top.

### Drag resize
- Dragging the handle resizes the sidebar. The score view is NOT refitted during drag.
- Dragging below 120px threshold collapses the sidebar (hides it), but the drag continues — dragging back past the threshold uncollapses it.
- On mouse release: `zoomToFit` is called ONLY if the sidebar ended up collapsed. Normal resize-and-release does not refit.

### Window resize
- The sidebar stays at the same width; extra width goes to the score view.
- The score auto-fits on window resize (`ScoreWidget::resizeEvent` calls `zoomToFit`).

### Sections (CollapsibleSection with QSplitter)
- **Parts** (index 0): height = min(desired content height, 60% of window). Desired height = sum of row heights + list frame (4px) + buttons (40px) + margins/padding (30px). Should be generous enough to avoid a scrollbar inside the list when all parts fit. Resizable via splitter.
- **Score Display** (index 1): fixed height based on content `sizeHint`. Never stretches.
- **Spacer** (index 2): absorbs remaining space. Its splitter handle is hidden.
- Splitter handle width: 12px. Last handle (before spacer) is hidden.
- Collapsing a section redistributes its height to the spacer. Expanding restores the saved height.

### Widget content styling
- Content areas (PartPanel, DisplaySettings) have dark grey background (`#252525`) using `QPalette::Window`.
- Content is wrapped in a CollapsibleSection with 8px horizontal margin so the dark background is inset from sidebar edges.
- Section headers have no inset (flush with sidebar).
- Content padding: 12px horizontal, 4px top, 10px bottom.
- Section header has 8px bottom padding for spacing before content.

### Part list focus
- Clicking outside the part list (anywhere on sidebar or score) unfocuses it — selection dims to inactive style but is not cleared.
- PartPanel and sidebar widget accept click focus (`Qt::ClickFocus`).
