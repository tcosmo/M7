/**
 * SynthBridge — renderer-process facade for the FluidSynth native addon.
 *
 * All calls cross the context-isolation boundary via `window.api.synth.*`
 * exposed by the preload script. For latency-critical paths (noteOn,
 * noteOff, allNotesOff) we use fire-and-forget IPC (ipcRenderer.send)
 * instead of invoke to avoid awaiting a round-trip.
 */

interface PresetInfo {
  program: number
  name: string
}

declare global {
  interface Window {
    api: {
      synth: {
        init: (sampleRate?: string) => Promise<boolean>
        loadSoundfont: (path: string) => Promise<number>
        noteOn: (channel: number, pitch: number, velocity: number) => void
        noteOff: (channel: number, pitch: number) => void
        allNotesOff: (channel: number) => void
        programSelect: (
          channel: number,
          sfontId: number,
          bank: number,
          program: number,
        ) => void
        setGain: (gain: number) => void
        getGain: () => Promise<number>
        setPitchOffset: (channel: number, semitones: number) => void
        getPresets: (sfontId?: number) => Promise<PresetInfo[]>
        shutdown: () => void
      }
      fs: {
        readFile: (path: string) => Promise<Uint8Array>
        readDir: (path: string) => Promise<string[]>
        writeFile: (path: string, data: Uint8Array) => Promise<void>
        exists: (path: string) => Promise<boolean>
      }
      app: {
        getResourcesPath: () => Promise<string>
      }
    }
  }
}

export class SynthBridge {
  /**
   * Initialize the synth engine and load the default soundfont.
   * Call once at app startup.
   */
  static async init(sf3Path: string): Promise<boolean> {
    const ok = await window.api.synth.init()
    if (!ok) return false

    const sfontId = await window.api.synth.loadSoundfont(sf3Path)
    return sfontId >= 0
  }

  /** Fire-and-forget note on. Velocity defaults to 100. */
  static noteOn(channel: number, pitch: number, velocity = 100): void {
    window.api?.synth?.noteOn?.(channel, pitch, velocity)
  }

  /** Fire-and-forget note off. */
  static noteOff(channel: number, pitch: number): void {
    window.api?.synth?.noteOff?.(channel, pitch)
  }

  /** Stop all sounding notes on a channel. */
  static allNotesOff(channel: number): void {
    window.api?.synth?.allNotesOff?.(channel)
  }

  /** Load additional soundfont, returns sfont id. */
  static async loadSoundfont(path: string): Promise<number> {
    return window.api.synth.loadSoundfont(path)
  }

  /** Select bank + program on a channel (fire-and-forget). */
  static programSelect(
    channel: number,
    sfontId: number,
    bank: number,
    program: number,
  ): void {
    window.api.synth.programSelect(channel, sfontId, bank, program)
  }

  /** Set master gain 0.0 – 5.0 (fire-and-forget). */
  static setGain(gain: number): void {
    window.api.synth.setGain(gain)
  }

  /** Get current gain value. */
  static async getGain(): Promise<number> {
    return window.api.synth.getGain()
  }

  /** Set pitch bend for tuning on a channel (fire-and-forget). */
  static setPitchOffset(channel: number, semitones: number): void {
    window.api.synth.setPitchOffset(channel, semitones)
  }

  /** Get list of presets from a loaded soundfont. */
  static async getPresets(sfontId?: number): Promise<PresetInfo[]> {
    return window.api.synth.getPresets(sfontId)
  }

  /** Shut down the synth engine. Call on app quit. */
  static shutdown(): void {
    window.api.synth.shutdown()
  }
}
