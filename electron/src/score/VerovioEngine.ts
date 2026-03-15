/**
 * VerovioEngine — wrapper around the verovio npm toolkit.
 *
 * Verovio page indices are 1-based.
 * The toolkit is loaded lazily via init().
 */

import { VerovioToolkit } from 'verovio/esm'
import createVerovioModule from 'verovio/wasm'

export interface NoteEvent {
  id: string
  pitch: number       // MIDI note number
  tick: number         // Verovio internal tick position
  duration: number     // duration in ticks
  onTimeMs: number     // real-time onset (ms)
  offTimeMs: number    // real-time offset (ms)
  staffN: number       // staff number (1-based)
}

export interface ElementPosition {
  x: number
  y: number
  width: number
  height: number
  pageIndex: number  // 0-based page index (converted from verovio's 1-based)
}

export interface MIDIValues {
  pitch: number
  tick: number
  duration: number
}

const DEFAULT_OPTIONS = {
  pageWidth: 2100,           // default, overridden dynamically by App
  pageHeight: 60000,        // very tall single "page" = continuous vertical scroll
  scale: 40,
  adjustPageHeight: true,
  breaks: 'auto' as const,
  header: 'none' as const,  // remove title/composer header
  pageMarginTop: 50,
  pageMarginLeft: 125,
  pageMarginRight: 125,
}

export class VerovioEngine {
  private toolkit: any = null
  private originalXml: string = ''
  private currentOptions: Record<string, any> = {}

  /**
   * Initialise the Verovio WASM module and create a toolkit instance.
   * Must be called before any other method.
   */
  async init(): Promise<void> {
    if (this.toolkit) return
    const VerovioModule = await createVerovioModule()
    this.toolkit = new VerovioToolkit(VerovioModule)
  }

  /**
   * Load MusicXML data into the engine and render the score.
   */
  loadMusicXML(
    xmlData: string,
    options?: {
      pageWidth?: number
      pageHeight?: number
      scale?: number
      adjustPageHeight?: boolean
      breaks?: 'auto' | 'line' | 'encoded'
    },
  ): void {
    this.assertReady()

    this.originalXml = xmlData

    const merged = { ...DEFAULT_OPTIONS, ...options }
    this.currentOptions = { ...merged }
    this.toolkit.setOptions(this.currentOptions)

    const ok = this.toolkit.loadData(xmlData)
    if (!ok) {
      throw new Error('Verovio failed to load MusicXML data')
    }
    // Force MIDI computation so getMIDIValuesForElement works for all notes
    try { this.toolkit.renderToMIDI() } catch { /* ignore */ }
  }

  /** Number of pages in the current layout. */
  getPageCount(): number {
    this.assertReady()
    return this.toolkit.getPageCount()
  }

  /**
   * Render a single page to SVG.
   * @param pageIndex 0-based page index (converted to 1-based for Verovio)
   */
  renderPage(pageIndex: number): string {
    this.assertReady()
    return this.toolkit.renderToSVG(pageIndex + 1)
  }

