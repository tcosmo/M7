import { create } from 'zustand'

/* ── Source data shape (parsed from .sources JSON) ───────────────── */

export interface SourceData {
  youtubeUrls?: string[]
  labels?: string[]
  tunings?: number[]
  instrumentVols?: number[]
  volumes?: number[]
  beatsFiles?: string[]
  startTimes?: number[]
  endTimes?: number[]
}

/* ── State ───────────────────────────────────────────────────────── */

export interface PlayerState {
  // Current player
  useYouTube: boolean
  isPlaying: boolean
  currentTime: number
  duration: number
  speed: number
  volume: number

  // Sources
  youtubeUrls: string[]
  sourceLabels: string[]
  sourceTunings: number[]
  sourceInstrumentVols: number[]
  sourceVolumes: number[]
  sourceBeatsFiles: string[]
  sourceStartTimes: number[]
  sourceEndTimes: number[]
  activeInterpretation: number
  interpStart: number
  interpEnd: number

  // Tracking
  trackingEnabled: boolean
  hasTrackingData: boolean

  // Actions
  setPosition: (seconds: number) => void
  setPlaying: (playing: boolean) => void
  setDuration: (duration: number) => void
  setSpeed: (speed: number) => void
  setVolume: (volume: number) => void
  setUseYouTube: (use: boolean) => void
  setTrackingEnabled: (enabled: boolean) => void
  setHasTrackingData: (has: boolean) => void
  loadSources: (sourcesData: SourceData, basePath: string) => void
  selectInterpretation: (index: number) => void
}

/* ── Store ────────────────────────────────────────────────────────── */

export const usePlayerStore = create<PlayerState>((set, get) => ({
  // Defaults
  useYouTube: false,
  isPlaying: false,
  currentTime: 0,
  duration: 0,
  speed: 1,
  volume: 100,

  youtubeUrls: [],
  sourceLabels: [],
  sourceTunings: [],
  sourceInstrumentVols: [],
  sourceVolumes: [],
  sourceBeatsFiles: [],
  sourceStartTimes: [],
  sourceEndTimes: [],
  activeInterpretation: 0,
  interpStart: 0,
  interpEnd: 0,

  trackingEnabled: false,
  hasTrackingData: false,

  // Actions
  setPosition: (seconds) => set({ currentTime: seconds }),
  setPlaying: (playing) => set({ isPlaying: playing }),
  setDuration: (duration) => set({ duration }),
  setSpeed: (speed) => set({ speed }),
  setVolume: (volume) => set({ volume }),
  setUseYouTube: (use) => set({ useYouTube: use }),
  setTrackingEnabled: (enabled) => set({ trackingEnabled: enabled }),
  setHasTrackingData: (has) => set({ hasTrackingData: has }),

  loadSources: (data: SourceData, basePath: string) => {
    const count = data.labels?.length ?? data.youtubeUrls?.length ?? 0
    const hasYouTube =
      (data.youtubeUrls?.length ?? 0) > 0 &&
      data.youtubeUrls!.some((u) => u.length > 0)

    // Resolve beats file paths relative to basePath
    const beatsFiles = (data.beatsFiles ?? []).map((f) =>
      f ? `${basePath}/${f}` : '',
    )

    set({
      youtubeUrls: data.youtubeUrls ?? [],
      sourceLabels: data.labels ?? Array.from({ length: count }, (_, i) => `Source ${i + 1}`),
      sourceTunings: data.tunings ?? new Array(count).fill(0),
      sourceInstrumentVols: data.instrumentVols ?? new Array(count).fill(100),
      sourceVolumes: data.volumes ?? new Array(count).fill(100),
      sourceBeatsFiles: beatsFiles,
      sourceStartTimes: data.startTimes ?? new Array(count).fill(0),
      sourceEndTimes: data.endTimes ?? new Array(count).fill(0),
      useYouTube: hasYouTube,
      activeInterpretation: 0,
      interpStart: data.startTimes?.[0] ?? 0,
      interpEnd: data.endTimes?.[0] ?? 0,
    })
  },

  selectInterpretation: (index: number) => {
    const state = get()
    if (index < 0 || index >= state.sourceLabels.length) return
    set({
      activeInterpretation: index,
      interpStart: state.sourceStartTimes[index] ?? 0,
      interpEnd: state.sourceEndTimes[index] ?? 0,
    })
  },
}))
