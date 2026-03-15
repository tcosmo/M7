/**
 * AutoScroll — logic ported from scorewidget.cpp.
 *
 * Decides whether the score container should scroll to keep the cursor visible,
 * and computes the target scroll position when it should.
 */

const MARGIN_PX = 40

/**
 * Determine whether the container needs to scroll to keep the cursor visible.
 *
 * @param cursorY        The cursor's Y position in document (score) coordinates.
 * @param containerScrollTop  Current scrollTop of the scrollable container.
 * @param containerHeight     Visible height of the container.
 * @param triggerPercent Fraction (0-1) of the viewport height that acts as the trigger line.
 *                       When the cursor passes below this line, scrolling is triggered.
 */
export function shouldScroll(
  cursorY: number,
  containerScrollTop: number,
  containerHeight: number,
  triggerPercent: number,
): boolean {
  const cursorInViewport = cursorY - containerScrollTop
  const triggerY = containerHeight * triggerPercent

  // Scroll if cursor is above the top margin or below the trigger line
  if (cursorInViewport < MARGIN_PX) return true
  if (cursorInViewport > triggerY) return true

  return false
}

/**
 * Compute the target scrollTop value that places the cursor at a comfortable
 * position in the viewport.
 *
 * The target position mirrors the Qt implementation: after scrolling, the cursor
 * sits at `triggerPercent * 0.35` from the top of the viewport (roughly the
 * upper third), matching the `m_scrollTarget` default in scorewidget.cpp.
 *
 * @param cursorY        The cursor's Y position in document (score) coordinates.
 * @param containerHeight     Visible height of the container.
 * @param triggerPercent Fraction (0-1) — same trigger line fraction used in shouldScroll.
 * @returns              The scrollTop value the container should animate to.
 */
export function getScrollTarget(
  cursorY: number,
  containerHeight: number,
  triggerPercent: number,
): number {
  // Place the cursor at ~35% of the trigger zone from the top.
  // This keeps it comfortably visible without jumping to the very top.
  const targetY = containerHeight * triggerPercent * 0.35
  const scrollTo = cursorY - targetY

  // Clamp to zero — can't scroll above the top of the document
  return Math.max(0, scrollTo)
}
