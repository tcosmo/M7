/**
 * SourcesLoader — ported from App::loadSources() in src/app.cpp (lines 1331-1450)
 *
 * Parses a sources.json file that describes local audio files and YouTube
 * interpretations for a section.
 *
 * Pure TypeScript, no DOM/React dependencies.
 */

// ── Data types ───────────────────────────────────────────────────────────

export interface YouTubeSource {
  url: string;
  label: string;
  /** Pitch offset in semitones (e.g. -0.9). */
  tuning: number;
  /** Instrument volume override (-1 = use default). */
  instrumentVolume: number;
  /** Master volume override (-1 = use default). */
  volume: number;
  /** Absolute path to per-interpretation beat data file, or empty. */
  beatsFile: string;
  /** Start time offset in seconds (for trimming intros). */
  start: number;
  /** End time in seconds (0 = play to end). */
  end: number;
}

export interface Sources {
  /** Absolute path to the local audio file, or empty. */
  audioFile: string;
  /** YouTube interpretations (may be empty). */
  youtube: YouTubeSource[];
}

// ── JSON shapes ──────────────────────────────────────────────────────────

interface YouTubeObjectJson {
  url?: string;
  label?: string;
  tuning?: number;
  instrumentVolume?: number;
  volume?: number;
  beats?: string;
  start?: number;
  end?: number;
}

interface SourcesJson {
  file?: string;
  youtube?: string | Array<string | YouTubeObjectJson>;
}

// ── Path helper (no Node.js path module) ─────────────────────────────────

function resolvePath(base: string, rel: string | undefined): string {
  if (!rel) return ''
  if (rel.startsWith('/')) return rel
  // Strip trailing slash from base
  const b = base.endsWith('/') ? base.slice(0, -1) : base
  return `${b}/${rel}`
}

// ── Loader ───────────────────────────────────────────────────────────────

/**
 * Parse a sources object (already deserialised).
 * `sourceDir` is the absolute directory of the sources.json file, used to
 * resolve relative paths.
 */
export function parseSources(
  obj: SourcesJson,
  sourceDir: string,
): Sources {
  const resolve = (rel: string | undefined): string =>
    resolvePath(sourceDir, rel);

  const result: Sources = {
    audioFile: resolve(obj.file),
    youtube: [],
  };

  if (obj.youtube === undefined || obj.youtube === null) {
    return result;
  }

  if (typeof obj.youtube === 'string') {
    // Single URL string
    result.youtube.push({
      url: obj.youtube,
      label: 'YouTube',
      tuning: 0,
      instrumentVolume: -1,
      volume: -1,
      beatsFile: '',
      start: 0,
      end: 0,
    });
  } else if (Array.isArray(obj.youtube)) {
    for (const item of obj.youtube) {
      if (typeof item === 'string') {
        result.youtube.push({
          url: item,
          label: 'YouTube',
          tuning: 0,
          instrumentVolume: -1,
          volume: -1,
          beatsFile: '',
          start: 0,
          end: 0,
        });
      } else if (typeof item === 'object' && item !== null) {
        const ytObj = item as YouTubeObjectJson;
        result.youtube.push({
          url: ytObj.url ?? '',
          label: ytObj.label ?? 'YouTube',
          tuning: ytObj.tuning ?? 0,
          instrumentVolume: ytObj.instrumentVolume ?? -1,
          volume: ytObj.volume ?? -1,
          beatsFile: resolve(ytObj.beats),
          start: ytObj.start ?? 0,
          end: ytObj.end ?? 0,
        });
      }
    }
  }

  return result;
}

/**
 * Load sources from a JSON string.
 * `sourceDir` is needed to resolve relative paths within the JSON.
 */
export function loadSourcesFromString(
  json: string,
  sourceDir: string,
): Sources {
  const obj = JSON.parse(json) as SourcesJson;
  return parseSources(obj, sourceDir);
}
