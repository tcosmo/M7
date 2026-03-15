import { create } from 'zustand'

export interface ScoreState {
  // SVG pages from Verovio
  svgPages: string[]
  pageCount: number
  zoom: number

  // Cursor position
  cursorRect: {
    x: number
    y: number
    width: number
    height: number
    pageIndex: number
  } | null

  // Highlighted note IDs
  highlightIds: string[]
  highlightIds2: string[]

  // Auto-scroll
  autoScroll: boolean

  // Visible parts (1-based part numbers)
  visibleParts: number[]

  // Actions
  setSvgPages: (pages: string[]) => void
  setZoom: (zoom: number) => void
  setCursorRect: (rect: ScoreState['cursorRect']) => void
  setHighlightIds: (ids: string[]) => void
  setHighlightIds2: (ids: string[]) => void
  toggleAutoScroll: () => void
  setAutoScroll: (on: boolean) => void
  setVisibleParts: (parts: number[]) => void
  clear: () => void
}

export const useScoreStore = create<ScoreState>((set) => ({
  svgPages: [],
  pageCount: 0,
  zoom: 1.0,
  cursorRect: null,
  highlightIds: [],
  highlightIds2: [],
  autoScroll: true,
  visibleParts: [],

  setSvgPages: (pages) => set({ svgPages: pages, pageCount: pages.length }),
  setZoom: (zoom) => set({ zoom }),
  setCursorRect: (rect) => set({ cursorRect: rect }),
  setHighlightIds: (ids) => set({ highlightIds: ids }),
  setHighlightIds2: (ids) => set({ highlightIds2: ids }),
  toggleAutoScroll: () => set((s) => ({ autoScroll: !s.autoScroll })),
  setAutoScroll: (on) => set({ autoScroll: on }),
  setVisibleParts: (parts) => set({ visibleParts: parts }),
  clear: () =>
    set({
      svgPages: [],
      pageCount: 0,
      cursorRect: null,
      highlightIds: [],
      highlightIds2: [],
    }),
}))
