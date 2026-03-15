import React from 'react'
import { theme } from '../theme/theme'

export interface InterpretationSelectProps {
  labels: string[]
  activeIndex: number
  onSelect: (index: number) => void
}

const styles = {
  container: {
    padding: '4px 8px 8px',
  },
  header: {
    fontSize: 10,
    fontWeight: 700,
    color: theme.textSecondary,
    marginBottom: 4,
  },
  item: (active: boolean): React.CSSProperties => ({
    display: 'block',
    width: '100%',
    textAlign: 'left',
    padding: '5px 8px',
    borderRadius: 4,
    fontSize: 11,
    color: active ? theme.accent : theme.textPrimary,
    background: active ? 'rgba(224, 122, 47, 0.12)' : 'transparent',
    cursor: 'pointer',
    border: 'none',
    fontFamily: 'inherit',
    transition: 'background 0.1s ease',
  }),
}

export function InterpretationSelect({
  labels,
  activeIndex,
  onSelect,
}: InterpretationSelectProps): React.ReactElement {
  return (
    <div style={styles.container}>
      <div style={styles.header}>Interpretation</div>
      {labels.map((label, i) => (
        <button
          key={i}
          style={styles.item(i === activeIndex)}
          onClick={() => onSelect(i)}
          onMouseEnter={(e) => {
            if (i !== activeIndex) {
              e.currentTarget.style.background = 'rgba(255,255,255,0.06)'
            }
          }}
          onMouseLeave={(e) => {
            e.currentTarget.style.background =
              i === activeIndex ? 'rgba(224, 122, 47, 0.12)' : 'transparent'
          }}
        >
          {label}
        </button>
      ))}
    </div>
  )
}
