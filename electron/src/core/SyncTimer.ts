/**
 * SyncTimer — converts audio time (seconds) to score ticks using beat data.
 *
 * Ported from src/synctimer.h / src/synctimer.cpp in the Qt desktop app.
 * Uses binary search + linear interpolation between control points.
 *
 * Pure TypeScript, no DOM/React dependencies.
 */

export interface SyncTimerData {
  beatTimes: number[]   // audio times in seconds (ascending)
  beatTicks: number[]   // corresponding score ticks (ascending)
  measureStarts: number[] // audio times of measure starts
  beatsPerMeasure: number
}

export class SyncTimer {
  private beatTimes: number[] = []
  private beatTicks: number[] = []
  private measureStarts: number[] = []
  private beatsPerMeasure = 3

  /** Load beat data. */
  load(data: SyncTimerData): void {
    this.beatTimes = [...data.beatTimes]
    this.beatTicks = [...data.beatTicks]
    this.measureStarts = [...data.measureStarts]
    this.beatsPerMeasure = data.beatsPerMeasure
  }

  /** Clear all data. */
  clear(): void {
    this.beatTimes = []
    this.beatTicks = []
    this.measureStarts = []
  }

  /** Whether beat data is loaded. */
  get hasData(): boolean {
    return this.beatTimes.length >= 2 && this.beatTicks.length >= 2
  }

  /**
   * Convert audio time (seconds) to score tick position.
   * Uses binary search + linear interpolation.
   * Returns 0 if no data loaded or time is before first beat.
   */
  timeToTick(timeSec: number): number {
    if (!this.hasData) return 0

    const times = this.beatTimes
    const ticks = this.beatTicks

    // Before first point
    if (timeSec <= times[0]) return ticks[0]

    // After last point
    if (timeSec >= times[times.length - 1]) return ticks[ticks.length - 1]

    // Binary search for the interval
    let lo = 0
    let hi = times.length - 1
    while (hi - lo > 1) {
      const mid = (lo + hi) >>> 1
      if (times[mid] <= timeSec) {
        lo = mid
      } else {
        hi = mid
      }
    }

    // Linear interpolation between lo and hi
    const t0 = times[lo]
    const t1 = times[hi]
    const tk0 = ticks[lo]
    const tk1 = ticks[hi]

    if (t1 <= t0) return tk0

    const frac = (timeSec - t0) / (t1 - t0)
    return tk0 + frac * (tk1 - tk0)
  }

  /**
   * Convert score tick to audio time (seconds).
   * Inverse of timeToTick. Uses binary search on ticks array.
   */
  tickToTime(tick: number): number {
    if (!this.hasData) return 0

    const times = this.beatTimes
    const ticks = this.beatTicks

    if (tick <= ticks[0]) return times[0]
    if (tick >= ticks[ticks.length - 1]) return times[times.length - 1]

    let lo = 0
    let hi = ticks.length - 1
    while (hi - lo > 1) {
      const mid = (lo + hi) >>> 1
      if (ticks[mid] <= tick) {
        lo = mid
      } else {
        hi = mid
      }
    }

    const tk0 = ticks[lo]
    const tk1 = ticks[hi]
    const t0 = times[lo]
    const t1 = times[hi]

    if (tk1 <= tk0) return t0

    const frac = (tick - tk0) / (tk1 - tk0)
    return t0 + frac * (t1 - t0)
  }

  /**
   * Get the current measure number (0-based) for a given audio time.
   */
  getMeasureAt(timeSec: number): number {
    if (this.measureStarts.length === 0) return 0

    // Binary search in measureStarts
    let lo = 0
    let hi = this.measureStarts.length
    while (lo < hi) {
      const mid = (lo + hi) >>> 1
      if (this.measureStarts[mid] <= timeSec) {
        lo = mid + 1
      } else {
        hi = mid
      }
    }
    return Math.max(0, lo - 1)
  }

  /** Get measure start times. */
  getMeasureStarts(): readonly number[] {
    return this.measureStarts
  }

  /** Get the raw beat data. */
  getBeatTimes(): readonly number[] {
    return this.beatTimes
  }

  getBeatTicks(): readonly number[] {
    return this.beatTicks
  }

  getBeatsPerMeasure(): number {
    return this.beatsPerMeasure
  }
}
