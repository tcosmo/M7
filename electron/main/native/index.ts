/**
 * FluidSynth native addon — TypeScript wrapper.
 *
 * Loads the compiled N-API addon and re-exports its functions
 * with proper TypeScript types. This module runs in the Electron
 * main process only.
 */

// eslint-disable-next-line @typescript-eslint/no-var-requires
const addon = require('./build/Release/fluidsynth_addon.node') as NativeAddon

interface PresetInfo {
  program: number
  name: string
}

interface NativeAddon {
  init(sampleRate?: string): boolean
  loadSoundfont(path: string): number
  noteOn(channel: number, pitch: number, velocity: number): void
  noteOff(channel: number, pitch: number): void
  allNotesOff(channel: number): void
  programSelect(channel: number, sfontId: number, bank: number, program: number): void
  setGain(gain: number): void
  getGain(): number
  setPitchOffset(channel: number, semitones: number): void
  getPresets(sfontId?: number): PresetInfo[]
  shutdown(): void
}

export function init(sampleRate?: string): boolean {
  return addon.init(sampleRate)
}

export function loadSoundfont(path: string): number {
  return addon.loadSoundfont(path)
}

export function noteOn(channel: number, pitch: number, velocity: number): void {
  addon.noteOn(channel, pitch, velocity)
}

export function noteOff(channel: number, pitch: number): void {
  addon.noteOff(channel, pitch)
}

export function allNotesOff(channel: number): void {
  addon.allNotesOff(channel)
}

export function programSelect(
  channel: number,
  sfontId: number,
  bank: number,
  program: number,
): void {
  addon.programSelect(channel, sfontId, bank, program)
}

export function setGain(gain: number): void {
  addon.setGain(gain)
}

export function getGain(): number {
  return addon.getGain()
}

export function setPitchOffset(channel: number, semitones: number): void {
  addon.setPitchOffset(channel, semitones)
}

export function getPresets(sfontId?: number): PresetInfo[] {
  return addon.getPresets(sfontId)
}

export function shutdown(): void {
  addon.shutdown()
}
