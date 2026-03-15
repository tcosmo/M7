/**
 * Convert an absolute file path to a local-file:// URL
 * that the Electron custom protocol handler can serve.
 */
export function localFileUrl(absolutePath: string): string {
  if (!absolutePath) return ''
  return `local-file://${absolutePath}`
}
