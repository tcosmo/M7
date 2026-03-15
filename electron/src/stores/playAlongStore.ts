import { create } from 'zustand'

export interface PlayAlongState {
  // Recording
  recordTrackingActive: boolean
  beatDataFromRecording: boolean

  // Key state
  keysHeld: number

  // Actions
  setRecordTrackingActive: (active: boolean) => void
  setBeatDataFromRecording: (fromRecording: boolean) => void
  setKeysHeld: (count: number) => void
  incrementKeysHeld: () => void
  decrementKeysHeld: () => void
}

export const usePlayAlongStore = create<PlayAlongState>((set) => ({
  recordTrackingActive: false,
  beatDataFromRecording: false,
  keysHeld: 0,

  setRecordTrackingActive: (active) => set({ recordTrackingActive: active }),
  setBeatDataFromRecording: (fromRecording) =>
    set({ beatDataFromRecording: fromRecording }),
  setKeysHeld: (count) => set({ keysHeld: count }),
  incrementKeysHeld: () => set((s) => ({ keysHeld: s.keysHeld + 1 })),
  decrementKeysHeld: () =>
    set((s) => ({ keysHeld: Math.max(0, s.keysHeld - 1) })),
}))
