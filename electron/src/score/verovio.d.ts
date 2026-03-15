/**
 * Minimal type declarations for the verovio npm package.
 * The package does not ship its own .d.ts files.
 */

declare module 'verovio/esm' {
  export class VerovioToolkit {
    constructor(module: any)
    setOptions(options: Record<string, any>): void
    loadData(data: string): boolean
    getPageCount(): number
    renderToSVG(page: number): string
    getElementsAtTime(ms: number): {
      notes: string[]
      chords: string[]
      page: number
    } | null
    getTimeForElement(id: string): {
      onTime: number
      offTime: number
      realTimeOnMs: number
      realTimeOffMs: number
      page?: number
    } | null
    getMIDIValuesForElement(id: string): {
      pitch: number
      tick: number
      duration: number
    } | null
    getElementAttr(id: string): Record<string, any> | null
    getElementBoundingBox(id: string): {
      x: number
      y: number
      width: number
      height: number
    } | null
    redoLayout(): void
    destroy(): void
  }
}

declare module 'verovio/wasm' {
  export default function createVerovioModule(): Promise<any>
}