  /**
   * Extract note events for a given part index.
   * Walks every page's MIDI-value data to build a list.
   */
  /**
   * Extract note events for a given part index.
   * IMPORTANT: pass the already-rendered SVG pages to avoid ID mismatch
   * (each renderToSVG call generates new random IDs).
   */
  getNotesForPart(partIndex: number, renderedPages?: string[]): NoteEvent[] {
    this.assertReady()
    const staffN = partIndex + 1

    // Collect note IDs from already-rendered SVGs (or re-render if not provided)
    const pages = renderedPages ?? (() => {
      const p: string[] = []
      for (let i = 1; i <= this.getPageCount(); i++) p.push(this.toolkit.renderToSVG(i))
      return p
    })()

    const allIds: string[] = []
    for (const svg of pages) {
      const parser = new DOMParser()
      const doc = parser.parseFromString(svg, 'image/svg+xml')

      // In Verovio SVGs, notes are inside: measure > staff > layer > note
      // Within each measure, staves appear in order (1st staff = staffN 1, etc.)
      const measures = doc.querySelectorAll('.measure')
      if (measures.length > 0 && staffN > 0) {
        // Multi-staff: extract notes only from the correct staff within each measure
        for (const measure of measures) {
          const staves = measure.querySelectorAll(':scope > .staff')
          const targetStaff = staves[staffN - 1] // 0-indexed into 1-based staffN
          if (!targetStaff) continue
          const noteEls = targetStaff.querySelectorAll('.note[id]')
          for (const el of noteEls) {
            const id = el.getAttribute('id')
            if (id) allIds.push(id)
          }
        }
      } else {
        // Single staff or no measures found: collect all notes
        const noteEls = doc.querySelectorAll('.note[id]')
        for (const el of noteEls) {
          const id = el.getAttribute('id')
          if (id) allIds.push(id)
        }
      }
    }

    const notes: NoteEvent[] = []
    const skippedFirst: string[] = [] // IDs skipped before the first successful note

    for (const id of allIds) {
      const midi = this.getMIDIValues(id)
      if (!midi || midi.pitch === undefined) {
        if (notes.length === 0) skippedFirst.push(id)
        continue
      }

      const time = this.toolkit.getTimeForElement(id)
      if (!time) {
        if (notes.length === 0) skippedFirst.push(id)
        continue
      }

      notes.push({
        id,
        pitch: midi.pitch,
        tick: midi.tick,
        duration: midi.duration,
        onTimeMs: time.realTimeOnMs ?? time.onTime ?? 0,
        offTimeMs: time.realTimeOffMs ?? time.offTime ?? 0,
        staffN,
      })
    }

    // Verovio sometimes fails to provide MIDI values for the very first note(s).
    // Recover their pitch from element attributes (pname + oct).
    if (skippedFirst.length > 0 && notes.length > 0) {
      const firstGood = notes[0]
      for (let i = skippedFirst.length - 1; i >= 0; i--) {
        let pitch = firstGood.pitch
        // Try to get the actual pitch from Verovio's element attributes
        try {
          const attr = this.toolkit.getElementAttr(skippedFirst[i])
          if (attr && attr.pname && attr.oct !== undefined) {
            pitch = VerovioEngine.pnameOctToMidi(attr.pname, Number(attr.oct), attr.accid)
          }
        } catch { /* use fallback */ }
        notes.unshift({
          id: skippedFirst[i],
          pitch,
          tick: 0,
          duration: firstGood.duration,
          onTimeMs: 0,
          offTimeMs: 0,
          staffN,
        })
      }
    }

    return notes
  }

  /**
   * Get the bounding-box position of an SVG element by its ID.
   * Returns null if the element is not found.
   */
  getElementPosition(elementId: string): ElementPosition | null {
    this.assertReady()

    const pageCount = this.getPageCount()
    for (let page = 1; page <= pageCount; page++) {
      // Verovio's getBoundingBox is available on recent toolkit builds
      // Fall back to iterating element attrs if needed
      const time = this.toolkit.getTimeForElement(elementId)
      if (!time) return null

      // Use getElementAttr to find which page this element lives on
      const attr = this.toolkit.getElementAttr(elementId)
      if (!attr) return null

      // Verovio 4.x provides a page property via getTimeForElement
      const elemPage = time.page ?? this.findPageForElement(elementId)
      if (elemPage < 1) return null

      // Attempt to get bounding box — Verovio 4.x toolkit API
      try {
        const bbox = this.toolkit.getElementBoundingBox(elementId)
        if (bbox && bbox.width > 0) {
          return {
            x: bbox.x,
            y: bbox.y,
            width: bbox.width,
            height: bbox.height,
            pageIndex: elemPage - 1,
          }
        }
      } catch {
        // getElementBoundingBox may not be available in all builds
      }

      // Fallback: parse SVG to locate the element (expensive, but reliable)
      const svg = this.toolkit.renderToSVG(elemPage)
      const pos = this.parseElementFromSVG(svg, elementId)
      if (pos) {
        return { ...pos, pageIndex: elemPage - 1 }
      }
      return null
    }

    return null
  }

