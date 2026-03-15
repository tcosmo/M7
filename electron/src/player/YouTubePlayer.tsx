import React, {
  forwardRef,
  useEffect,
  useImperativeHandle,
  useRef,
  useCallback,
} from 'react'

/* ── YouTube IFrame API global types ─────────────────────────────── */

declare global {
  interface Window {
    YT: typeof YT
    onYouTubeIframeAPIReady: (() => void) | undefined
  }
  // eslint-disable-next-line @typescript-eslint/no-namespace
  namespace YT {
    const PlayerState: {
      PLAYING: number
      PAUSED: number
      ENDED: number
      BUFFERING: number
      CUED: number
    }
    class Player {
      constructor(el: string | HTMLElement, opts: Record<string, unknown>)
      playVideo(): void
      pauseVideo(): void
      stopVideo(): void
      seekTo(seconds: number, allowSeekAhead?: boolean): void
      getCurrentTime(): number
      getDuration(): number
      getPlayerState(): number
      setPlaybackRate(rate: number): void
      setVolume(volume: number): void
      destroy(): void
    }
  }
}

/* ── Props / Ref types ───────────────────────────────────────────── */

export interface YouTubePlayerProps {
  videoId: string
  onReady?: (duration: number) => void
  onPositionChange?: (seconds: number) => void
  onPlaybackStarted?: () => void
  onPlaybackPaused?: () => void
  onPlaybackStopped?: () => void
  onPlaybackRateChange?: (rate: number) => void
  style?: React.CSSProperties
}

export interface YouTubePlayerRef {
  play(): void
  pause(): void
  stop(): void
  seekTo(seconds: number): void
  getCurrentTime(): number
  getDuration(): number
  isPlaying(): boolean
  setPlaybackRate(rate: number): void
  setVolume(volume: number): void
}

/* ── Helpers ─────────────────────────────────────────────────────── */

let apiLoading = false
let apiReady = false
const apiReadyCallbacks: (() => void)[] = []

function loadYouTubeAPI(): Promise<void> {
  if (apiReady) return Promise.resolve()
  return new Promise<void>((resolve) => {
    apiReadyCallbacks.push(resolve)
    if (apiLoading) return
    apiLoading = true

    const prev = window.onYouTubeIframeAPIReady
    window.onYouTubeIframeAPIReady = () => {
      apiReady = true
      apiLoading = false
      prev?.()
      apiReadyCallbacks.forEach((cb) => cb())
      apiReadyCallbacks.length = 0
    }

    const script = document.createElement('script')
    script.src = 'https://www.youtube.com/iframe_api'
    document.head.appendChild(script)
  })
}

/* ── Component ───────────────────────────────────────────────────── */

