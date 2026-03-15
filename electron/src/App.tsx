import React, { useCallback, useEffect, useRef, useState } from 'react'
import { createPortal } from 'react-dom'
import { useAppStore } from './stores/appStore'
import { usePlayerStore } from './stores/playerStore'
import { useScoreStore } from './stores/scoreStore'
import { usePlayAlongStore } from './stores/playAlongStore'
import { WorldSidebar } from './world/WorldSidebar'
import { LevelBrowser } from './world/LevelBrowser'
import ScoreView from './score/ScoreView'
import YouTubePlayer, { type YouTubePlayerRef } from './player/YouTubePlayer'
import PlayerControls from './player/PlayerControls'
import { VerovioEngine } from './score/VerovioEngine'
import { SyncTimer } from './core/SyncTimer'
import { loadBeatDataFromString } from './core/BeatDataLoader'
import { parseSources } from './core/SourcesLoader'
import { parseWorld, type World } from './core/WorldLoader'
// import { RecordTracker, serialiseBeatData } from './core/RecordTracking' // disabled for now
import { FilePlayer } from './player/FilePlayer'
import { SynthBridge } from './synth/SynthBridge'
import { buildNoteTable, createVoice, advanceTiedNotes, skipTiedContinuations, resetVoice, type Voice, type RawNoteInput } from './core/NoteTable'
import { getVoiceIndicesForKey, isLetterKey, type VoiceKeyConfig } from './core/KeyZone'
import { theme } from './theme/theme'
import { InstrumentPanel, type VoiceInfo } from './synth/InstrumentPanel'

// ── Singletons (persist across renders) ─────────────────────────────────

const verovio = new VerovioEngine()
const syncTimer = new SyncTimer()
// const recordTracker = new RecordTracker() // tracking disabled
const filePlayer = new FilePlayer()

// Helper: extract youtube video ID from URL
function extractVideoId(url: string): string {
  const shortMatch = url.match(/youtu\.be\/([A-Za-z0-9_-]{11})/)
  if (shortMatch) return shortMatch[1]
  try {
    const parsed = new URL(url)
    const v = parsed.searchParams.get('v')
    if (v && v.length === 11) return v
    const embedMatch = parsed.pathname.match(/\/embed\/([A-Za-z0-9_-]{11})/)
    if (embedMatch) return embedMatch[1]
  } catch { /* not a valid URL */ }
  return ''
}

// Helper: resolve relative path against base
function resolvePath(base: string, rel: string): string {
  if (!rel) return ''
  if (rel.startsWith('/')) return rel
  const b = base.endsWith('/') ? base.slice(0, -1) : base
  return `${b}/${rel}`
}

// ── App ─────────────────────────────────────────────────────────────────

