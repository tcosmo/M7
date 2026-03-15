"use strict";
const electron = require("electron");
electron.contextBridge.exposeInMainWorld("api", {
  synth: {
    // Async (invoke) — used for init / loading / queries
    init: (sampleRate) => electron.ipcRenderer.invoke("synth:init", sampleRate),
    loadSoundfont: (path) => electron.ipcRenderer.invoke("synth:loadSoundfont", path),
    getGain: () => electron.ipcRenderer.invoke("synth:getGain"),
    getPresets: (sfontId) => electron.ipcRenderer.invoke("synth:getPresets", sfontId),
    // Fire-and-forget (send) — latency-critical audio paths
    noteOn: (channel, note, velocity) => electron.ipcRenderer.send("synth:noteOn", channel, note, velocity),
    noteOff: (channel, note) => electron.ipcRenderer.send("synth:noteOff", channel, note),
    allNotesOff: (channel) => electron.ipcRenderer.send("synth:allNotesOff", channel),
    programSelect: (channel, sfontId, bank, program) => electron.ipcRenderer.send("synth:programSelect", channel, sfontId, bank, program),
    setGain: (gain) => electron.ipcRenderer.send("synth:setGain", gain),
    setPitchOffset: (channel, semitones) => electron.ipcRenderer.send("synth:setPitchOffset", channel, semitones),
    shutdown: () => electron.ipcRenderer.send("synth:shutdown")
  },
  fs: {
    readFile: (path) => electron.ipcRenderer.invoke("fs:readFile", path),
    readDir: (path) => electron.ipcRenderer.invoke("fs:readDir", path),
    writeFile: (path, data) => electron.ipcRenderer.invoke("fs:writeFile", path, data),
    exists: (path) => electron.ipcRenderer.invoke("fs:exists", path)
  },
  app: {
    getResourcesPath: () => electron.ipcRenderer.invoke("app:getResourcesPath")
  }
});