const YouTubePlayer = forwardRef<YouTubePlayerRef, YouTubePlayerProps>(
  function YouTubePlayer(props, ref) {
    const {
      videoId,
      onReady,
      onPositionChange,
      onPlaybackStarted,
      onPlaybackPaused,
      onPlaybackStopped,
      onPlaybackRateChange,
      style,
    } = props

    const containerRef = useRef<HTMLDivElement>(null)
    const playerRef = useRef<YT.Player | null>(null)
    const readyRef = useRef(false)
    const playingRef = useRef(false)
    const pendingPlayRef = useRef(false)
    const durationRef = useRef(0)
    const playbackRateRef = useRef(1)

    // Local clock state for smooth position updates
    const syncPositionRef = useRef(0)
    const syncTimestampRef = useRef(0) // performance.now() at last sync
    const currentTimeRef = useRef(0)
    const rafRef = useRef(0)
    const pollIntervalRef = useRef(0)

    // Stable callback refs
    const onReadyRef = useRef(onReady)
    const onPositionChangeRef = useRef(onPositionChange)
    const onPlaybackStartedRef = useRef(onPlaybackStarted)
    const onPlaybackPausedRef = useRef(onPlaybackPaused)
    const onPlaybackStoppedRef = useRef(onPlaybackStopped)
    const onPlaybackRateChangeRef = useRef(onPlaybackRateChange)

    useEffect(() => { onReadyRef.current = onReady }, [onReady])
    useEffect(() => { onPositionChangeRef.current = onPositionChange }, [onPositionChange])
    useEffect(() => { onPlaybackStartedRef.current = onPlaybackStarted }, [onPlaybackStarted])
    useEffect(() => { onPlaybackPausedRef.current = onPlaybackPaused }, [onPlaybackPaused])
    useEffect(() => { onPlaybackStoppedRef.current = onPlaybackStopped }, [onPlaybackStopped])
    useEffect(() => { onPlaybackRateChangeRef.current = onPlaybackRateChange }, [onPlaybackRateChange])

    /* ── RAF loop: smooth ~60fps position from local clock ───────── */

    const tick = useCallback(() => {
      if (!playingRef.current) return
      const elapsed = (performance.now() - syncTimestampRef.current) / 1000
      let pos = syncPositionRef.current + elapsed * playbackRateRef.current
      if (durationRef.current > 0 && pos > durationRef.current) {
        pos = durationRef.current
      }
      currentTimeRef.current = pos
      onPositionChangeRef.current?.(pos)
      rafRef.current = requestAnimationFrame(tick)
    }, [])

    const startLocalClock = useCallback(() => {
      syncPositionRef.current = currentTimeRef.current
      syncTimestampRef.current = performance.now()
      rafRef.current = requestAnimationFrame(tick)
    }, [tick])

    const stopLocalClock = useCallback(() => {
      cancelAnimationFrame(rafRef.current)
    }, [])

    /* ── Periodic YouTube sync (every 500ms while playing) ───────── */

    const startPolling = useCallback(() => {
      stopPolling()
      pollIntervalRef.current = window.setInterval(() => {
        if (readyRef.current && playerRef.current && playingRef.current) {
          const ytTime = playerRef.current.getCurrentTime()
          syncPositionRef.current = ytTime
          syncTimestampRef.current = performance.now()
          currentTimeRef.current = ytTime
        }
      }, 500)
    }, [])

    const stopPolling = useCallback(() => {
      if (pollIntervalRef.current) {
        clearInterval(pollIntervalRef.current)
        pollIntervalRef.current = 0
      }
    }, [])

    /* ── Create / destroy YT.Player ──────────────────────────────── */

    useEffect(() => {
      if (!videoId) return

      let destroyed = false

      loadYouTubeAPI().then(() => {
        if (destroyed || !containerRef.current) return

        // Create a target div inside our container
        const target = document.createElement('div')
        containerRef.current.innerHTML = ''
        containerRef.current.appendChild(target)

        const player = new window.YT.Player(target, {
          width: '100%',
          height: '100%',
          videoId,
          playerVars: {
            autoplay: 0,
            controls: 1,
            modestbranding: 1,
            rel: 0,
            fs: 0,
            enablejsapi: 1,
          },
          events: {
            onReady: () => {
              if (destroyed) return
              readyRef.current = true
              const dur = player.getDuration()
              durationRef.current = dur
              onReadyRef.current?.(dur)

              if (pendingPlayRef.current) {
                pendingPlayRef.current = false
                player.playVideo()
              }
            },
            onStateChange: (e: { data: number }) => {
              if (destroyed) return
              switch (e.data) {
                case window.YT.PlayerState.PLAYING: {
                  playingRef.current = true
                  // Resolve duration on first play if not yet known
                  if (durationRef.current <= 0) {
                    const dur = player.getDuration()
                    if (dur > 0) {
                      durationRef.current = dur
                      onReadyRef.current?.(dur)
                    }
                  }
                  startLocalClock()
                  startPolling()
                  onPlaybackStartedRef.current?.()
                  break
                }
                case window.YT.PlayerState.PAUSED:
                  playingRef.current = false
                  stopLocalClock()
                  stopPolling()
                  // Get final position from YouTube
                  currentTimeRef.current = player.getCurrentTime()
                  onPositionChangeRef.current?.(currentTimeRef.current)
                  onPlaybackPausedRef.current?.()
                  break
                case window.YT.PlayerState.ENDED:
                  playingRef.current = false
                  stopLocalClock()
                  stopPolling()
                  currentTimeRef.current = 0
                  onPlaybackStoppedRef.current?.()
                  break
              }
            },
            onPlaybackRateChange: (e: { data: number }) => {
              if (destroyed) return
              playbackRateRef.current = e.data
              // Re-sync local clock so the new rate applies from now
              syncPositionRef.current = currentTimeRef.current
              syncTimestampRef.current = performance.now()
              onPlaybackRateChangeRef.current?.(e.data)
            },
            onError: (e: { data: number }) => {
              console.warn('YouTubePlayer: error code', e.data)
            },
          },
        })
        playerRef.current = player
      })

      return () => {
        destroyed = true
        stopLocalClock()
        stopPolling()
        readyRef.current = false
        playingRef.current = false
        pendingPlayRef.current = false

        if (playerRef.current) {
          try {
            playerRef.current.destroy()
          } catch {
            // player may already be gone
          }
          playerRef.current = null
        }
      }
    }, [videoId, startLocalClock, stopLocalClock, startPolling, stopPolling])

    /* ── Imperative handle ───────────────────────────────────────── */

    useImperativeHandle(ref, () => ({
      play() {
        if (!readyRef.current) {
          pendingPlayRef.current = true
          return
        }
        playerRef.current?.playVideo()
      },
      pause() {
        pendingPlayRef.current = false
        playerRef.current?.pauseVideo()
      },
      stop() {
        pendingPlayRef.current = false
        if (!readyRef.current || !playerRef.current) return
        playerRef.current.seekTo(0, true)
        playerRef.current.pauseVideo()
      },
      seekTo(seconds: number) {
        if (!readyRef.current || !playerRef.current) return
        syncPositionRef.current = seconds
        currentTimeRef.current = seconds
        syncTimestampRef.current = performance.now()
        playerRef.current.seekTo(seconds, true)
      },
      getCurrentTime() {
        return currentTimeRef.current
      },
      getDuration() {
        return durationRef.current
      },
      isPlaying() {
        return playingRef.current
      },
      setPlaybackRate(rate: number) {
        playbackRateRef.current = rate
        playerRef.current?.setPlaybackRate(rate)
      },
      setVolume(volume: number) {
        playerRef.current?.setVolume(Math.round(Math.max(0, Math.min(100, volume))))
      },
    }))

    /* ── Render ───────────────────────────────────────────────────── */

    return (
      <div
        ref={containerRef}
        style={{
          width: '100%',
          height: '100%',
          background: '#000',
          ...style,
        }}
      />
    )
  },
)

export default YouTubePlayer
