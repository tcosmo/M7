/**
 * WorldLoader — ported from src/worldmodel.h and src/worldbrowser.cpp loadWorlds()
 *
 * Reads world JSON files that describe pieces, sections, levels, and
 * voice configurations.  All paths in the returned structures are resolved
 * to absolute paths relative to the JSON file that defined them.
 *
 * Pure TypeScript, no DOM/React dependencies.
 */

// ── Data types (mirrors C++ structs in worldmodel.h) ─────────────────────

export interface VoiceConfig {
  /** 1-based part number to activate for play-along. */
  playPart: number;
  /** General MIDI program number (default 34 = Electric Bass pick). */
  gmProgram: number;
  /** Soundfont filename; empty string means use the default. */
  soundfont: string;
  /** Key zone: "left", "right", or "all". */
  keys: string;
}

export interface Level {
  id: string;
  title: string;
  description: string;
  /** 1-based part numbers to display. */
  parts: number[];
  /** 1-based part for single-voice play-along (-1 = none). */
  playPart: number;
  /** GM program for single-voice mode. */
  gmProgram: number;
  /** Soundfont filename for single-voice mode. */
  soundfont: string;
  /** Multi-voice config; if non-empty, overrides playPart/gmProgram/soundfont. */
  voices: VoiceConfig[];
}

export interface Section {
  id: string;
  title: string;
  /** Absolute path to the MusicXML score. */
  scorePath: string;
  /** Absolute path to sources.json. */
  sourcesPath: string;
  /** Absolute path to beatdata.json. */
  beatsPath: string;
  levels: Level[];
}

export interface World {
  id: string;
  title: string;
  composer: string;
  catalogue: string;
  /** Absolute path to the cover image. */
  coverPath: string;
  /** Sort order (lower = shown first). */
  order: number;
  sections: Section[];
}

// ── JSON shapes (what the .json files actually contain) ──────────────────

interface VoiceConfigJson {
  playPart?: number;
  gmProgram?: number;
  soundfont?: string;
  keys?: string;
}

interface LevelJson {
  id?: string;
  title?: string;
  description?: string;
  parts?: number[];
  playPart?: number;
  gmProgram?: number;
  soundfont?: string;
  voices?: VoiceConfigJson[];
}

interface SectionJson {
  id?: string;
  title?: string;
  score?: string;
  sources?: string;
  beats?: string;
  levels?: LevelJson[];
}

interface WorldJson {
  id?: string;
  title?: string;
  composer?: string;
  catalogue?: string;
  cover?: string;
  order?: number;
  sections?: SectionJson[];
}

// ── Path helper (no Node.js path module) ─────────────────────────────────

function resolvePath(base: string, rel: string | undefined): string {
  if (!rel) return ''
  if (rel.startsWith('/')) return rel
  // Strip trailing slash from base
  const b = base.endsWith('/') ? base.slice(0, -1) : base
  const combined = `${b}/${rel}`
  // Resolve '..' segments
  const parts = combined.split('/')
  const resolved: string[] = []
  for (const p of parts) {
    if (p === '..') resolved.pop()
    else if (p !== '.') resolved.push(p)
  }
  return resolved.join('/')
}

// ── Loaders ──────────────────────────────────────────────────────────────

/**
 * Parse a world from an already-deserialised JSON object.
 * `jsonDir` is the absolute directory of the JSON file, used to resolve
 * relative paths.
 */
export function parseWorld(obj: WorldJson, jsonDir: string): World {
  const resolve = (rel: string | undefined): string =>
    resolvePath(jsonDir, rel);

  const world: World = {
    id: obj.id ?? '',
    title: obj.title ?? '',
    composer: obj.composer ?? '',
    catalogue: obj.catalogue ?? '',
    coverPath: resolve(obj.cover),
    order: obj.order ?? 0,
    sections: [],
  };

  if (Array.isArray(obj.sections)) {
    for (const so of obj.sections) {
      const section: Section = {
        id: so.id ?? '',
        title: so.title ?? '',
        scorePath: resolve(so.score),
        sourcesPath: resolve(so.sources),
        beatsPath: resolve(so.beats),
        levels: [],
      };

      if (Array.isArray(so.levels)) {
        for (const lo of so.levels) {
          const level: Level = {
            id: lo.id ?? '',
            title: lo.title ?? '',
            description: lo.description ?? '',
            parts: Array.isArray(lo.parts) ? [...lo.parts] : [],
            playPart: lo.playPart ?? -1,
            gmProgram: lo.gmProgram ?? 34,
            soundfont: lo.soundfont ?? '',
            voices: [],
          };

          if (Array.isArray(lo.voices)) {
            for (const vo of lo.voices) {
              level.voices.push({
                playPart: vo.playPart ?? -1,
                gmProgram: vo.gmProgram ?? 34,
                soundfont: vo.soundfont ?? '',
                keys: vo.keys ?? 'all',
              });
            }
          }

          section.levels.push(level);
        }
      }

      world.sections.push(section);
    }
  }

  return world;
}

