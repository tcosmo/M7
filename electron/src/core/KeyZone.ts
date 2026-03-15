/**
 * KeyZone — ported from App::keyPressEvent() / keyReleaseEvent() in src/app.cpp
 *
 * Maps laptop keyboard letter keys to voice indices based on a left/right
 * zone split.  The split mirrors a physical keyboard layout so that two
 * players (or two hands) can each control their own instrument voice.
 *
 * Pure TypeScript, no DOM/React dependencies.
 */

// ── Zone constants (match the C++ static strings) ────────────────────────

/** Letter keys assigned to the left zone (roughly the left half of QWERTY). */
export const LEFT_KEYS = 'ABCDEFGQRSTVWXZ';

/** Letter keys assigned to the right zone (roughly the right half of QWERTY). */
export const RIGHT_KEYS = 'HIJKLMNOPUY';

// Pre-compute Sets for O(1) lookup
const LEFT_SET = new Set(LEFT_KEYS);
const RIGHT_SET = new Set(RIGHT_KEYS);

// ── Types ────────────────────────────────────────────────────────────────

export type KeyZoneName = 'left' | 'right' | 'all';

export interface VoiceKeyConfig {
  keyZone: KeyZoneName;
}

// ── Core function ────────────────────────────────────────────────────────

/**
 * Given a key character (uppercase A-Z) and an array of voice configs with
 * keyZone settings, returns the indices of all voices that should fire for
 * that key press.
 *
 * Returns an empty array if the key is not a letter or doesn't match any
 * voice's zone.
 */
export function getVoiceIndicesForKey(
  key: string,
  voices: readonly VoiceKeyConfig[],
): number[] {
  const ch = key.toUpperCase();
  if (ch.length !== 1 || ch < 'A' || ch > 'Z') return [];

  const isLeft = LEFT_SET.has(ch);
  const isRight = RIGHT_SET.has(ch);

  const result: number[] = [];
  for (let i = 0; i < voices.length; i++) {
    const zone = voices[i].keyZone;
    if (
      zone === 'all' ||
      (zone === 'left' && isLeft) ||
      (zone === 'right' && isRight)
    ) {
      result.push(i);
    }
  }
  return result;
}

/**
 * Check whether a key character belongs to the left zone.
 */
export function isLeftKey(key: string): boolean {
  return LEFT_SET.has(key.toUpperCase());
}

/**
 * Check whether a key character belongs to the right zone.
 */
export function isRightKey(key: string): boolean {
  return RIGHT_SET.has(key.toUpperCase());
}

/**
 * Check whether a key character is a recognised letter key (left or right).
 */
export function isLetterKey(key: string): boolean {
  const ch = key.toUpperCase();
  return ch.length === 1 && ch >= 'A' && ch <= 'Z';
}
