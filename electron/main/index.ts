import { app, BrowserWindow, shell, protocol, net, screen } from 'electron'
import { join, normalize } from 'path'
import { createServer } from 'http'
import { pathToFileURL } from 'url'
import { registerIpcHandlers } from './ipc-handlers'

const isDev = !app.isPackaged

let mainWindow: BrowserWindow | null = null

function createWindow(): void {
  // Find secondary display (MSI) if available, otherwise use primary
  const displays = screen.getAllDisplays()
  const primaryId = screen.getPrimaryDisplay().id
  const secondary = displays.find(d => d.id !== primaryId)
  const targetBounds = secondary ? secondary.workArea : screen.getPrimaryDisplay().workArea

  mainWindow = new BrowserWindow({
    x: targetBounds.x + Math.round((targetBounds.width - 1400) / 2),
    y: targetBounds.y + Math.round((targetBounds.height - 900) / 2),
    width: Math.min(1400, targetBounds.width),
    height: Math.min(900, targetBounds.height),
    minWidth: 800,
    minHeight: 600,
    backgroundColor: '#202225',
    titleBarStyle: 'hiddenInset',
    trafficLightPosition: { x: 12, y: 12 },
    webPreferences: {
      preload: join(__dirname, '../preload/preload.js'),
      sandbox: false,
      contextIsolation: true,
      nodeIntegration: false,
    },
  })

  // Forward renderer console to main process stdout
  mainWindow.webContents.on('console-message', (_e, level, message) => {
    const prefix = ['LOG', 'WARN', 'ERR', 'INFO', 'DEBUG'][level] ?? 'LOG'
    console.log(`[renderer ${prefix}] ${message}`)
  })

  // Open external links in browser
  mainWindow.webContents.setWindowOpenHandler(({ url }) => {
    shell.openExternal(url)
    return { action: 'deny' }
  })

  // Dev: load from vite dev server; Prod: load built files
  if (isDev && process.env['ELECTRON_RENDERER_URL']) {
    mainWindow.loadURL(process.env['ELECTRON_RENDERER_URL'])
  } else {
    mainWindow.loadFile(join(__dirname, '../renderer/index.html'))
  }

  mainWindow.on('closed', () => {
    mainWindow = null
  })
}

// Dev-only debug HTTP server: POST /eval with JS body, or GET /screenshot
function startDebugServer(): void {
  if (!isDev) return
  const server = createServer(async (req, res) => {
    if (!mainWindow) {
      res.writeHead(503); res.end('No window'); return
    }
    if (req.method === 'POST' && req.url === '/eval') {
      let body = ''
      req.on('data', (c: Buffer) => { body += c.toString() })
      req.on('end', async () => {
        try {
          const result = await mainWindow!.webContents.executeJavaScript(body)
          res.writeHead(200, { 'Content-Type': 'application/json' })
          res.end(JSON.stringify({ ok: true, result }))
        } catch (e: any) {
          res.writeHead(500, { 'Content-Type': 'application/json' })
          res.end(JSON.stringify({ ok: false, error: e.message }))
        }
      })
    } else if (req.method === 'GET' && req.url === '/screenshot') {
      try {
        const img = await mainWindow!.webContents.capturePage()
        const png = img.toPNG()
        res.writeHead(200, { 'Content-Type': 'image/png' })
        res.end(png)
      } catch (e: any) {
        res.writeHead(500); res.end(e.message)
      }
    } else {
      res.writeHead(404); res.end('Not found')
    }
  })
  server.on('error', (e: NodeJS.ErrnoException) => {
    if (e.code === 'EADDRINUSE') {
      console.warn('Debug server port 17243 in use, skipping')
    } else {
      console.warn('Debug server error:', e.message)
    }
  })
  server.listen(17243, '127.0.0.1', () => {
    console.log('Debug server on http://127.0.0.1:17243')
  })
}

// Register custom protocol to serve local files from the renderer
// Usage: local-file:///absolute/path/to/file.jpg
protocol.registerSchemesAsPrivileged([
  { scheme: 'local-file', privileges: { bypassCSP: true, supportFetchAPI: true } },
])

app.whenReady().then(() => {
  protocol.handle('local-file', (req) => {
    // Strip scheme, decode URI
    const filePath = normalize(decodeURIComponent(new URL(req.url).pathname))
    return net.fetch(pathToFileURL(filePath).toString())
  })

  registerIpcHandlers()
  createWindow()
  startDebugServer()

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow()
    }
  })
})

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit()
  }
})
