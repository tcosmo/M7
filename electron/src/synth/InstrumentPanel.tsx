/**
 * InstrumentPanel — per-voice instrument preset, soundfont, volume, and pitch controls.
 */
import React from 'react'
import { theme } from '../theme/theme'

// Common GM presets for quick selection
const GM_PRESETS = [
  { name: 'Acoustic Grand Piano', program: 0 },
  { name: 'Nylon Guitar', program: 24 },
  { name: 'Steel Guitar', program: 25 },
  { name: 'Acoustic Bass', program: 32 },
  { name: 'Electric Bass (finger)', program: 33 },
  { name: 'Electric Bass (pick)', program: 34 },
  { name: 'Violin', program: 40 },
  { name: 'Cello', program: 42 },
  { name: 'Orchestral Harp', program: 46 },
  { name: 'Timpani', program: 47 },
  { name: 'String Ensemble', program: 48 },
  { name: 'Trumpet', program: 56 },
  { name: 'French Horn', program: 60 },
  { name: 'Oboe', program: 68 },
  { name: 'Clarinet', program: 71 },
  { name: 'Flute', program: 73 },
  { name: 'Recorder', program: 74 },
]

export interface VoiceInfo {
  partName: string
  channel: number
  gmProgram: number
  sfontId: number
  soundfontName: string
}

export interface InstrumentPanelProps {
  voices: VoiceInfo[]
  soundfonts: string[]
  volume: number
  pitchOffset: number
  onProgramChange: (voiceIndex: number, program: number) => void
  onSoundfontChange: (voiceIndex: number, soundfontName: string) => void
  onVolumeChange: (gain: number) => void
  onPitchChange: (semitones: number) => void
}

export function InstrumentPanel({
  voices,
  soundfonts,
  volume,
  pitchOffset,
  onProgramChange,
  onSoundfontChange,
  onVolumeChange,
  onPitchChange,
}: InstrumentPanelProps): React.ReactElement {
  if (voices.length === 0) return <></>

  return (
    <div style={styles.root}>
      <div style={styles.row}>
        {/* Per-voice: soundfont + preset */}
        {voices.map((voice, i) => (
          <React.Fragment key={i}>
            <span style={styles.label}>{voice.partName}</span>
            <select
              style={styles.select}
              value={voice.soundfontName}
              onChange={(e) => onSoundfontChange(i, e.target.value)}
              title="Soundfont"
            >
              {soundfonts.map((sf) => (
                <option key={sf} value={sf}>{sf.replace(/\.[^.]+$/, '')}</option>
              ))}
            </select>
            <select
              style={styles.select}
              value={voice.gmProgram}
              onChange={(e) => onProgramChange(i, parseInt(e.target.value))}
              title="Instrument"
            >
              {GM_PRESETS.map((p) => (
                <option key={p.program} value={p.program}>{p.name}</option>
              ))}
            </select>
            {i < voices.length - 1 && <div style={styles.separator} />}
          </React.Fragment>
        ))}

        <div style={styles.separator} />

        {/* Volume */}
        <span style={styles.label}>Vol</span>
        <input
          type="range"
          min={0}
          max={100}
          value={Math.round(volume * 100)}
          onChange={(e) => onVolumeChange(parseInt(e.target.value) / 100)}
          style={styles.slider}
        />
        <span style={styles.value}>{Math.round(volume * 100)}%</span>

        <div style={styles.separator} />

        {/* Pitch — numeric up/down */}
        <span style={styles.label}>Pitch</span>
        <input
          type="number"
          min={-24}
          max={24}
          step={0.1}
          value={pitchOffset}
          onChange={(e) => onPitchChange(parseFloat(e.target.value) || 0)}
          style={styles.pitchInput}
        />
      </div>
    </div>
  )
}

const styles: Record<string, React.CSSProperties> = {
  root: {
    flexShrink: 0,
  },
  row: {
    display: 'flex',
    alignItems: 'center',
    gap: 6,
  },
  label: {
    fontSize: 11,
    color: theme.textSecondary,
    whiteSpace: 'nowrap' as const,
  },
  select: {
    fontSize: 11,
    maxWidth: 160,
  },
  separator: {
    width: 1,
    height: 16,
    background: theme.popupBg,
    flexShrink: 0,
  },
  slider: {
    width: 80,
    height: 4,
    cursor: 'pointer',
    flexShrink: 0,
  },
  value: {
    fontSize: 10,
    color: theme.textSecondary,
    minWidth: 32,
    fontVariantNumeric: 'tabular-nums',
  },
  pitchInput: {
    width: 60,
    fontSize: 11,
    textAlign: 'center' as const,
  },
}