function App(): React.ReactElement {
  const ytRef = useRef<YouTubePlayerRef>(null)
  const verovioReady = useRef(false)
  const voicesRef = useRef<Voice[]>([])
  const voiceKeyConfigs = useRef<VoiceKeyConfig[]>([])
  // Per-voice key counters (local ref, not store — must be synchronous for audio)
  const voiceKeysHeldRef = useRef<number[]>([])
  // Per-voice soundfont name tracking
  const voiceSfNamesRef = useRef<string[]>([])
  // Portal target for the YouTube player (sidebar video slot)
  const [sidebarVideoEl, setSidebarVideoEl] = useState<HTMLDivElement | null>(null)
  // Synth state for InstrumentPanel
  const [synthVolume, setSynthVolume] = useState(0.6)
  const [synthPitch, setSynthPitch] = useState(0)
  const synthPitchRef = useRef(0) // ref for keyboard handler (avoids re-registration)
  const [voiceInfos, setVoiceInfos] = useState<VoiceInfo[]>([])
  const [showInstrumentPanel, setShowInstrumentPanel] = useState(false)
  const [availableSoundfonts, setAvailableSoundfonts] = useState<string[]>([])

  // ── Store slices ────────────────────────────────────────────────────
  const {
    worlds, currentWorldIndex, activeWorldIndex, activeSectionIndex,
    activeLevelIndex, playModeActive, videoExpanded, sidebarVisible,
    setWorlds, selectWorld, loadLevel: storeLoadLevel,
    toggleVideoExpand, toggleSidebar,
  } = useAppStore()

  const {
    useYouTube, isPlaying, currentTime, duration, speed, volume,
    youtubeUrls, sourceLabels, activeInterpretation,
    sourceTunings, sourceInstrumentVols,
    interpStart, interpEnd,
    setPosition, setPlaying, setDuration, setSpeed, setVolume,
    setUseYouTube,
    loadSources, selectInterpretation,
  } = usePlayerStore()

  const {
    svgPages, zoom, cursorRect, highlightIds, highlightIds2, autoScroll,
    setSvgPages, setCursorRect, setHighlightIds, clear: clearScore,
  } = useScoreStore()

  const {
    incrementKeysHeld, decrementKeysHeld,
  } = usePlayAlongStore()

  // ── Init Verovio ───────────────────────────────────────────────────
  useEffect(() => {
    verovio.init().then(() => {
      verovioReady.current = true
      console.log('Verovio initialized')
    })
  }, [])

  // ── Init FluidSynth + load default soundfont ─────────────────────
  useEffect(() => {
    (async () => {
      try {
        const ok = await window.api.synth.init()
        if (!ok) { console.warn('FluidSynth init failed'); return }
        console.log('FluidSynth initialized')
        const resourcesPath = await window.api.app.getResourcesPath()
        const sfPath = `${resourcesPath}/resources/sounds/FluidR3Mono_GM.sf3`
        const sfId = await window.api.synth.loadSoundfont(sfPath)
        console.log('Default soundfont loaded, id:', sfId)
        // Discover available soundfonts
        const soundsDir = `${resourcesPath}/resources/sounds`
        const files = await window.api.fs.readDir(soundsDir)
        const sfFiles = files.filter((f: string) => f.endsWith('.sf2') || f.endsWith('.sf3')).sort()
        setAvailableSoundfonts(sfFiles)
      } catch (e) {
        console.warn('FluidSynth init error:', e)
      }
    })()
  }, [])

  // ── Current video ID ──────────────────────────────────────────────
  const videoId = useYouTube && youtubeUrls[activeInterpretation]
    ? extractVideoId(youtubeUrls[activeInterpretation])
    : ''

  // ── Position callback (from YouTube or FilePlayer) ────────────────
  const onPositionChanged = useCallback((seconds: number) => {
    setPosition(seconds)

    // Stop at interpretation end time
    const end = usePlayerStore.getState().interpEnd
    if (end > 0 && seconds >= end) {
      if (useYouTube) ytRef.current?.pause()
      else filePlayer.pause()
      setPlaying(false)
      return
    }

    if (!syncTimer.hasData || !verovioReady.current) return

    const tick = syncTimer.timeToTick(seconds)

    // Update cursor via Verovio's getElementsAtTime
    const timeMs = seconds * 1000
    const elems = verovio.getElementsAtTime(timeMs)
    if (elems.notes.length > 0) {
      const pos = verovio.getElementPosition(elems.notes[0])
      if (pos) {
        setCursorRect(pos)
      }
    }

    // Advance tied notes for play-along
    for (const voice of voicesRef.current) {
      advanceTiedNotes(voice, tick)
    }
  }, [setPosition, setCursorRect])

  // ── Wire FilePlayer callbacks ─────────────────────────────────────
  useEffect(() => {
    filePlayer.onPositionChange = onPositionChanged
    filePlayer.onPlaybackStarted = () => setPlaying(true)
    filePlayer.onPlaybackPaused = () => setPlaying(false)
    filePlayer.onPlaybackEnded = () => setPlaying(false)
  }, [onPositionChanged, setPlaying])

  // ── Load worlds on startup ────────────────────────────────────────
  useEffect(() => {
    (async () => {
      try {
        const resourcesPath = await window.api.app.getResourcesPath()
        const worldsDir = `${resourcesPath}/resources/worlds`
        const exists = await window.api.fs.exists(worldsDir)
        if (!exists) {
          console.log('No worlds directory found at', worldsDir)
          return
        }
        const files = await window.api.fs.readDir(worldsDir)
        const jsonFiles = files.filter((f: string) => f.endsWith('.json')).sort()

        const loadedWorlds: World[] = []
        for (const file of jsonFiles) {
          try {
            const data = await window.api.fs.readFile(`${worldsDir}/${file}`)
            const json = new TextDecoder().decode(data)
            const obj = JSON.parse(json)
            loadedWorlds.push(parseWorld(obj, worldsDir))
          } catch (e) {
            console.warn('Failed to load world:', file, e)
          }
        }
        loadedWorlds.sort((a, b) => a.order - b.order)
        setWorlds(loadedWorlds)
      } catch (e) {
        console.warn('Failed to load worlds:', e)
      }
    })()
  }, [setWorlds])

  // ── Load a level ──────────────────────────────────────────────────
  const handleLoadLevel = useCallback(async (
    worldIndex: number, sectionIndex: number, levelIndex: number,
  ) => {
    const world = worlds[worldIndex]
    if (!world) return
    const section = world.sections[sectionIndex]
    if (!section) return
    const level = section.levels[levelIndex]
    if (!level) return

    storeLoadLevel(worldIndex, sectionIndex, levelIndex)

    // Stop current playback
    if (useYouTube) {
      ytRef.current?.stop()
    } else {
      filePlayer.stop()
    }
    setPlaying(false)
    clearScore()

    // Load score
    let pages: string[] = []
    if (section.scorePath && verovioReady.current) {
      try {
        const data = await window.api.fs.readFile(section.scorePath)
        const xml = new TextDecoder().decode(data)
        // Compute pageWidth from container: sidebar=260, scale=40% → pixel * 2.5
        const mainEl = document.querySelector('.main-area')
        const containerPx = mainEl ? mainEl.clientWidth : 1100
        const pageWidth = Math.round(containerPx * 2.5)
        verovio.loadMusicXML(xml, { pageWidth })
        // Filter to only the parts specified in the level
        if (level.parts.length > 0) {
          verovio.selectStaves(level.parts)
        }
        for (let i = 0; i < verovio.getPageCount(); i++) {
          // Fix Verovio SVG overflow="visible" which causes negative layout offsets
          pages.push(verovio.renderPage(i).replaceAll('overflow="visible"', 'overflow="hidden"'))
        }
        setSvgPages(pages)
      } catch (e) {
        console.warn('Failed to load score:', e)
      }
    }

    // Load sources
    if (section.sourcesPath) {
      try {
        const data = await window.api.fs.readFile(section.sourcesPath)
        const json = new TextDecoder().decode(data)
        const sourceDir = section.sourcesPath.replace(/\/[^/]+$/, '')
        const sources = parseSources(JSON.parse(json), sourceDir)

        const ytUrls = sources.youtube.map(s => s.url)
        const labels = sources.youtube.map(s => s.label)
        const hasYT = ytUrls.some(u => u.length > 0)

        loadSources({
          youtubeUrls: ytUrls,
          labels,
          tunings: sources.youtube.map(s => s.tuning),
          instrumentVols: sources.youtube.map(s => s.instrumentVolume),
          volumes: sources.youtube.map(s => s.volume),
          beatsFiles: sources.youtube.map(s => s.beatsFile),
          startTimes: sources.youtube.map(s => s.start),
          endTimes: sources.youtube.map(s => s.end),
        }, sourceDir)

        setUseYouTube(hasYT)

        // Apply tuning & volume from the first interpretation
        if (sources.youtube.length > 0) {
          const tuning = sources.youtube[0].tuning ?? 0
          const instrVol = sources.youtube[0].instrumentVolume ?? -1
          setSynthPitch(tuning)
          synthPitchRef.current = tuning
          if (instrVol >= 0) {
            const gain = Math.min(instrVol / 100, 2.0)
            setSynthVolume(gain)
          }
        }

        // Load audio file if not YouTube
        if (!hasYT && sources.audioFile) {
          try {
            await filePlayer.load(sources.audioFile)
            setDuration(filePlayer.getDuration())
          } catch (e) {
            console.warn('Failed to load audio:', e)
          }
        }
      } catch (e) {
        console.warn('Failed to load sources:', e)
      }
    }

    // Load beat data
    syncTimer.clear()
    // setHasTrackingData(false) // tracking disabled
    if (section.beatsPath) {
      try {
        const data = await window.api.fs.readFile(section.beatsPath)
        const json = new TextDecoder().decode(data)
        const beatData = loadBeatDataFromString(json)
        syncTimer.load(beatData)
        // setHasTrackingData(true) // tracking disabled
      } catch (e) {
        console.warn('Failed to load beat data:', e)
      }
    }

    // Build note table for play-along
    // After selectStaves(), parts are renumbered starting from 0
    // Map original part numbers to their position in the filtered set
    const filteredParts = level.parts.length > 0 ? [...level.parts].sort((a, b) => a - b) : []

    voicesRef.current = []
    voiceKeyConfigs.current = []
    voiceKeysHeldRef.current = []
    if (level.voices.length > 0) {
      // Multi-voice
      for (const vc of level.voices) {
        if (vc.playPart > 0 && verovioReady.current) {
          const filteredIndex = filteredParts.indexOf(vc.playPart)
          const partIdx = filteredIndex >= 0 ? filteredIndex : 0
          const notes = verovio.getNotesForPart(partIdx, pages)
          const raw: RawNoteInput[] = notes.map(n => ({
            tick: n.tick,
            pitch: n.pitch,
            duration: n.duration,
            elementId: n.id,
          }))
          const voice = createVoice(
            `Voice ${vc.playPart}`,
            vc.gmProgram,
            voicesRef.current.length,
          )
          voice.notes = buildNoteTable(raw)
          voicesRef.current.push(voice)
          voiceKeyConfigs.current.push({
            keyZone: (vc.keys as 'left' | 'right' | 'all') || 'all',
          })
        }
      }
    } else if (level.playPart > 0 && verovioReady.current) {
      // Single-voice — after filtering, the part is at index 0
      const filteredIndex = filteredParts.indexOf(level.playPart)
      const partIdx = filteredIndex >= 0 ? filteredIndex : 0
      const notes = verovio.getNotesForPart(partIdx, pages)
      const raw: RawNoteInput[] = notes.map(n => ({
        tick: n.tick,
        pitch: n.pitch,
        duration: n.duration,
        elementId: n.id,
      }))
      const voice = createVoice('Voice 1', level.gmProgram, 0)
      voice.notes = buildNoteTable(raw)
      voicesRef.current.push(voice)
      voiceKeyConfigs.current.push({ keyZone: 'all' })
    }

    // Set up synth: load soundfonts and select GM programs per voice
    const resourcesPath = await window.api.app.getResourcesPath()
    const soundsDir = `${resourcesPath}/resources/sounds`
    voiceSfNamesRef.current = []
    for (const voice of voicesRef.current) {
      // Find the soundfont for this voice from the level config
      let sfName = level.soundfont || ''
      if (level.voices.length > 0) {
        const vc = level.voices.find(v => v.playPart === voice.channel + 1 ||
          level.voices.indexOf(v) === voicesRef.current.indexOf(voice))
        if (vc) sfName = vc.soundfont || ''
      }
      voiceSfNamesRef.current.push(sfName)
      // Load soundfont if specified, otherwise use default
      if (sfName) {
        const sfPath = `${soundsDir}/${sfName}`
        try {
          const sfId = await window.api.synth.loadSoundfont(sfPath)
          if (sfId >= 0) {
            voice.sfontId = sfId
          }
        } catch (e) {
          console.warn('Failed to load soundfont:', sfName, e)
        }
      }
      // Select GM program on this voice's channel
      const bank = Math.floor(voice.gmProgram / 128)
      const prog = voice.gmProgram % 128
      const sfId = voice.sfontId >= 0 ? voice.sfontId : 1
      window.api.synth.programSelect(voice.channel, sfId, bank, prog)
      // Apply pitch tuning
      window.api.synth.setPitchOffset(voice.channel, synthPitchRef.current)
    }
    // Apply volume
    window.api.synth.setGain(synthVolume)

    // Update UI with voice info
    setVoiceInfos(voicesRef.current.map((v, i) => ({
      partName: v.partName,
      channel: v.channel,
      gmProgram: v.gmProgram,
      sfontId: v.sfontId,
      soundfontName: voiceSfNamesRef.current[i] || '',
    })))

    // Highlight the first note to play
    const firstNoteIds: string[] = []
    for (const v of voicesRef.current) {
      if (v.notes.length > 0) firstNoteIds.push(v.notes[0].elementId)
    }
    setHighlightIds(firstNoteIds)

    // Auto-start playback (seek to interpretation start time if set)
    setTimeout(() => {
      const start = usePlayerStore.getState().interpStart
      if (start > 0 && useYouTube) {
        ytRef.current?.seekTo(start)
      }
      if (useYouTube) ytRef.current?.play()
      else filePlayer.play()
    }, 200)
  }, [worlds, storeLoadLevel, useYouTube, youtubeUrls, setPlaying, clearScore,
    setSvgPages, setHighlightIds, loadSources, setUseYouTube, setDuration])

  // ── Play/Pause ────────────────────────────────────────────────────
  const handlePlayPause = useCallback(() => {
    if (isPlaying) {
      if (useYouTube) ytRef.current?.pause()
      else filePlayer.pause()
    } else {
      if (useYouTube) ytRef.current?.play()
      else filePlayer.play()
    }
  }, [isPlaying, useYouTube])

  // ── Restart ───────────────────────────────────────────────────────
  const handleRestart = useCallback(() => {
    // Stop and rewind to interpretation start
    const start = interpStart || 0
    for (const v of voicesRef.current) {
      if (v.lastPlayedNote >= 0) SynthBridge.noteOff(v.channel, v.lastPlayedNote)
      resetVoice(v)
    }
    voiceKeysHeldRef.current = voiceKeysHeldRef.current.map(() => 0)
    setPosition(start)
    if (useYouTube) {
      ytRef.current?.seekTo(start)
      ytRef.current?.play()
    } else {
      filePlayer.seekTo(start)
      filePlayer.play()
    }
  }, [useYouTube, interpStart, setPosition])

  // ── Seek ──────────────────────────────────────────────────────────
  const handleSeek = useCallback((seconds: number) => {
    if (useYouTube) ytRef.current?.seekTo(seconds)
    else filePlayer.seekTo(seconds)
    setPosition(seconds)
  }, [useYouTube, setPosition])

  // ── Speed ─────────────────────────────────────────────────────────
  const handleSpeedChange = useCallback((newSpeed: number) => {
    setSpeed(newSpeed)
    if (useYouTube) ytRef.current?.setPlaybackRate(newSpeed)
    else filePlayer.setPlaybackRate(newSpeed)
  }, [useYouTube, setSpeed])

  // ── Apply tuning & volume for an interpretation ──────────────────
  const applyInterpretationSettings = useCallback((index: number) => {
    const tuning = sourceTunings[index] ?? 0
    const instrVol = sourceInstrumentVols[index] ?? -1

    // Apply pitch offset to all voices (both state and ref)
    setSynthPitch(tuning)
    synthPitchRef.current = tuning
    for (const voice of voicesRef.current) {
      window.api.synth.setPitchOffset(voice.channel, tuning)
    }

    // Apply instrument volume (0-200 range in sources → 0-1 gain for FluidSynth)
    if (instrVol >= 0) {
      const gain = Math.min(instrVol / 100, 2.0)
      setSynthVolume(gain)
      window.api.synth.setGain(gain) // scale to FluidSynth 0-5 range
    }
  }, [sourceTunings, sourceInstrumentVols])

  // ── Select interpretation (wraps store + applies synth settings) ──
  const handleSelectInterpretation = useCallback((index: number) => {
    selectInterpretation(index)
    applyInterpretationSettings(index)
    // Seek to new interpretation's start time
    const { sourceStartTimes } = usePlayerStore.getState()
    const start = sourceStartTimes[index] ?? 0
    if (start > 0 && useYouTube) {
      ytRef.current?.seekTo(start)
    }
  }, [selectInterpretation, applyInterpretationSettings, useYouTube])

  // ── Highlight the next note to play ──────────────────────────────
  const updateNextNoteHighlight = useCallback(() => {
    const ids: string[] = []
    for (const voice of voicesRef.current) {
      if (voice.nextIndex < voice.notes.length) {
        ids.push(voice.notes[voice.nextIndex].elementId)
      }
    }
    setHighlightIds(ids)
  }, [setHighlightIds])

  // ── Keyboard handler (play-along) ─────────────────────────────────
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      // Cmd+Shift+R = hard reload (default browser behavior, let through)
      // Cmd+R = restart level
      if ((e.metaKey || e.ctrlKey) && (e.key === 'r' || e.key === 'R')) {
        if (e.shiftKey) return // let Cmd+Shift+R through for page reload
        if (playModeActive) {
          e.preventDefault()
          handleRestart()
          updateNextNoteHighlight()
          return
        }
      }
      // Let other Cmd+shortcuts through
      if (e.metaKey || e.ctrlKey) return

      // Space = play/pause
      if (e.key === ' ') {
        e.preventDefault()
        handlePlayPause()
        return
      }

      // Play-along key handling
      if (!playModeActive || !isPlaying) return
      if (!isLetterKey(e.key)) return
      if (e.repeat) return

      const voiceIndices = getVoiceIndicesForKey(e.key, voiceKeyConfigs.current)

      // Play all matching voices in a tight loop (minimize inter-voice latency)
      for (const vi of voiceIndices) {
        const voice = voicesRef.current[vi]
        if (!voice || voice.nextIndex >= voice.notes.length) continue

        const note = voice.notes[voice.nextIndex]

        // Apply pitch offset
        // Match Qt: use truncation toward zero (not rounding) for integer semitone offset
        const pitch = Math.max(0, Math.min(127, note.midiPitch + Math.trunc(synthPitchRef.current)))

        // Turn off previous note, then play new one — same call, no IPC gap
        if (voice.lastPlayedNote >= 0) {
          SynthBridge.noteOff(voice.channel, voice.lastPlayedNote)
        }
        SynthBridge.noteOn(voice.channel, pitch, 100)
        voice.lastPlayedNote = pitch

        // Increment per-voice key counter
        if (!voiceKeysHeldRef.current[vi]) voiceKeysHeldRef.current[vi] = 0
        voiceKeysHeldRef.current[vi]++

        // Advance
        voice.nextIndex++
        skipTiedContinuations(voice)
      }

      // Defer highlight update so it doesn't block the next keypress
      requestAnimationFrame(() => updateNextNoteHighlight())
    }

    const handleKeyUp = (e: KeyboardEvent) => {
      if (!isLetterKey(e.key)) return

      // Only send noteOff when ALL keys for this voice are released (legato overlap)
      const voiceIndices = getVoiceIndicesForKey(e.key, voiceKeyConfigs.current)
      for (const vi of voiceIndices) {
        if (!voiceKeysHeldRef.current[vi]) continue
        voiceKeysHeldRef.current[vi] = Math.max(0, voiceKeysHeldRef.current[vi] - 1)
        if (voiceKeysHeldRef.current[vi] === 0) {
          const voice = voicesRef.current[vi]
          if (voice && voice.lastPlayedNote >= 0) {
            SynthBridge.noteOff(voice.channel, voice.lastPlayedNote)
            voice.lastPlayedNote = -1
          }
        }
      }
    }

    window.addEventListener('keydown', handleKeyDown)
    window.addEventListener('keyup', handleKeyUp)
    return () => {
      window.removeEventListener('keydown', handleKeyDown)
      window.removeEventListener('keyup', handleKeyUp)
    }
  }, [playModeActive, isPlaying, handlePlayPause, handleRestart, updateNextNoteHighlight])

  // ── Computed ──────────────────────────────────────────────────────
  const currentWorld = worlds[currentWorldIndex] ?? null
  const showScoreView = playModeActive && svgPages.length > 0
  const showLevelBrowser = !playModeActive

  return (
    <div className="app-shell">
      {/* Left sidebar — world cards + video */}
      {sidebarVisible && (
        <WorldSidebar
          worlds={worlds}
          selectedWorldIndex={currentWorldIndex}
          onWorldSelect={selectWorld}
          videoExpanded={videoExpanded}
          onVideoExpandToggle={toggleVideoExpand}
          videoSlotRef={setSidebarVideoEl}
          interpretations={sourceLabels.length > 0 ? sourceLabels : undefined}
          activeInterpretation={activeInterpretation}
          onInterpretationSelect={handleSelectInterpretation}
          showVolume={useYouTube}
          volume={volume}
          onVolumeChange={(v) => {
            setVolume(v)
            ytRef.current?.setVolume(v)
          }}
        />
      )}

      {/* Center area */}
      <main className="main-area">
        {/* Player controls — top bar */}
        {playModeActive && (
          <PlayerControls
            isPlaying={isPlaying}
            currentTime={currentTime}
            duration={duration}
            speed={speed}
            showInstrument={showInstrumentPanel}
            onPlayPause={handlePlayPause}
            onRestart={handleRestart}
            onSeek={handleSeek}
            onSpeedChange={handleSpeedChange}
            onToggleInstrument={() => setShowInstrumentPanel(p => !p)}
          />
        )}

        {/* Spacer when video is expanded — pushes score content down */}
        {videoExpanded && videoId && (
          <div style={{
            height: '33%',
            minHeight: 180,
            flexShrink: 0,
          }} />
        )}

        {/* Score or Level browser */}
        {showScoreView ? (
          <div style={{ display: 'flex', flexDirection: 'column', flex: 1, minHeight: 0 }}>
            <ScoreView
              svgPages={svgPages}
              zoom={zoom}
              cursorRect={cursorRect ?? undefined}
              highlightIds={highlightIds}
              highlightIds2={highlightIds2}
              autoScroll={autoScroll}
            />
            {showInstrumentPanel && voiceInfos.length > 0 && (
              <div style={{
                borderTop: `1px solid ${theme.popupBg}`,
                background: theme.surfaceBg,
                padding: '6px 12px',
                flexShrink: 0,
              }}>
                <InstrumentPanel
                  voices={voiceInfos}
                  soundfonts={availableSoundfonts}
                  volume={synthVolume}
                  pitchOffset={synthPitch}
                  onProgramChange={(vi, prog) => {
                    const voice = voicesRef.current[vi]
                    if (!voice) return
                    voice.gmProgram = prog
                    const bank = Math.floor(prog / 128)
                    const p = prog % 128
                    const sfId = voice.sfontId >= 0 ? voice.sfontId : 1
                    window.api.synth.programSelect(voice.channel, sfId, bank, p)
                    setVoiceInfos(voicesRef.current.map((v, i) => ({
                      partName: v.partName, channel: v.channel,
                      gmProgram: v.gmProgram, sfontId: v.sfontId,
                      soundfontName: voiceSfNamesRef.current[i] || '',
                    })))
                  }}
                  onSoundfontChange={async (vi, sfName) => {
                    const voice = voicesRef.current[vi]
                    if (!voice) return
                    const resourcesPath = await window.api.app.getResourcesPath()
                    const sfPath = `${resourcesPath}/resources/sounds/${sfName}`
                    const sfId = await window.api.synth.loadSoundfont(sfPath)
                    if (sfId >= 0) {
                      voice.sfontId = sfId
                      voiceSfNamesRef.current[vi] = sfName
                      // Re-select program on new soundfont
                      const bank = Math.floor(voice.gmProgram / 128)
                      const prog = voice.gmProgram % 128
                      window.api.synth.programSelect(voice.channel, sfId, bank, prog)
                      setVoiceInfos(voicesRef.current.map((v, i) => ({
                        partName: v.partName, channel: v.channel,
                        gmProgram: v.gmProgram, sfontId: v.sfontId,
                        soundfontName: voiceSfNamesRef.current[i] || '',
                      })))
                    }
                  }}
                  onVolumeChange={(gain) => {
                    setSynthVolume(gain)
                    window.api.synth.setGain(gain)
                  }}
                  onPitchChange={(semitones) => {
                    setSynthPitch(semitones)
                    synthPitchRef.current = semitones
                    for (const voice of voicesRef.current) {
                      window.api.synth.setPitchOffset(voice.channel, semitones)
                    }
                  }}
                />
              </div>
            )}
          </div>
        ) : showLevelBrowser ? (
          <LevelBrowser
            world={currentWorld}
            currentSection={activeSectionIndex}
            currentLevel={activeLevelIndex}
            onLevelSelect={(si, li) =>
              handleLoadLevel(currentWorldIndex, si, li)
            }
            onResume={() => {
              // Resume play mode
              useAppStore.setState({ playModeActive: true })
            }}
          />
        ) : (
          <div className="score-view">
            <div className="score-placeholder">
              Open a MusicXML file to begin
            </div>
          </div>
        )}

        {/* YouTube player — portalled into sidebar video slot */}
        {videoId && sidebarVideoEl && createPortal(
          <YouTubePlayer
            ref={ytRef}
            videoId={videoId}
            onReady={(dur) => setDuration(dur)}
            onPositionChange={onPositionChanged}
            onPlaybackStarted={() => setPlaying(true)}
            onPlaybackPaused={() => setPlaying(false)}
            onPlaybackStopped={() => setPlaying(false)}
            onPlaybackRateChange={(rate) => setSpeed(rate)}
          />,
          sidebarVideoEl,
        )}

      </main>
    </div>
  )
}

export default App