  /**
   * Get element IDs at a given time in milliseconds.
   * Returns note IDs and the page number (converted to 0-based).
   */
  getElementsAtTime(timeMs: number): { notes: string[]; page: number } {
    this.assertReady()
    const result = this.toolkit.getElementsAtTime(timeMs)
    if (!result) return { notes: [], page: 0 }

    return {
      notes: result.notes ?? [],
      page: (result.page ?? 1) - 1, // convert 1-based to 0-based
    }
  }

  /**
   * Get MIDI pitch, tick position, and duration for an element.
   */
  getMIDIValues(elementId: string): MIDIValues | null {
    this.assertReady()
    try {
      const vals = this.toolkit.getMIDIValuesForElement(elementId)
      if (!vals || vals.pitch === undefined) return null
      return {
        pitch: vals.pitch,
        tick: vals.tick ?? 0,
        duration: vals.duration ?? 0,
      }
    } catch {
      return null
    }
  }

  /**
   * Select which staves to display (1-based part numbers).
   * Pass empty array to show all staves.
   *
   * Verovio's toolkit.select() only supports measure-range selection,
   * NOT staff filtering. To display a subset of parts we must filter
   * the MusicXML source — removing unwanted <score-part> and <part>
   * elements — then reload.
   */
  selectStaves(partNumbers: number[]): void {
    this.assertReady()
    if (!this.originalXml) return

    if (partNumbers.length === 0) {
      // Reload the full score
      this.toolkit.setOptions(this.currentOptions)
      this.toolkit.loadData(this.originalXml)
      return
    }

    const filtered = VerovioEngine.filterMusicXML(this.originalXml, partNumbers)
    this.toolkit.setOptions(this.currentOptions)
    const ok = this.toolkit.loadData(filtered)
    if (!ok) {
      console.warn('selectStaves: Verovio failed to load filtered MusicXML, falling back to full score')
      this.toolkit.loadData(this.originalXml)
    }
    // Force MIDI computation for the filtered score
    try { this.toolkit.renderToMIDI() } catch { /* ignore */ }
  }

  /**
   * Filter a MusicXML string to keep only the specified parts.
   * @param xml   Raw MusicXML string (score-partwise format)
   * @param parts 1-based part indices to keep (e.g. [4] keeps the 4th part)
   */
  private static filterMusicXML(xml: string, parts: number[]): string {
    const parser = new DOMParser()
    const doc = parser.parseFromString(xml, 'application/xml')

    // Build the set of part IDs to keep.
    // In MusicXML, <score-part> elements appear in document order inside
    // <part-list>. The 1-based index matches the order of appearance.
    const partList = doc.querySelector('part-list')
    if (!partList) return xml

    const scoreParts = Array.from(partList.querySelectorAll('score-part'))
    // Build ordered list of part IDs to keep (preserving requested order)
    const orderedIds: string[] = []
    for (const idx of parts) {
      const sp = scoreParts[idx - 1]
      if (sp) orderedIds.push(sp.getAttribute('id') ?? '')
    }
    const keepIds = new Set(orderedIds)
    if (keepIds.size === 0) return xml

    // Clear part-list entirely and rebuild in requested order
    while (partList.firstChild) partList.removeChild(partList.firstChild)

    // Add bracket group if multiple parts
    if (orderedIds.length > 1) {
      const startGroup = doc.createElement('part-group')
      startGroup.setAttribute('type', 'start')
      startGroup.setAttribute('number', '1')
      const groupSymbol = doc.createElement('group-symbol')
      groupSymbol.textContent = 'bracket'
      startGroup.appendChild(groupSymbol)
      const groupBarline = doc.createElement('group-barline')
      groupBarline.textContent = 'yes'
      startGroup.appendChild(groupBarline)
      partList.appendChild(startGroup)
    }

    // Re-add score-parts in requested order
    for (const id of orderedIds) {
      const sp = scoreParts.find(s => s.getAttribute('id') === id)
      if (sp) partList.appendChild(sp)
    }

    if (orderedIds.length > 1) {
      const stopGroup = doc.createElement('part-group')
      stopGroup.setAttribute('type', 'stop')
      stopGroup.setAttribute('number', '1')
      partList.appendChild(stopGroup)
    }

    // Remove unwanted <part> elements and reorder kept ones
    const root = doc.documentElement
    const partElements = Array.from(root.getElementsByTagName('part'))
    const keptPartEls: Element[] = []
    for (const pe of partElements) {
      if (pe.parentNode !== root) continue
      const id = pe.getAttribute('id') ?? ''
      if (keepIds.has(id)) {
        keptPartEls.push(pe)
      }
      root.removeChild(pe)
    }
    // Re-append in requested order
    for (const id of orderedIds) {
      const pe = keptPartEls.find(e => e.getAttribute('id') === id)
      if (pe) root.appendChild(pe)
    }

    // Serialise back to string
    const serializer = new XMLSerializer()
    return serializer.serializeToString(doc)
  }

