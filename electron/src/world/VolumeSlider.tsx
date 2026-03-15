import React from 'react'
import { theme } from '../theme/theme'

export interface VolumeSliderProps {
  value: number // 0-100
  onChange: (value: number) => void
}

const styles = {
  container: {
    display: 'flex',
    alignItems: 'center',
    gap: 6,
    padding: '4px 8px 8px',
  },
  label: {
    fontSize: 10,
    color: theme.textSecondary,
    flexShrink: 0,
    userSelect: 'none' as const,
  },
  slider: {
    flex: 1,
    height: 4,
    appearance: 'none' as const,
    background: theme.inputBg,
    borderRadius: 2,
    outline: 'none',
    cursor: 'pointer',
    WebkitAppearance: 'none' as const,
  },
}

// We inject a <style> tag for the range thumb since pseudo-elements
// cannot be styled via inline styles.
const SLIDER_CLASS = 'vol-slider'
const thumbCss = `
  .${SLIDER_CLASS}::-webkit-slider-thumb {
    -webkit-appearance: none;
    width: 12px;
    height: 12px;
    border-radius: 50%;
    background: ${theme.textPrimary};
    cursor: pointer;
    border: none;
    margin-top: -4px;
  }
  .${SLIDER_CLASS}::-webkit-slider-runnable-track {
    height: 4px;
    border-radius: 2px;
    background: ${theme.inputBg};
  }
`

let styleInjected = false
function injectStyle(): void {
  if (styleInjected) return
  const tag = document.createElement('style')
  tag.textContent = thumbCss
  document.head.appendChild(tag)
  styleInjected = true
}

export function VolumeSlider({ value, onChange }: VolumeSliderProps): React.ReactElement {
  React.useEffect(() => { injectStyle() }, [])

  return (
    <div style={styles.container}>
      <span style={styles.label}>Vol</span>
      <input
        className={SLIDER_CLASS}
        type="range"
        min={0}
        max={100}
        value={value}
        title={`Volume: ${value}%`}
        onChange={(e) => onChange(Number(e.target.value))}
        style={styles.slider}
      />
    </div>
  )
}
