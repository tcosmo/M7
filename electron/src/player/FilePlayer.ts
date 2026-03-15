/**
 * FilePlayer — audio file playback using HTMLAudioElement with
 * requestAnimationFrame position polling for smooth updates.
 */
export class FilePlayer {
  private audio: HTMLAudioElement
  private rafId = 0
  private _playing = false

  /* ── Callbacks ───────────────────────────────────────────────── */

  onPositionChange?: (seconds: number) => void
  onPlaybackStarted?: () => void
  onPlaybackPaused?: () => void
  onPlaybackEnded?: () => void

  constructor() {
    this.audio = new Audio()

    this.audio.addEventListener('play', () => {
      this._playing = true
      this.startRAF()
      this.onPlaybackStarted?.()
    })

    this.audio.addEventListener('pause', () => {
      this._playing = false
      this.stopRAF()
      this.onPlaybackPaused?.()
    })

    this.audio.addEventListener('ended', () => {
      this._playing = false
      this.stopRAF()
      this.onPlaybackEnded?.()
    })
  }

  /* ── Public API ──────────────────────────────────────────────── */

  async load(url: string): Promise<void> {
    this.stop()
    this.audio.src = url
    this.audio.load()

    return new Promise<void>((resolve, reject) => {
      const onCanPlay = () => {
        cleanup()
        resolve()
      }
      const onError = () => {
        cleanup()
        reject(new Error(`Failed to load audio: ${url}`))
      }
      const cleanup = () => {
        this.audio.removeEventListener('canplaythrough', onCanPlay)
        this.audio.removeEventListener('error', onError)
      }
      this.audio.addEventListener('canplaythrough', onCanPlay, { once: true })
      this.audio.addEventListener('error', onError, { once: true })
    })
  }

  play(): void {
    this.audio.play().catch((err) => {
      console.warn('FilePlayer: play() rejected:', err)
    })
  }

  pause(): void {
    this.audio.pause()
  }

  stop(): void {
    this.audio.pause()
    this.audio.currentTime = 0
    this._playing = false
    this.stopRAF()
  }

  seekTo(seconds: number): void {
    this.audio.currentTime = Math.max(0, Math.min(seconds, this.getDuration()))
    this.onPositionChange?.(this.audio.currentTime)
  }

  getCurrentTime(): number {
    return this.audio.currentTime
  }

  getDuration(): number {
    const d = this.audio.duration
    return Number.isFinite(d) ? d : 0
  }

  isPlaying(): boolean {
    return this._playing
  }

  setPlaybackRate(rate: number): void {
    this.audio.playbackRate = rate
  }

  setVolume(volume: number): void {
    // Accepts 0.0 - 1.0
    this.audio.volume = Math.max(0, Math.min(1, volume))
  }

  dispose(): void {
    this.stop()
    this.audio.removeAttribute('src')
    this.audio.load()
  }

  /* ── Internal RAF loop ───────────────────────────────────────── */

  private tick = (): void => {
    if (!this._playing) return
    this.onPositionChange?.(this.audio.currentTime)
    this.rafId = requestAnimationFrame(this.tick)
  }

  private startRAF(): void {
    this.stopRAF()
    this.rafId = requestAnimationFrame(this.tick)
  }

  private stopRAF(): void {
    if (this.rafId) {
      cancelAnimationFrame(this.rafId)
      this.rafId = 0
    }
  }
}
