/**
 * ScoreView — renders Verovio SVG pages in a scrollable, zoomable container
 * with cursor overlay and note highlighting.
 */

import React, { useCallback, useEffect, useRef } from 'react'
import CursorOverlay from './CursorOverlay'
import {
  highlightElements,
  clearHighlights,
  HIGHLIGHT_V0_CLASS,
  HIGHLIGHT_V1_CLASS,
} from './NoteHighlight'
import { shouldScroll, getScrollTarget } from './AutoScroll'
import { theme } from '../theme/theme'

/** Unique DOM id for the SVG container so NoteHighlight can find it. */
const CONTAINER_ID = 'score-svg-container'

export interface ScoreViewProps {
  /** SVG markup strings, one per page. */
  svgPages: string[]
  /** Zoom factor (1.0 = 100%). */
  zoom: number
  /** Current cursor position rectangle (in score/SVG coordinates). */
  cursorRect?: {
    x: number
    y: number
    width: number
    height: number
    pageIndex: number
  }
  /** Element IDs to highlight for voice 0 (blue). */
  highlightIds?: string[]
  /** Element IDs to highlight for voice 1 (violet). */
  highlightIds2?: string[]
  /** Whether auto-scroll is enabled during playback. */
  autoScroll?: boolean
  /**
   * Fraction of the viewport height (0-1) that acts as the trigger line
   * for auto-scrolling. Default 0.65 (65% from top).
   */
  triggerLinePercent?: number
  /** Called when the user manually scrolls. */
  onScroll?: (scrollTop: number) => void
}

const ScoreView: React.FC<ScoreViewProps> = ({
  svgPages,
  zoom,
  cursorRect,
  highlightIds,
  highlightIds2,
  autoScroll = false,
  triggerLinePercent = 0.65,
  onScroll,
}) => {
  const scrollRef = useRef<HTMLDivElement>(null)
  const pageRefs = useRef<(HTMLDivElement | null)[]>([])

  // ── Reset scroll and fix negative SVG offset when pages change ──
  useEffect(() => {
    if (!scrollRef.current) return
    scrollRef.current.scrollTop = 0
    // Verovio SVGs have overflow="visible" which causes elements to render
    // above y=0, creating a negative container offsetTop. Fix: iteratively
    // add margin-top to push the container down until offsetTop >= 0.
    const container = document.getElementById(CONTAINER_ID)
    if (container) {
      container.style.marginTop = '0px'
      void container.offsetTop
      let totalMargin = 0
      for (let attempt = 0; attempt < 5; attempt++) {
        const offset = container.offsetTop
        if (offset >= 0) break
        totalMargin += -offset
        container.style.marginTop = `${totalMargin}px`
        void container.offsetTop
      }
    }
  }, [svgPages])

  // ── Highlight management ─────────────────────────────────────────
  useEffect(() => {
    clearHighlights(CONTAINER_ID, HIGHLIGHT_V0_CLASS)
    if (highlightIds && highlightIds.length > 0) {
      highlightElements(CONTAINER_ID, highlightIds, HIGHLIGHT_V0_CLASS)
    }
  }, [highlightIds, svgPages])

  useEffect(() => {
    clearHighlights(CONTAINER_ID, HIGHLIGHT_V1_CLASS)
    if (highlightIds2 && highlightIds2.length > 0) {
      highlightElements(CONTAINER_ID, highlightIds2, HIGHLIGHT_V1_CLASS)
    }
  }, [highlightIds2, svgPages])

  // ── Auto-scroll ──────────────────────────────────────────────────
  useEffect(() => {
    if (!autoScroll || !cursorRect || !scrollRef.current) return

    const container = scrollRef.current
    const pageEl = pageRefs.current[cursorRect.pageIndex]
    if (!pageEl) return

    // Compute the cursor's document-level Y by adding the page's offset
    const cursorDocY = pageEl.offsetTop + cursorRect.y * zoom

    if (
      shouldScroll(
        cursorDocY,
        container.scrollTop,
        container.clientHeight,
        triggerLinePercent,
      )
    ) {
      const target = getScrollTarget(
        cursorDocY,
        container.clientHeight,
        triggerLinePercent,
      )
      container.scrollTo({ top: target, behavior: 'smooth' })
    }
  }, [cursorRect, autoScroll, triggerLinePercent, zoom])

  // ── Scroll event forwarding ──────────────────────────────────────
  const handleScroll = useCallback(() => {
    if (onScroll && scrollRef.current) {
      onScroll(scrollRef.current.scrollTop)
    }
  }, [onScroll])

  // ── Cursor position in container coordinates ─────────────────────
  let cursorVisible = false
  let cursorX = 0
  let cursorY = 0
  let cursorW = 0
  let cursorH = 0

  if (cursorRect) {
    const pageEl = pageRefs.current[cursorRect.pageIndex]
    if (pageEl) {
      cursorX = cursorRect.x * zoom
      cursorY = pageEl.offsetTop + cursorRect.y * zoom
      cursorW = cursorRect.width * zoom
      cursorH = cursorRect.height * zoom
      cursorVisible = true
    }
  }

  return (
    <div
      ref={scrollRef}
      className="score-view"
      onScroll={handleScroll}
      style={{
        position: 'relative',
        overflow: 'auto',
        flex: 1,
        background: theme.scoreBg,
      }}
    >
      <div
        id={CONTAINER_ID}
        style={{
          transformOrigin: 'top left',
          transform: `scale(${zoom})`,
        }}
      >
        {svgPages.map((svg, i) => (
          <div
            key={i}
            ref={(el) => {
              pageRefs.current[i] = el
            }}
            className="score-page"
            style={{ position: 'relative' }}
            dangerouslySetInnerHTML={{ __html: svg }}
          />
        ))}
      </div>

      {/* Cursor overlay disabled — using note highlighting instead */}

      {/* Inline style tag for highlight classes */}
      <style>{`
        .${HIGHLIGHT_V0_CLASS} {
          fill: rgba(50, 120, 255, 0.9) !important;
          stroke: rgba(50, 120, 255, 1.0) !important;
          filter: drop-shadow(0 0 6px rgba(50, 120, 255, 0.7));
        }
        .${HIGHLIGHT_V1_CLASS} {
          fill: rgba(180, 80, 220, 0.9) !important;
          stroke: rgba(180, 80, 220, 1.0) !important;
          filter: drop-shadow(0 0 6px rgba(180, 80, 220, 0.7));
        }
        .score-page {
          background: #ffffff;
          overflow: hidden;
        }
        .score-page > svg {
          overflow: hidden !important;
        }
        .score-page svg {
          display: block;
        }
      `}</style>
    </div>
  )
}

export default ScoreView
