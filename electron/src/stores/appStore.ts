import { create } from 'zustand'
import type { World } from '../core/WorldLoader'

export interface AppState {
  // Worlds
  worlds: World[]
  currentWorldIndex: number

  // Active level (the level currently loaded / playing)
  activeWorldIndex: number
  activeSectionIndex: number
  activeLevelIndex: number

  // Play mode
  playModeActive: boolean

  // Video
  videoExpanded: boolean

  // Sidebar
  sidebarVisible: boolean

  // Actions
  setWorlds: (worlds: World[]) => void
  selectWorld: (index: number) => void
  loadLevel: (worldIndex: number, sectionIndex: number, levelIndex: number) => void
  toggleVideoExpand: () => void
  toggleSidebar: () => void
}

export const useAppStore = create<AppState>((set) => ({
  // State
  worlds: [],
  currentWorldIndex: 0,

  activeWorldIndex: -1,
  activeSectionIndex: -1,
  activeLevelIndex: -1,

  playModeActive: false,

  videoExpanded: false,

  sidebarVisible: true,

  // Actions
  setWorlds: (worlds) =>
    set({ worlds, currentWorldIndex: worlds.length > 0 ? 0 : -1 }),

  selectWorld: (index) =>
    set({ currentWorldIndex: index, playModeActive: false }),

  loadLevel: (worldIndex, sectionIndex, levelIndex) =>
    set({
      activeWorldIndex: worldIndex,
      activeSectionIndex: sectionIndex,
      activeLevelIndex: levelIndex,
      playModeActive: true,
    }),

  toggleVideoExpand: () =>
    set((s) => ({ videoExpanded: !s.videoExpanded })),

  toggleSidebar: () =>
    set((s) => ({ sidebarVisible: !s.sidebarVisible })),
}))
