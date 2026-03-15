/**
 * NoteHighlight — utility for highlighting note elements in rendered SVG.
 *
 * Works by finding SVG elements by ID inside a container and toggling CSS classes.
 * The corresponding CSS classes (.highlight-v0, .highlight-v1) are defined in
 * the score stylesheet injected by ScoreView.
 */

/** CSS class for voice 0 highlights (blue). */
export const HIGHLIGHT_V0_CLASS = 'highlight-v0'

/** CSS class for voice 1 highlights (violet). */
export const HIGHLIGHT_V1_CLASS = 'highlight-v1'

/**
 * Add a CSS class to a set of SVG elements inside a container.
 *
 * @param containerId  The DOM id of the container element holding the rendered SVG pages.
 * @param elementIds   Verovio element IDs to highlight.
 * @param className    The CSS class to apply (e.g. HIGHLIGHT_V0_CLASS).
 */
export function highlightElements(
  containerId: string,
  elementIds: string[],
  className: string,
): void {
  const container = document.getElementById(containerId)
  if (!container) return

  for (const id of elementIds) {
    const el = container.querySelector(`#${CSS.escape(id)}`)
    if (el) {
      el.classList.add(className)
    }
  }
}

/**
 * Remove a CSS class from all elements inside a container that carry it.
 *
 * @param containerId  The DOM id of the container element.
 * @param className    The CSS class to remove.
 */
export function clearHighlights(containerId: string, className: string): void {
  const container = document.getElementById(containerId)
  if (!container) return

  const highlighted = container.querySelectorAll(`.${CSS.escape(className)}`)
  for (const el of highlighted) {
    el.classList.remove(className)
  }
}