  /** Update toolkit options without reloading data. */
  setOptions(options: Record<string, any>): void {
    this.assertReady()
    this.toolkit.setOptions(options)
  }

  /** Re-run the layout engine (e.g. after a resize or option change). */
  redoLayout(): void {
    this.assertReady()
    this.toolkit.redoLayout()
  }

  /**
   * Collect all note/chord element IDs, optionally filtered to a staff.
   * @param staffN 1-based staff filter; omit or 0 for all staves
   */
  getAllNoteElements(staffN?: number): string[] {
    this.assertReady()
    const ids: string[] = []
    const pageCount = this.getPageCount()

    for (let page = 1; page <= pageCount; page++) {
      const svg = this.toolkit.renderToSVG(page)
      // Parse note element IDs from SVG using DOMParser for reliability
      const parser = new DOMParser()
      const doc = parser.parseFromString(svg, 'image/svg+xml')
      const noteEls = doc.querySelectorAll('.note[id]')
      for (const el of noteEls) {
        const id = el.getAttribute('id')
        if (!id) continue
        if (staffN && staffN > 0) {
          const attr = this.toolkit.getElementAttr(id)
          if (attr && attr.staff !== undefined && Number(attr.staff) !== staffN) {
            continue
          }
        }
        ids.push(id)
      }
    }

    return ids
  }

  // ── Private helpers ──────────────────────────────────────────────

  private assertReady(): void {
    if (!this.toolkit) {
      throw new Error('VerovioEngine not initialised — call init() first')
    }
  }

  /** Convert Verovio pname (c,d,e,f,g,a,b) + octave + accidental to MIDI pitch. */
  private static pnameOctToMidi(pname: string, oct: number, accid?: string): number {
    const base: Record<string, number> = { c: 0, d: 2, e: 4, f: 5, g: 7, a: 9, b: 11 }
    let midi = (oct + 1) * 12 + (base[pname.toLowerCase()] ?? 0)
    if (accid === 's' || accid === '#') midi++
    else if (accid === 'f' || accid === 'b') midi--
    return Math.max(0, Math.min(127, midi))
  }

  /**
   * Determine which page an element lives on by rendering each page's SVG
   * and checking if the element ID appears in the markup.
   */
  private findPageForElement(elementId: string): number {
    const count = this.getPageCount()
    for (let p = 1; p <= count; p++) {
      const svg = this.toolkit.renderToSVG(p)
      if (svg.includes(`id="${elementId}"`)) return p
    }
    return -1
  }

  /**
   * Parse a bounding box for an element out of raw SVG markup.
   * This is a fallback when toolkit.getElementBoundingBox is unavailable.
   */
  private parseElementFromSVG(
    svg: string,
    elementId: string,
  ): { x: number; y: number; width: number; height: number } | null {
    // Use a temporary DOM to parse
    if (typeof document === 'undefined') return null

    const parser = new DOMParser()
    const doc = parser.parseFromString(svg, 'image/svg+xml')
    const el = doc.getElementById(elementId)
    if (!el) return null

    // For <use>, <g>, <rect>, etc., try the bounding box via getBBox if available
    // In a non-rendered context getBBox won't work, so fall back to attributes
    const x = parseFloat(el.getAttribute('x') ?? '0')
    const y = parseFloat(el.getAttribute('y') ?? '0')
    const width = parseFloat(el.getAttribute('width') ?? el.getAttribute('rx') ?? '20')
    const height = parseFloat(el.getAttribute('height') ?? el.getAttribute('ry') ?? '20')

    return { x, y, width, height }
  }
}

export default VerovioEngine
