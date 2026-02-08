# Parts Panel — UX & UI Reference

The Parts panel lives in the right sidebar under a collapsible "Parts" section. It controls which parts (instruments) are visible in the score and how they are displayed.

## Layout (top to bottom)

### 1. Global Settings

#### Transposing Instruments
- Label: **"Transposing Instruments"**
- Below the label, a grey sub-label lists the names of any transposing instruments in the score (elided with "..." if they overflow). If there are none, it reads "No transposing instruments." and the radio buttons are disabled.
- Radio buttons: **Written** | **Concert** | **Per Part**
  - **Written** — all transposing parts show written pitch (original key).
  - **Concert** — all transposing parts are transposed to concert pitch.
  - **Per Part** — each part's own Written/Concert radio (inside its expanded row) becomes active.
- Changing the global selection immediately applies to all transposing parts and re-lays out the score.

#### Clefs
- Label: **"Clefs"**
- Radio buttons: **Written** | **Per Part**
  - **Written** — every part uses the clef from the original MusicXML. Per-part clef/octave combos are disabled.
  - **Per Part** — each part's clef and octave combos become active, and any per-part overrides (including restored settings) take effect.

### 2. Buttons

- **Show All** — makes every part visible.
- **Solo** — solos the first currently-visible part (hides all others).

### 3. Part Rows (scrollable list)

One row per part from the MusicXML, in score order. Each row has a **header** (always visible) and a **collapsible content area**.

#### Header (28 px tall)
| Element | Behaviour |
|---|---|
| Eye icon (left) | Click to toggle this part's visibility. The icon is a white open-eye when visible, grey with a strike-through when hidden. |
| Part name | Shows the part name (white when visible, grey when hidden). Clicking the name or arrow expands/collapses the row. |
| Arrow (right) | Points right when collapsed, down when expanded. |

**Shift+click** anywhere on the header row (eye icon, part name, or arrow) to **solo** this part — hides all other parts, shows only this one.

#### Content Area (expanded)
Slightly lighter background (`#2a2a2a`) with left indent.

**Clef** — Combo box (160 px wide) with options:
- Treble Clef, Bass Clef, Soprano Clef (C1), Mezzo-Soprano Clef (C2), Alto Clef (C3), Tenor Clef (C4), Baritone Clef (C5)
- Initialised from the MusicXML. Disabled when global Clefs mode is "Written".

**Octave** — Combo box with options:
- Written (no shift), 8va (octave up), 8vb (octave down)
- Enabled only for Treble and Bass clefs; automatically disabled and reset to "Written" for C clefs.
- Disabled when global Clefs mode is "Written".

**Transposing Instrument** — Per-part Written/Concert radios.
- Greyed out and disabled if the part is not a transposing instrument.
- Enabled only when the global Transposing Instruments mode is "Per Part".

## Settings Persistence

Settings are saved to `settings.json` next to the app binary. The file is shared with Display Settings; each section owns its own key.

```json
{
  "display": { "layoutMode": 2, "showTitleFrame": false },
  "defaults": { "transposingInstruments": "concert" },
  "scores": {
    "Magnificat.musicxml": {
      "parts": {
        "Tenore.": { "clef": 0, "octave": -1 }
      }
    }
  }
}
```

- **`defaults.transposingInstruments`** — loaded on startup to set the global pitch radio ("concert" or "written"). In-app changes do not update this default.
- **`scores.<filename>.parts`** — per-score, per-part clef and octave overrides. Only parts that differ from their original MusicXML clef are stored. Saved whenever a clef or octave combo changes. Restored after the score is loaded; if any saved overrides exist, the global Clefs mode is automatically switched to "Per Part".

## Relayout Behaviour

Any change to visibility, clef, octave, or pitch mode triggers a full score relayout. When 2 or fewer parts are visible, instrument names on subsequent systems are hidden to save space.

## Visual Style

- Dark theme throughout: backgrounds `#252525` (header) / `#2a2a2a` (content).
- Radio buttons: light grey text, blue indicator when checked, grey when disabled.
- Eye icons rendered at 2x into a QPixmap with `setDevicePixelRatio(2)` for retina.
- Scrollbar: semi-transparent white handle, no arrow buttons.
