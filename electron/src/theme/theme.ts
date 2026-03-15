/**
 * Theme tokens — mirrors the Discord-dark palette from the Qt desktop app (theme.h).
 * Use CSS custom properties (set in global.css) for styling;
 * import this module when you need the values in JS/TS (e.g. canvas drawing).
 */

export const theme = {
  // Background layers (lighter → darker)
  panelBg:   '#2f3136',
  contentBg: '#292b2f',
  scoreBg:   '#36393f',
  inputBg:   '#40444b',
  surfaceBg: '#202225',
  popupBg:   '#18191c',

  // Text hierarchy
  textPrimary:   '#dcddde',
  textSecondary: '#b9bbbe',
  textDisabled:  '#72767d',
  textHidden:    '#4f545c',
  textHint:      '#96989d',

  // Icons
  iconVisible: '#dcddde',
  iconHidden:  '#4f545c',
  arrowColor:  '#b9bbbe',

  // Accent colors
  accent:      '#E07A2F',
  accentHover: '#C46A28',
  green:       '#57F287',
  red:         '#ED4245',
  yellow:      '#FEE75C',
} as const

export type ThemeKey = keyof typeof theme
