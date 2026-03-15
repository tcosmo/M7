/**
 * NoteTable — ported from src/playalongsynth.h
 *
 * Data structures for note events used in play-along mode and a placeholder
 * builder that accepts pre-extracted note data (the real extraction happens
 * inside the WASM/MuseScore engraving layer).
 *
 * Pure TypeScript, no DOM/React dependencies.
 */

// ── Data types ────────────────────────────────────────────────────────────

export interface NoteEvent {
  /** Tick position in the score (480 ticks = one quarter note). */
  tick: number;
  /** MIDI pitch (0-127). */
  midiPitch: number;
  /** Duration in ticks. */
  durationTicks: number;
  /** Verovio / engraving SVG element ID (used for highlighting). */
  elementId: string;
  /** True if this note is a tie continuation (auto-advance, no keypress). */
  tiedBack: boolean;
  /** True if the note carries a trill ornament. */
  hasTrill: boolean;
  /** MIDI pitch of the upper trill note (meaningful only when hasTrill). */
  trillPitch: number;
}

export interface Voice {
  /** Long instrument name (used to match the score part). */
  partName: string;
  /** General MIDI program number. */
  gmProgram: number;
  /** MIDI channel for playback. */
  channel: number;
  /** Loaded soundfont id (-1 = not loaded yet). */
  sfontId: number;
  /** MIDI pitch of the last note that was turned on (-1 = none). */
  lastPlayedNote: number;
  /** Index of the next note to be played in `notes`. */
  nextIndex: number;
  /** Notes sorted ascending by tick. */
  notes: NoteEvent[];
}

// ── Minimal input for the placeholder builder ─────────────────────────────

export interface RawNoteInput {
  tick: number;
  pitch: number;
  duration: number;
  elementId: string;
  tiedBack?: boolean;
  hasTrill?: boolean;
  trillPitch?: number;
}

// ── Factory helpers ───────────────────────────────────────────────────────

/**
 * Create a fresh Voice with default state.
 */
export function createVoice(
  partName: string,
  gmProgram = 34,
  channel = 0,
  sfontId = -1,
): Voice {
  return {
    partName,
    gmProgram,
    channel,
    sfontId,
    lastPlayedNote: -1,
    nextIndex: 0,
    notes: [],
  };
}

/**
 * Placeholder builder: takes an array of raw note descriptors and produces
 * a sorted NoteEvent[] suitable for a Voice.
 *
 * In the full desktop app, `buildNoteTableForPart` walks the MuseScore
 * engraving DOM (Segments -> Chords -> Notes).  In the Electron app the
 * equivalent data will come from the WASM bridge; this function normalises
 * and sorts whatever the bridge provides.
 */
export function buildNoteTable(raw: RawNoteInput[]): NoteEvent[] {
  const events: NoteEvent[] = raw.map((r) => ({
    tick: r.tick,
    midiPitch: r.pitch,
    durationTicks: r.duration,
    elementId: r.elementId,
    tiedBack: r.tiedBack ?? false,
    hasTrill: r.hasTrill ?? false,
    trillPitch: r.trillPitch ?? 0,
  }));

  // Sort ascending by tick (stable — preserves chord order for same-tick notes)
  events.sort((a, b) => a.tick - b.tick);

  return events;
}

/**
 * Build a complete Voice from raw note inputs.
 */
export function buildVoice(
  partName: string,
  gmProgram: number,
  channel: number,
  raw: RawNoteInput[],
  sfontId = -1,
): Voice {
  const voice = createVoice(partName, gmProgram, channel, sfontId);
  voice.notes = buildNoteTable(raw);
  return voice;
}

// ── Playback helpers ──────────────────────────────────────────────────────

/**
 * Advance a voice past any tied-back notes starting from the current
 * nextIndex, as long as their tick <= currentTick.
 * Returns true if any advancement happened.
 */
export function advanceTiedNotes(voice: Voice, currentTick: number): boolean {
  let advanced = false;
  while (
    voice.nextIndex < voice.notes.length &&
    voice.notes[voice.nextIndex].tiedBack &&
    voice.notes[voice.nextIndex].tick <= currentTick
  ) {
    voice.nextIndex++;
    advanced = true;
  }
  return advanced;
}

/**
 * Advance a voice's nextIndex to the next non-tied note.
 * Called after playing a note to skip over tie continuations.
 */
export function skipTiedContinuations(voice: Voice): void {
  while (
    voice.nextIndex < voice.notes.length &&
    voice.notes[voice.nextIndex].tiedBack
  ) {
    voice.nextIndex++;
  }
}

/**
 * Reset a voice to the beginning.
 */
export function resetVoice(voice: Voice): void {
  voice.nextIndex = 0;
  voice.lastPlayedNote = -1;
}
