/**
 * RecordTracking — ported from startRecordTracking / stopRecordTracking /
 * finalizeRecordedTracking in src/app.cpp (lines 1738-1856)
 *
 * Records {wallTime, tick} pairs during play-along, then finalises them
 * into a control_points beat-data format suitable for SyncTimer.
 *
 * In the C++ version, measure starts are computed by walking the MuseScore
 * Score DOM.  Here we accept an array of measure tick positions as input
 * (the caller extracts these from the engraving layer / WASM bridge).
 *
 * Pure TypeScript, no DOM/React dependencies.
 */

// ── Data types ───────────────────────────────────────────────────────────

export interface RecordedNote {
  /** Wall-clock time in seconds (from the audio player). */
  wallTime: number;
  /** Score tick of the note that was played. */
  tick: number;
}

export interface ControlPointsBeatData {
  control_points: Array<{ tick: number; time: number }>;
  measure_starts: number[];
}

// ── Recorder class ───────────────────────────────────────────────────────

export class RecordTracker {
  private _notes: RecordedNote[] = [];
  private _active = false;

  /** Whether recording is currently active. */
  get active(): boolean {
    return this._active;
  }

  /** Number of recorded notes so far. */
  get noteCount(): number {
    return this._notes.length;
  }

  /**
   * Start (or restart) recording.  Clears any previously recorded data.
   */
  start(): void {
    this._notes = [];
    this._active = true;
  }

  /**
   * Record a single note event (called on each keypress during play-along).
   */
  record(wallTime: number, tick: number): void {
    if (!this._active) return;
    this._notes.push({ wallTime, tick });
  }

  /**
   * Stop recording without finalising (data is preserved for later
   * finalisation via `finalise()`).
   */
  stop(): void {
    this._active = false;
  }

  /**
   * Finalise the recorded data into a SyncTimer-compatible beat data
   * structure.
   *
   * @param measureTicks  Sorted array of tick positions for the start of
   *                      each measure in the score.  These are used to
   *                      compute interpolated measure-start times.  Pass
   *                      an empty array if measure boundary info is not
   *                      available.
   *
   * Returns null if there are fewer than 2 recorded notes.
   */
  finalise(measureTicks: readonly number[] = []): ControlPointsBeatData | null {
    this._active = false;

    if (this._notes.length < 2) {
      this._notes = [];
      return null;
    }

    // Sort by tick
    this._notes.sort((a, b) => a.tick - b.tick);

    // Remove duplicate ticks (keep first occurrence)
    const deduped: RecordedNote[] = [this._notes[0]];
    for (let i = 1; i < this._notes.length; i++) {
      if (this._notes[i].tick !== this._notes[i - 1].tick) {
        deduped.push(this._notes[i]);
      }
    }

    if (deduped.length < 2) {
      this._notes = [];
      return null;
    }

    // Build parallel arrays
    const beatTimes: number[] = [];
    const beatTicks: number[] = [];
    for (const rn of deduped) {
      beatTimes.push(rn.wallTime);
      beatTicks.push(rn.tick);
    }

    // Compute measure starts by interpolating score measure boundaries
    const measureStarts: number[] = [];
    const firstTick = beatTicks[0];
    const lastTick = beatTicks[beatTicks.length - 1];

    for (const mTick of measureTicks) {
      // Only include measures within our recorded range
      if (mTick < firstTick || mTick > lastTick) continue;

      // Find the interpolation position via lower_bound on beatTicks
      const idx = lowerBound(beatTicks, mTick);
      if (idx >= beatTicks.length) continue;

      if (beatTicks[idx] === mTick) {
        // Exact match
        measureStarts.push(beatTimes[idx]);
      } else if (idx > 0) {
        // Interpolate between idx-1 and idx
        const t0 = beatTimes[idx - 1];
        const t1 = beatTimes[idx];
        const tk0 = beatTicks[idx - 1];
        const tk1 = beatTicks[idx];
        const frac = tk1 > tk0 ? (mTick - tk0) / (tk1 - tk0) : 0;
        measureStarts.push(t0 + frac * (t1 - t0));
      }
    }

    // Build output
    const controlPoints: Array<{ tick: number; time: number }> = [];
    for (let i = 0; i < beatTimes.length; i++) {
      controlPoints.push({ tick: beatTicks[i], time: beatTimes[i] });
    }

    // Clear internal state
    this._notes = [];

    return {
      control_points: controlPoints,
      measure_starts: measureStarts,
    };
  }

  /**
   * Discard all recorded data without finalising.
   */
  clear(): void {
    this._notes = [];
    this._active = false;
  }
}

// ── Utility ──────────────────────────────────────────────────────────────

/**
 * Serialise finalised beat data to a JSON string (control_points format).
 */
export function serialiseBeatData(data: ControlPointsBeatData): string {
  return JSON.stringify(data, null, 2);
}

/**
 * std::lower_bound equivalent: returns the index of the first element in
 * `arr` that is >= `value`.
 */
function lowerBound(arr: readonly number[], value: number): number {
  let lo = 0;
  let hi = arr.length;
  while (lo < hi) {
    const mid = (lo + hi) >>> 1;
    if (arr[mid] < value) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}
