import { ipcMain, app } from 'electron'
import { readFile, readdir, writeFile, access } from 'fs/promises'
import { constants } from 'fs'
import { join, dirname } from 'path'

// Load FluidSynth native addon in the main process (where CoreAudio works)
let synth: any = null
try {
  const addonPath = join(dirname(dirname(__dirname)), 'main', 'native', 'build', 'Release', 'fluidsynth_addon.node')
  synth = require(addonPath)
  console.log('FluidSynth native addon loaded from', addonPath)
} catch (e) {
  console.warn('FluidSynth native addon not available:', (e as Error).message)
}

export function registerIpcHandlers(): void {
  // ── Synth ────────────────────────────────────────────────────────

  ipcMain.handle('synth:init', async (_e, sampleRate?: string) => {
    return synth?.init(sampleRate) ?? false
  })

  ipcMain.handle('synth:loadSoundfont', async (_e, path: string) => {
    return synth?.loadSoundfont(path) ?? -1
  })

  ipcMain.handle('synth:getGain', async () => {
    return synth?.getGain() ?? 0
  })

  ipcMain.handle('synth:getPresets', async (_e, sfontId?: number) => {
    return synth?.getPresets(sfontId) ?? []
  })

  // Fire-and-forget — latency-critical
  ipcMain.on('synth:noteOn', (_e, channel: number, pitch: number, velocity: number) => {
    synth?.noteOn(channel, pitch, velocity)
  })

  ipcMain.on('synth:noteOff', (_e, channel: number, pitch: number) => {
    synth?.noteOff(channel, pitch)
  })

  ipcMain.on('synth:allNotesOff', (_e, channel: number) => {
    synth?.allNotesOff(channel)
  })

  ipcMain.on('synth:programSelect', (_e, channel: number, sfontId: number, bank: number, program: number) => {
    synth?.programSelect(channel, sfontId, bank, program)
  })

  ipcMain.on('synth:setGain', (_e, gain: number) => {
    synth?.setGain(gain)
  })

  ipcMain.on('synth:setPitchOffset', (_e, channel: number, semitones: number) => {
    synth?.setPitchOffset(channel, semitones)
  })

  ipcMain.on('synth:shutdown', () => {
    synth?.shutdown()
  })

  // ── Filesystem ─────────────────────────────────────────────────

  ipcMain.handle('fs:readFile', async (_e, path: string) => {
    const buf = await readFile(path)
    return new Uint8Array(buf)
  })

  ipcMain.handle('fs:readDir', async (_e, path: string) => {
    return readdir(path)
  })

  ipcMain.handle('fs:writeFile', async (_e, path: string, data: Uint8Array) => {
    await writeFile(path, data)
  })

  ipcMain.handle('fs:exists', async (_e, path: string) => {
    try {
      await access(path, constants.F_OK)
      return true
    } catch {
      return false
    }
  })

  // ── App ────────────────────────────────────────────────────────

  ipcMain.handle('app:getResourcesPath', async () => {
    if (app.isPackaged) {
      return process.resourcesPath
    }
    return dirname(app.getAppPath())
  })
}
