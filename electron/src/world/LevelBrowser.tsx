import React, { useState } from 'react'
import type { World, Section, Level } from '../core/WorldLoader'
import { theme } from '../theme/theme'
import { localFileUrl } from '../core/localFileUrl'

export interface LevelBrowserProps {
  world: World | null
  currentSection?: number
  currentLevel?: number
  onLevelSelect: (sectionIndex: number, levelIndex: number) => void
  onResume?: () => void
}

// -- Styles -------------------------------------------------------------------

const styles = {
  root: {
    flex: 1,
    overflowY: 'auto' as const,
    overflowX: 'hidden' as const,
    background: theme.contentBg,
  },
  placeholder: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    height: '100%',
    color: theme.textDisabled,
    fontSize: 15,
  },
  content: {
    padding: '24px 32px',
  },
  // Header block (cover + title)
  headerRow: {
    display: 'flex',
    gap: 20,
    marginBottom: 20,
  },
  coverImage: {
    width: 120,
    height: 120,
    borderRadius: 8,
    objectFit: 'cover' as const,
    flexShrink: 0,
  },
  titleBlock: {
    display: 'flex',
    flexDirection: 'column' as const,
    gap: 4,
    paddingTop: 8,
    minWidth: 0,
    flex: 1,
  },
  composer: {
    fontSize: 12,
    color: theme.textHint,
  },
  worldTitle: {
    fontSize: 22,
    fontWeight: 700,
    color: theme.textPrimary,
    lineHeight: 1.2,
    wordWrap: 'break-word' as const,
  },
  catalogue: {
    fontSize: 11,
    color: theme.textSecondary,
  },
  // Levels heading
  levelsHeading: {
    fontSize: 16,
    fontWeight: 700,
    color: theme.textPrimary,
    marginBottom: 8,
  },
  // Section
  sectionTitle: {
    fontSize: 14,
    fontWeight: 700,
    color: theme.textPrimary,
    paddingTop: 8,
    marginBottom: 6,
  },
  comingSoon: {
    fontSize: 11,
    fontStyle: 'italic' as const,
    color: theme.textDisabled,
    paddingLeft: 8,
    marginBottom: 8,
  },
  // Level card
  levelCard: (isCurrent: boolean, hovered: boolean): React.CSSProperties => ({
    display: 'flex',
    alignItems: 'center',
    height: 56,
    maxWidth: 520,
    padding: '0 16px',
    borderRadius: 8,
    marginBottom: 4,
    cursor: 'pointer',
    background: isCurrent
      ? theme.inputBg
      : hovered
        ? theme.inputBg
        : theme.panelBg,
    border: isCurrent ? `2px solid ${theme.accent}` : '2px solid transparent',
    transition: 'background 0.1s ease, border-color 0.1s ease',
  }),
  playDot: (isCurrent: boolean): React.CSSProperties => ({
    width: 16,
    fontSize: 10,
    color: isCurrent ? theme.accent : 'transparent',
    flexShrink: 0,
    marginRight: 4,
  }),
  levelName: (isCurrent: boolean): React.CSSProperties => ({
    fontSize: 13,
    fontWeight: 700,
    color: isCurrent ? theme.accent : theme.textPrimary,
    flex: 1,
    minWidth: 0,
  }),
  levelDesc: {
    fontSize: 11,
    color: theme.textHint,
    marginLeft: 8,
  },
  playBtn: (hovered: boolean): React.CSSProperties => ({
    flexShrink: 0,
    width: 72,
    height: 32,
    borderRadius: 6,
    border: 'none',
    fontSize: 12,
    fontWeight: 700,
    color: '#ffffff',
    background: hovered ? theme.accentHover : theme.accent,
    cursor: 'pointer',
    fontFamily: 'inherit',
    transition: 'background 0.1s ease',
  }),
  spacer: {
    height: 8,
  },
}

// -- Sub-components -----------------------------------------------------------

function LevelCardRow({
  level,
  isCurrent,
  onPlay,
}: {
  level: Level
  isCurrent: boolean
  onPlay: () => void
}): React.ReactElement {
  const [hovered, setHovered] = useState(false)
  const [btnHovered, setBtnHovered] = useState(false)

  return (
    <div
      style={styles.levelCard(isCurrent, hovered)}
      onMouseEnter={() => setHovered(true)}
      onMouseLeave={() => setHovered(false)}
    >
      {/* Play indicator */}
      <span style={styles.playDot(isCurrent)}>{'\u25B6'}</span>

      {/* Name */}
      <span style={styles.levelName(isCurrent)}>{level.title}</span>

      {/* Description */}
      {level.description && (
        <span style={styles.levelDesc}>{level.description}</span>
      )}

      <div style={{ width: 16 }} />

      {/* Play / Resume button */}
      <button
        style={styles.playBtn(btnHovered)}
        onClick={(e) => {
          e.stopPropagation()
          onPlay()
        }}
        onMouseEnter={() => setBtnHovered(true)}
        onMouseLeave={() => setBtnHovered(false)}
      >
        {isCurrent ? 'Resume' : 'Play'}
      </button>
    </div>
  )
}

function SectionBlock({
  section,
  sectionIndex,
  currentSection,
  currentLevel,
  onLevelSelect,
  onResume,
}: {
  section: Section
  sectionIndex: number
  currentSection?: number
  currentLevel?: number
  onLevelSelect: (si: number, li: number) => void
  onResume?: () => void
}): React.ReactElement {
  return (
    <>
      <div style={styles.sectionTitle}>{section.title}</div>

      {section.levels.length === 0 && (
        <div style={styles.comingSoon}>Coming soon...</div>
      )}

      {section.levels.map((level, li) => {
        const isCurrent =
          sectionIndex === currentSection && li === currentLevel
        return (
          <LevelCardRow
            key={level.id || li}
            level={level}
            isCurrent={isCurrent}
            onPlay={() => {
              if (isCurrent && onResume) {
                onResume()
              } else {
                onLevelSelect(sectionIndex, li)
              }
            }}
          />
        )
      })}

      <div style={styles.spacer} />
    </>
  )
}

// -- Main component -----------------------------------------------------------

export function LevelBrowser({
  world,
  currentSection,
  currentLevel,
  onLevelSelect,
  onResume,
}: LevelBrowserProps): React.ReactElement {
  if (!world) {
    return (
      <div style={{ ...styles.root, ...styles.placeholder }}>
        Select a world to browse levels
      </div>
    )
  }

  return (
    <div style={styles.root}>
      <div style={styles.content}>
        {/* Header: cover + title block */}
        <div style={styles.headerRow}>
          {world.coverPath && (
            <img
              src={localFileUrl(world.coverPath)}
              alt={world.title}
              style={styles.coverImage}
              draggable={false}
            />
          )}
          <div style={styles.titleBlock}>
            <div style={styles.composer}>{world.composer}</div>
            <div style={styles.worldTitle}>{world.title}</div>
            {world.catalogue && (
              <div style={styles.catalogue}>{world.catalogue}</div>
            )}
          </div>
        </div>

        {/* Levels heading */}
        <div style={styles.levelsHeading}>Levels</div>

        {/* Sections */}
        {world.sections.map((section, si) => (
          <SectionBlock
            key={section.id || si}
            section={section}
            sectionIndex={si}
            currentSection={currentSection}
            currentLevel={currentLevel}
            onLevelSelect={onLevelSelect}
            onResume={onResume}
          />
        ))}
      </div>
    </div>
  )
}
