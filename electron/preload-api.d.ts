/** Type declarations for the contextBridge API exposed via preload */

interface SynthApi {
  init(sampleRate?: string): Promise<boolean>
  loadSoundfont(path: string): Promise<number>
  noteOn(channel: number, note: number, velocity: number): void
  noteOff(channel: number, note: number): void
  allNotesOff(channel: number): void
  programSelect(channel: number, sfontId: number, bank: number, program: number): void
  setGain(gain: number): void
  getGain(): Promise<number>
  setPitchOffset(channel: number, semitones: number): void
  getPresets(sfontId?: number): Promise<Array<{ program: number; name: string }>>
  shutdown(): void
}

interface FsApi {
  readFile(path: string): Promise<Uint8Array>
  readDir(path: string): Promise<string[]>
  writeFile(path: string, data: Uint8Array): Promise<void>
  exists(path: string): Promise<boolean>
}

interface AppApi {
  getResourcesPath(): Promise<string>
}

interface ElectronApi {
  synth: SynthApi
  fs: FsApi
  app: AppApi
}

declare global {
  interface Window {
    api: ElectronApi
  }
}

export {}
