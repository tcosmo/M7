/**
 * CursorOverlay — a semi-transparent rectangle that marks the current playback position.
 *
 * Positioned absolutely over the score. Uses CSS transitions for smooth movement.
 */

import React from 'react'
import { theme } from '../theme/theme'

export interface CursorOverlayProps {
  x: number
  y: number
  width: number
  height: number
  visible: boolean
}

const CursorOverlay: React.FC<CursorOverlayProps> = ({ x, y, width, height, visible }) => {
  if (!visible) return null

  return (
    <div
      className="cursor-overlay"
      style={{
        position: 'absolute',
        left: x,
        top: y,
        width,
        height,
        backgroundColor: 'rgba(66, 133, 244, 0.30)',
        borderLeft: `2px solid ${theme.accent}`,
        pointerEvents: 'none',
        transition: 'left 80ms linear, top 80ms linear',
        zIndex: 10,
      }}
    />
  )
}

export default CursorOverlay
