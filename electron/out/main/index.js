"use strict";
const electron = require("electron");
const path = require("path");
const http = require("http");
const url = require("url");
const promises = require("fs/promises");
const fs = require("fs");
let synth = null;
try {
  const addonPath = path.join(path.dirname(path.dirname(__dirname)), "main", "native", "build", "Release", "fluidsynth_addon.node");
  synth = require(addonPath);
  console.log("FluidSynth native addon loaded from", addonPath);
} catch (e) {
  console.warn("FluidSynth native addon not available:", e.message);
}
function registerIpcHandlers() {
  electron.ipcMain.handle("synth:init", async (_e, sampleRate) => {
    return synth?.init(sampleRate) ?? false;
  });
  electron.ipcMain.handle("synth:loadSoundfont", async (_e, path2) => {
    return synth?.loadSoundfont(path2) ?? -1;
  });
  electron.ipcMain.handle("synth:getGain", async () => {
    return synth?.getGain() ?? 0;
  });
  electron.ipcMain.handle("synth:getPresets", async (_e, sfontId) => {
    return synth?.getPresets(sfontId) ?? [];
  });
  electron.ipcMain.on("synth:noteOn", (_e, channel, pitch, velocity) => {
    synth?.noteOn(channel, pitch, velocity);
  });
  electron.ipcMain.on("synth:noteOff", (_e, channel, pitch) => {
    synth?.noteOff(channel, pitch);
  });
  electron.ipcMain.on("synth:allNotesOff", (_e, channel) => {
    synth?.allNotesOff(channel);
  });
  electron.ipcMain.on("synth:programSelect", (_e, channel, sfontId, bank, program) => {
    synth?.programSelect(channel, sfontId, bank, program);
  });
  electron.ipcMain.on("synth:setGain", (_e, gain) => {
    synth?.setGain(gain);
  });
  electron.ipcMain.on("synth:setPitchOffset", (_e, channel, semitones) => {
    synth?.setPitchOffset(channel, semitones);
  });
  electron.ipcMain.on("synth:shutdown", () => {
    synth?.shutdown();
  });
  electron.ipcMain.handle("fs:readFile", async (_e, path2) => {
    const buf = await promises.readFile(path2);
    return new Uint8Array(buf);
  });
  electron.ipcMain.handle("fs:readDir", async (_e, path2) => {
    return promises.readdir(path2);
  });
  electron.ipcMain.handle("fs:writeFile", async (_e, path2, data) => {
    await promises.writeFile(path2, data);
  });
  electron.ipcMain.handle("fs:exists", async (_e, path2) => {
    try {
      await promises.access(path2, fs.constants.F_OK);
      return true;
    } catch {
      return false;
    }
  });
  electron.ipcMain.handle("app:getResourcesPath", async () => {
    if (electron.app.isPackaged) {
      return process.resourcesPath;
    }
    return path.dirname(electron.app.getAppPath());
  });
}
const isDev = !electron.app.isPackaged;
let mainWindow = null;
function createWindow() {
  const displays = electron.screen.getAllDisplays();
  const primaryId = electron.screen.getPrimaryDisplay().id;
  const secondary = displays.find((d) => d.id !== primaryId);
  const targetBounds = secondary ? secondary.workArea : electron.screen.getPrimaryDisplay().workArea;
  mainWindow = new electron.BrowserWindow({
    x: targetBounds.x + Math.round((targetBounds.width - 1400) / 2),
    y: targetBounds.y + Math.round((targetBounds.height - 900) / 2),
    width: Math.min(1400, targetBounds.width),
    height: Math.min(900, targetBounds.height),
    minWidth: 800,
    minHeight: 600,
    backgroundColor: "#202225",
    titleBarStyle: "hiddenInset",
    trafficLightPosition: { x: 12, y: 12 },
    webPreferences: {
      preload: path.join(__dirname, "../preload/preload.js"),
      sandbox: false,
      contextIsolation: true,
      nodeIntegration: false
    }
  });
  mainWindow.webContents.on("console-message", (_e, level, message) => {
    const prefix = ["LOG", "WARN", "ERR", "INFO", "DEBUG"][level] ?? "LOG";
    console.log(`[renderer ${prefix}] ${message}`);
  });
  mainWindow.webContents.setWindowOpenHandler(({ url: url2 }) => {
    electron.shell.openExternal(url2);
    return { action: "deny" };
  });
  if (isDev && process.env["ELECTRON_RENDERER_URL"]) {
    mainWindow.loadURL(process.env["ELECTRON_RENDERER_URL"]);
  } else {
    mainWindow.loadFile(path.join(__dirname, "../renderer/index.html"));
  }
  mainWindow.on("closed", () => {
    mainWindow = null;
  });
}
function startDebugServer() {
  if (!isDev) return;
  const server = http.createServer(async (req, res) => {
    if (!mainWindow) {
      res.writeHead(503);
      res.end("No window");
      return;
    }
    if (req.method === "POST" && req.url === "/eval") {
      let body = "";
      req.on("data", (c) => {
        body += c.toString();
      });
      req.on("end", async () => {
        try {
          const result = await mainWindow.webContents.executeJavaScript(body);
          res.writeHead(200, { "Content-Type": "application/json" });
          res.end(JSON.stringify({ ok: true, result }));
        } catch (e) {
          res.writeHead(500, { "Content-Type": "application/json" });
          res.end(JSON.stringify({ ok: false, error: e.message }));
        }
      });
    } else if (req.method === "GET" && req.url === "/screenshot") {
      try {
        const img = await mainWindow.webContents.capturePage();
        const png = img.toPNG();
        res.writeHead(200, { "Content-Type": "image/png" });
        res.end(png);
      } catch (e) {
        res.writeHead(500);
        res.end(e.message);
      }
    } else {
      res.writeHead(404);
      res.end("Not found");
    }
  });
  server.on("error", (e) => {
    if (e.code === "EADDRINUSE") {
      console.warn("Debug server port 17243 in use, skipping");
    } else {
      console.warn("Debug server error:", e.message);
    }
  });
  server.listen(17243, "127.0.0.1", () => {
    console.log("Debug server on http://127.0.0.1:17243");
  });
}
electron.protocol.registerSchemesAsPrivileged([
  { scheme: "local-file", privileges: { bypassCSP: true, supportFetchAPI: true } }
]);
electron.app.whenReady().then(() => {
  electron.protocol.handle("local-file", (req) => {
    const filePath = path.normalize(decodeURIComponent(new URL(req.url).pathname));
    return electron.net.fetch(url.pathToFileURL(filePath).toString());
  });
  registerIpcHandlers();
  createWindow();
  startDebugServer();
  electron.app.on("activate", () => {
    if (electron.BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });
});
electron.app.on("window-all-closed", () => {
  if (process.platform !== "darwin") {
    electron.app.quit();
  }
});
