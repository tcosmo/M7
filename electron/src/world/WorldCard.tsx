import React, { useState } from 'react'
import type { World } from '../core/WorldLoader'
import { theme } from '../theme/theme'
import { localFileUrl } from '../core/localFileUrl'

export interface WorldCardProps {
  world: World
  selected: boolean
  onClick: () => void
}

const styles = {
  card: (selected: boolean, hovered: boolean): React.CSSProperties => ({
    display: 'flex',
    alignItems: 'center',
    height: 80,
    padding: '0 12px',
    margin: '2px 4px',
    borderRadius: 8,
    cursor: 'pointer',
    position: 'relative',
    background: selected
      ? theme.inputBg
      : hovered
        ? '#36393f'
        : theme.panelBg,
    transition: 'background 0.12s ease',
  }),
  selectionBar: {
    position: 'absolute' as const,
    left: 4,
    top: 8,
    width: 4,
    bottom: 8,
    borderRadius: 2,
    background: theme.accent,
  },
  coverImage: {
    width: 56,
    height: 56,
    borderRadius: 6,
    objectFit: 'cover' as const,
    flexShrink: 0,
  },
  coverPlaceholder: {
    width: 56,
    height: 56,
    borderRadius: 6,
    flexShrink: 0,
    background: theme.surfaceBg,
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    color: theme.textDisabled,
    fontSize: 20,
  },
  textBlock: {
    marginLeft: 10,
    minWidth: 0,
    flex: 1,
  },
  title: {
    fontSize: 12,
    fontWeight: 700,
    color: theme.textPrimary,
    whiteSpace: 'nowrap' as const,
    overflow: 'hidden' as const,
    textOverflow: 'ellipsis' as const,
    lineHeight: '18px',
  },
  composer: {
    fontSize: 11,
    color: theme.textSecondary,
    whiteSpace: 'nowrap' as const,
    overflow: 'hidden' as const,
    textOverflow: 'ellipsis' as const,
    lineHeight: '16px',
  },
  catalogue: {
    fontSize: 10,
    color: theme.textHint,
    whiteSpace: 'nowrap' as const,
    overflow: 'hidden' as const,
    textOverflow: 'ellipsis' as const,
    lineHeight: '14px',
    marginTop: 1,
  },
}

export function WorldCard({ world, selected, onClick }: WorldCardProps): React.ReactElement {
  const [hovered, setHovered] = useState(false)

  return (
    <div
      style={styles.card(selected, hovered)}
      onClick={onClick}
      onMouseEnter={() => setHovered(true)}
      onMouseLeave={() => setHovered(false)}
      title={`${world.composer} \u2014 ${world.title}`}
    >
      {selected && <div style={styles.selectionBar} />}

      {world.coverPath ? (
        <img
          src={localFileUrl(world.coverPath)}
          alt={world.title}
          style={styles.coverImage}
          draggable={false}
        />
      ) : (
        <div style={styles.coverPlaceholder}>{'\u266B'}</div>
      )}

      <div style={styles.textBlock}>
        <div style={styles.title}>{world.title}</div>
        <div style={styles.composer}>{world.composer}</div>
        {world.catalogue && (
          <div style={styles.catalogue}>{world.catalogue}</div>
        )}
      </div>
    </div>
  )
}
