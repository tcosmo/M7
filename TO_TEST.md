# PlayBach — Manual Test Pathways

## Startup
- [ ] App opens with world browser (level navigator) visible
- [ ] First interpretation's YouTube video loads in sidebar corner (no play, no seek)
- [ ] Volume slider visible in sidebar
- [ ] Interpretation combo NOT visible in sidebar (navigation mode)
- [ ] No PAC script errors in console
- [ ] First interpretation thumbnail selected (accent border) in Interpretations section

## Navigation View
- [ ] Spacebar toggles YouTube play/pause
- [ ] Clicking an interpretation thumbnail loads that video in sidebar (no play, no seek)
- [ ] Selected interpretation has larger thumbnail with accent border
- [ ] Interpretation scroll area scrolls to show the selected card (e.g. last one)
- [ ] Clicking a world card in sidebar shows its levels
- [ ] Tabs (Levels / Campaign / Sandbox) switch content correctly
- [ ] Description text displays under BWV number, not clipped

## Entering a Level (Play button)
- [ ] Toolbar appears
- [ ] Expand Video button appears
- [ ] Seek slider resets to 0:00
- [ ] Score cursor resets to beginning
- [ ] Video expands automatically
- [ ] Preselected interpretation is loaded (correct YouTube video)
- [ ] Interpretation combo appears in sidebar with correct selection
- [ ] If beat data available: tracking enabled, cursor visible
- [ ] If no beat data: tracking button disabled (not clickable), cursor hidden
- [ ] First play (spacebar or YouTube click) seeks to interpretation start time
- [ ] No YouTube loading spinner if same video was already loaded from preview

## Playing a Level
- [ ] Spacebar toggles play/pause
- [ ] Keys trigger play-along synth (NOT YouTube play/pause)
- [ ] Cursor follows playback (when tracking data available)
- [ ] Changing interpretation via sidebar combo loads new video and resets

## Exiting a Level (Escape or clicking world card)
- [ ] YouTube pauses
- [ ] Toolbar hides
- [ ] Expand Video button hides
- [ ] Video collapses to sidebar corner (220x200)
- [ ] Interpretation combo hides
- [ ] Volume slider stays visible
- [ ] No crash (previously: segfault in ScoreWidget::resizeEvent or ensureWidgetVisible)

## Returning to Level (Resume)
- [ ] If interpretation unchanged: resumes where left off, interpretation combo restored
- [ ] If interpretation changed: reloads level with new interpretation (same as Play)
- [ ] Video re-expands

## Switching Interpretations in Navigation
- [ ] Selecting interpretation pauses current playback
- [ ] New video loads without seeking
- [ ] Going back to navigation after level: correct interpretation highlighted
- [ ] Sidebar combo change in preview mode loads video preview

## Switching Between Levels
- [ ] Playing level A, go back, play level B: no crash
- [ ] Score reloads correctly for new level
- [ ] Beat data / tracking state updates for new level
- [ ] Seek slider resets

## Video Resize
- [ ] Sidebar video is 220x200 (YouTube ToS minimum 200x200)
- [ ] Expanded video fills the content area
- [ ] Collapse Video button collapses back to 220x200
- [ ] No iframe size mismatch (video not showing only top-left corner)
- [ ] Collapse Video button has left margin, looks like a proper button

## Build Configurations
- [ ] Default build (Verovio only): `cmake -B build/release -DCMAKE_BUILD_TYPE=Release`
- [ ] MuseScore build: `cmake -B build/release -DCMAKE_BUILD_TYPE=Release -DUSE_MUSESCORE=ON`
- [ ] Both build without errors
