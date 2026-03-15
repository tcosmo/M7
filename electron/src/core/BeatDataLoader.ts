/**
 * BeatDataLoader — ported from App::loadBeatData() in src/app.cpp (lines 1012-1093)
 *
 * Parses three beat-data JSON formats into a normalised structure that can
 * be fed directly into SyncTimer.
 *
 * Pure TypeScript, no DOM/React dependencies.
 */

// ── Output type ──────────────────────────────────────────────────────────

export interface BeatData {
  beatTimes: number[];
  beatTicks: number[];
  measureStarts: number[];
  beatsPerMeasure: number;
}

// ── JSON input shapes ────────────────────────────────────────────────────

/** Format 1: "control_points" — recorded tracking output */
interface ControlPointsFormat {
  control_points: Array<{ tick: number; time: number }>;
  measure_starts?: number[];
  beats_per_measure?: number;
}

/** Format 2: "measures" — structured per-measure beat list */
interface MeasuresFormat {
  beats_per_measure?: number;
  measures: Array<{
    beats: Array<{ beat: number; time: number }>;
  }>;
}

/** Format 3: "beat_times" — legacy flat array */
interface BeatTimesFormat {
  beats_per_measure?: number;
  beat_times: number[];
}

type BeatDataJson = ControlPointsFormat | MeasuresFormat | BeatTimesFormat;

// ── Loader ───────────────────────────────────────────────────────────────

/**
 * Parse a beat-data JSON object (already deserialised) into normalised
 * BeatData.  Throws on unrecognised format.
 */
export function parseBeatData(obj: Record<string, unknown>): BeatData {
  const beatsPerMeasure =
    typeof obj.beats_per_measure === 'number' ? obj.beats_per_measure : 3;

  const beatTimes: number[] = [];
  const beatTicks: number[] = [];
  const measureStarts: number[] = [];

  if ('control_points' in obj && Array.isArray(obj.control_points)) {
    // ── Format 1: control_points ─────────────────────────────────────
    const cpArr = obj.control_points as Array<{ tick: number; time: number }>;
    for (const pt of cpArr) {
      beatTicks.push(pt.tick);
      beatTimes.push(pt.time);
    }
    if ('measure_starts' in obj && Array.isArray(obj.measure_starts)) {
      for (const v of obj.measure_starts as number[]) {
        measureStarts.push(v);
      }
    }
  } else if ('measures' in obj && Array.isArray(obj.measures)) {
    // ── Format 2: measures ───────────────────────────────────────────
    const measuresArr = obj.measures as Array<{
      beats: Array<{ beat: number; time: number }>;
    }>;
    for (const m of measuresArr) {
      let first = true;
      for (const b of m.beats) {
        beatTimes.push(b.time);
        if (first) {
          measureStarts.push(b.time);
          first = false;
        }
      }
    }
  } else if ('beat_times' in obj && Array.isArray(obj.beat_times)) {
    // ── Format 3: beat_times (legacy flat) ───────────────────────────
    const arr = obj.beat_times as number[];
    for (const v of arr) {
      beatTimes.push(v);
    }
    for (let i = 0; i < beatTimes.length; i += beatsPerMeasure) {
      measureStarts.push(beatTimes[i]);
    }
  } else {
    throw new Error(
      'Unrecognised beat data format: expected "control_points", "measures", or "beat_times" key',
    );
  }

  // Compute tick positions if not already provided (beat-based formats).
  // Each beat maps to one quarter note = 480 ticks.
  if (beatTicks.length === 0) {
    for (let i = 0; i < beatTimes.length; i++) {
      beatTicks.push(i * 480);
    }
  }

  return { beatTimes, beatTicks, measureStarts, beatsPerMeasure };
}

/**
 * Load beat data from a JSON string.
 */
export function loadBeatDataFromString(json: string): BeatData {
  const obj = JSON.parse(json) as Record<string, unknown>;
  return parseBeatData(obj);
}

