import React from 'react'
import type { World } from '../core/WorldLoader'
import { theme } from '../theme/theme'
import { WorldCard } from './WorldCard'
import { InterpretationSelect } from './InterpretationSelect'
import { VolumeSlider } from './VolumeSlider'

export interface WorldSidebarProps {
  worlds: World[]
  selectedWorldIndex: number
  onWorldSelect: (index: number) => void
  // Video
  videoExpanded: boolean
  onVideoExpandToggle: () => void
  /** Ref callback to get the video slot DOM element for portal rendering */
  videoSlotRef?: (el: HTMLDivElement | null) => void
  // Interpretations
  interpretations?: string[]
  activeInterpretation?: number
  onInterpretationSelect?: (index: number) => void
  // Volume
  showVolume?: boolean
  volume?: number
  onVolumeChange?: (percent: number) => void
  // Extra content (e.g. InstrumentPanel)
  children?: React.ReactNode
}

const styles = {
  root: {
    width: 260,
    minWidth: 260,
    maxWidth: 260,
    display: 'flex',
    flexDirection: 'column' as const,
    background: theme.surfaceBg,
    borderRight: `1px solid ${theme.popupBg}`,
    height: '100%',
    userSelect: 'none' as const,
  },
  header: {
    height: 40,
    display: 'flex',
    alignItems: 'center',
    paddingLeft: 72, // Clear macOS traffic lights
    fontSize: 13,
    fontWeight: 700,
    color: theme.textPrimary,
    flexShrink: 0,
    borderBottom: `1px solid ${theme.popupBg}`,
    WebkitAppRegion: 'drag' as any,
  },
  scrollArea: {
    flex: 1,
    overflowY: 'auto' as const,
    overflowX: 'hidden' as const,
    padding: '4px 0',
  },
  expandBar: {
    display: 'flex',
    justifyContent: 'center',
    alignItems: 'center',
    height: 20,
    cursor: 'pointer',
    background: theme.panelBg,
    color: theme.textSecondary,
    fontSize: 10,
    flexShrink: 0,
    borderTop: `1px solid ${theme.popupBg}`,
    borderBottom: `1px solid ${theme.popupBg}`,
    userSelect: 'none' as const,
    transition: 'background 0.1s ease',
  },
  videoSlot: (expanded: boolean): React.CSSProperties => ({
    height: 200,
    overflow: 'hidden',
    flexShrink: 0,
    background: '#000',
    transition: 'height 0.2s ease',
    ...(expanded ? {
      // When expanded, break out of sidebar to cover the main area
      position: 'fixed' as const,
      left: 260, // sidebar width
      top: 0,
      right: 0,
      height: '33%',
      minHeight: 180,
      zIndex: 20,
    } : {}),
  }),
}

export function WorldSidebar({
  worlds,
  selectedWorldIndex,
  onWorldSelect,
  videoExpanded,
  onVideoExpandToggle,
  videoSlotRef,
  interpretations,
  activeInterpretation,
  onInterpretationSelect,
  showVolume,
  volume,
  onVolumeChange,
  children,
}: WorldSidebarProps): React.ReactElement {
  return (
    <div style={styles.root}>
      {/* Header */}
      <div style={styles.header}>Worlds</div>

      {/* Scrollable world cards */}
      <div style={styles.scrollArea}>
        {worlds.map((world, i) => (
          <WorldCard
            key={world.id || i}
            world={world}
            selected={i === selectedWorldIndex}
            onClick={() => onWorldSelect(i)}
          />
        ))}
      </div>

      {/* Expand/collapse bar for video */}
      <div
        style={styles.expandBar}
        onClick={onVideoExpandToggle}
        onMouseEnter={(e) => {
          e.currentTarget.style.background = theme.inputBg
        }}
        onMouseLeave={(e) => {
          e.currentTarget.style.background = theme.panelBg
        }}
      >
        {videoExpanded ? '\u25BC Video' : '\u25B2 Video'}
      </div>

      {/* Video container slot — portal target for YouTube player */}
      <div ref={videoSlotRef} style={styles.videoSlot(videoExpanded)} />

      {/* Interpretation list */}
      {interpretations && interpretations.length > 0 && onInterpretationSelect && (
        <InterpretationSelect
          labels={interpretations}
          activeIndex={activeInterpretation ?? 0}
          onSelect={onInterpretationSelect}
        />
      )}

      {/* Volume slider */}
      {showVolume && onVolumeChange && (
        <VolumeSlider
          value={volume ?? 80}
          onChange={onVolumeChange}
        />
      )}

      {/* Extra content (InstrumentPanel etc.) */}
      {children}
    </div>
  )
}
