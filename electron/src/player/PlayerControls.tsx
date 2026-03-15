import React, { useCallback } from 'react'

/* ── Types ───────────────────────────────────────────────────────── */

export interface PlayerControlsProps {
  isPlaying: boolean
  currentTime: number
  duration: number
  speed: number
  showInstrument?: boolean
  onPlayPause: () => void
  onRestart: () => void
  onSeek: (seconds: number) => void
  onSpeedChange: (speed: number) => void
  onToggleInstrument?: () => void
}

/* ── Helpers ─────────────────────────────────────────────────────── */

const SPEED_OPTIONS = [0.25, 0.5, 0.75, 1, 1.25, 1.5, 2]

function formatTime(seconds: number): string {
  if (!Number.isFinite(seconds) || seconds < 0) return '0:00'
  const mins = Math.floor(seconds / 60)
  const secs = Math.floor(seconds % 60)
  return `${mins}:${secs.toString().padStart(2, '0')}`
}

/* ── Component ───────────────────────────────────────────────────── */

export default function PlayerControls(props: PlayerControlsProps): React.ReactElement {
  const {
    isPlaying,
    currentTime,
    duration,
    speed,
    showInstrument,
    onPlayPause,
    onRestart,
    onSeek,
    onSpeedChange,
    onToggleInstrument,
  } = props

  /* ── Seek slider ───────────────────────────────────────────────── */

  const handleSeek = useCallback(
    (e: React.ChangeEvent<HTMLInputElement>) => {
      onSeek(parseFloat(e.target.value))
    },
    [onSeek],
  )

  /* ── Speed dropdown ────────────────────────────────────────────── */

  const handleSpeedChange = useCallback(
    (e: React.ChangeEvent<HTMLSelectElement>) => {
      onSpeedChange(parseFloat(e.target.value))
    },
    [onSpeedChange],
  )

  /* ── Slider fill percentage ────────────────────────────────────── */

  const pct = duration > 0 ? (currentTime / duration) * 100 : 0

  return (
    <div style={styles.container}>
      {/* Play / Pause */}
      <button
        className="toolbar-btn"
        onClick={onPlayPause}
        title={isPlaying ? 'Pause' : 'Play'}
        style={styles.btn}
      >
        {isPlaying ? '\u275A\u275A' : '\u25B6'}
      </button>

      {/* Restart */}
      <button
        className="toolbar-btn"
        onClick={onRestart}
        title="Restart"
        style={styles.btn}
      >
        {'\u23EE'}
      </button>

      {/* Seek slider */}
      <input
        type="range"
        min={0}
        max={duration || 1}
        step={0.1}
        value={currentTime}
        onChange={handleSeek}
        style={{
          ...styles.slider,
          background: `linear-gradient(to right, var(--accent) ${pct}%, var(--input-bg) ${pct}%)`,
        }}
        title={formatTime(currentTime)}
      />

      {/* Time label */}
      <span style={styles.time}>
        {formatTime(currentTime)} / {formatTime(duration)}
      </span>

      {/* Speed dropdown */}
      <select
        value={speed}
        onChange={handleSpeedChange}
        style={styles.speed}
        title="Playback speed"
      >
        {SPEED_OPTIONS.map((s) => (
          <option key={s} value={s}>
            {s}x
          </option>
        ))}
      </select>

      {/* Instrument panel toggle */}
      {onToggleInstrument && (
        <button
          className="toolbar-btn"
          onClick={onToggleInstrument}
          title="Instrument settings"
          style={{
            ...styles.btn,
            fontSize: 11,
            fontWeight: 600,
            width: 'auto',
            padding: '0 6px',
            color: showInstrument ? 'var(--accent)' : undefined,
          }}
        >
          Instruments
        </button>
      )}
    </div>
  )
}

/* ── Styles ──────────────────────────────────────────────────────── */

const styles: Record<string, React.CSSProperties> = {
  container: {
    display: 'flex',
    alignItems: 'center',
    gap: 4,
    height: 28,
    padding: '0 6px',
    background: 'var(--panel-bg)',
    borderBottom: '1px solid var(--surface-bg)',
    userSelect: 'none',
    position: 'relative',
  },
  btn: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    width: 24,
    height: 24,
    padding: 0,
    fontSize: 12,
  },
  slider: {
    flex: 1,
    height: 4,
    appearance: 'none' as const,
    borderRadius: 2,
    outline: 'none',
    cursor: 'pointer',
  },
  time: {
    fontSize: 11,
    color: 'var(--text-secondary)',
    whiteSpace: 'nowrap' as const,
    minWidth: 80,
    textAlign: 'center' as const,
    fontVariantNumeric: 'tabular-nums',
  },
  speed: {
    width: 52,
    textAlign: 'center' as const,
  },
}
