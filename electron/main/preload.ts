import { contextBridge, ipcRenderer } from 'electron'

contextBridge.exposeInMainWorld('api', {
  synth: {
    // Async (invoke) — used for init / loading / queries
    init: (sampleRate?: string) =>
      ipcRenderer.invoke('synth:init', sampleRate),
    loadSoundfont: (path: string) =>
      ipcRenderer.invoke('synth:loadSoundfont', path),
    getGain: () =>
      ipcRenderer.invoke('synth:getGain'),
    getPresets: (sfontId?: number) =>
      ipcRenderer.invoke('synth:getPresets', sfontId),

    // Fire-and-forget (send) — latency-critical audio paths
    noteOn: (channel: number, note: number, velocity: number) =>
      ipcRenderer.send('synth:noteOn', channel, note, velocity),
    noteOff: (channel: number, note: number) =>
      ipcRenderer.send('synth:noteOff', channel, note),
    allNotesOff: (channel: number) =>
      ipcRenderer.send('synth:allNotesOff', channel),
    programSelect: (channel: number, sfontId: number, bank: number, program: number) =>
      ipcRenderer.send('synth:programSelect', channel, sfontId, bank, program),
    setGain: (gain: number) =>
      ipcRenderer.send('synth:setGain', gain),
    setPitchOffset: (channel: number, semitones: number) =>
      ipcRenderer.send('synth:setPitchOffset', channel, semitones),
    shutdown: () =>
      ipcRenderer.send('synth:shutdown'),
  },

  fs: {
    readFile: (path: string) =>
      ipcRenderer.invoke('fs:readFile', path),
    readDir: (path: string) =>
      ipcRenderer.invoke('fs:readDir', path),
    writeFile: (path: string, data: Uint8Array) =>
      ipcRenderer.invoke('fs:writeFile', path, data),
    exists: (path: string) =>
      ipcRenderer.invoke('fs:exists', path),
  },

  app: {
    getResourcesPath: () =>
      ipcRenderer.invoke('app:getResourcesPath'),
  },
})
