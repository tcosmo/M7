#include "youtubeplayer.h"
#include "theme.h"
#include <QDebug>
#include <QUrl>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QWebEnginePage>
#include <QWebEngineSettings>

namespace scoretracker {

// --- YTRequestInterceptor ---

void YTRequestInterceptor::interceptRequest(QWebEngineUrlRequestInfo& info)
{
    info.setHttpHeader("Referer", m_referer.toEncoded());
}

// --- YTBridge ---

void YTBridge::onReady(double duration)
{
    emit videoReady(duration);
}

void YTBridge::onStateChange(int state)
{
    // YT.PlayerState: PLAYING=1, PAUSED=2, ENDED=0, BUFFERING=3
    switch (state) {
    case 1: emit playbackStarted(); break;
    case 2: emit playbackPaused(); break;
    case 0: emit playbackStopped(); break;
    default: break;
    }
}

void YTBridge::onTimeUpdate(double seconds)
{
    emit positionChanged(seconds);
}

void YTBridge::onError(int code)
{
    qWarning() << "YT: error code" << code;
    emit errorOccurred(code);
}

void YTBridge::onAutoplayBlocked()
{
    qWarning() << "YT: autoplay blocked by browser — click the video to start playback";
    emit autoplayBlocked();
}

void YTBridge::onPlaybackRateChange(double rate)
{
    qDebug() << "YT: playback rate changed to" << rate;
    emit playbackRateChanged(rate);
}

// --- YouTubePlayer ---

YouTubePlayer::YouTubePlayer(QObject* parent)
    : QObject(parent)
{
    m_view = new QWebEngineView();
    m_view->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    // Allow programmatic playback without user gesture (Space key from toolbar)
    m_view->page()->settings()->setAttribute(
        QWebEngineSettings::PlaybackRequiresUserGesture, false);

    m_channel = new QWebChannel(this);
    m_bridge = new YTBridge(this);
    m_channel->registerObject(QStringLiteral("bridge"), m_bridge);
    m_view->page()->setWebChannel(m_channel);

    // Wire bridge signals to our signals and cached state
    connect(m_bridge, &YTBridge::videoReady, this, [this](double dur) {
        m_ready = true;
        if (dur > 0) {
            m_duration = dur;
            qDebug() << "YT: player ready, duration:" << dur;
        } else {
            qDebug() << "YT: player ready (duration not yet available, will resolve on play)";
        }
        emit videoReady(dur);
        if (m_pendingPlay) {
            qDebug() << "YT: executing queued play";
            m_pendingPlay = false;
            play();
        }
    });

    connect(m_bridge, &YTBridge::playbackStarted, this, [this]() {
        qDebug() << "YT: playbackStarted";
        m_playing = true;
        // Start local clock from current cached position
        m_syncPosition = m_currentTime;
        m_elapsed.start();
        m_localTimer.start();
        // Duration becomes available on first play — query it via JS
        if (m_duration <= 0) {
            m_view->page()->runJavaScript(
                QStringLiteral("player.getDuration()"),
                [this](const QVariant& result) {
                    double dur = result.toDouble();
                    if (dur > 0 && m_duration <= 0) {
                        m_duration = dur;
                        qDebug() << "YT: duration resolved:" << dur;
                        emit videoReady(dur);
                    }
                });
        }
        emit playbackStarted();
    });

    connect(m_bridge, &YTBridge::playbackPaused, this, [this]() {
        qDebug() << "YT: playbackPaused";
        m_playing = false;
        m_localTimer.stop();
        emit playbackPaused();
    });

    connect(m_bridge, &YTBridge::playbackStopped, this, [this]() {
        qDebug() << "YT: playbackStopped";
        m_playing = false;
        m_localTimer.stop();
        m_currentTime = 0.0;
        emit playbackStopped();
    });

    // JS position updates used only for drift correction (not emitted directly)
    connect(m_bridge, &YTBridge::positionChanged, this, [this](double secs) {
        m_syncPosition = secs;
        m_elapsed.restart();
        m_currentTime = secs;
    });

    // Error and autoplay-blocked handling
    connect(m_bridge, &YTBridge::errorOccurred, this, [](int code) {
        // 2=invalid param, 5=HTML5 error, 100=not found, 101/150=embed disallowed, 153=missing referer
        qWarning() << "YT: player error code" << code;
    });
    connect(m_bridge, &YTBridge::autoplayBlocked, this, []() {
        qWarning() << "YT: autoplay blocked — user must click video to start";
    });

    // Playback rate changes (from YouTube's own controls or JS)
    connect(m_bridge, &YTBridge::playbackRateChanged, this, [this](double rate) {
        m_playbackRate = rate;
        // Re-sync local clock so the new rate applies from now
        m_syncPosition = m_currentTime;
        m_elapsed.restart();
        emit playbackRateChanged(rate);
    });

    // Local timer: smooth position updates at ~60fps
    m_localTimer.setInterval(16);
    connect(&m_localTimer, &QTimer::timeout, this, [this]() {
        double pos = m_syncPosition + m_elapsed.elapsed() / 1000.0 * m_playbackRate;
        if (m_duration > 0 && pos > m_duration) pos = m_duration;
        m_currentTime = pos;
        emit positionChanged(pos);
    });
}

YouTubePlayer::~YouTubePlayer()
{
    delete m_view;
}

void YouTubePlayer::load(const QString& url)
{
    QString videoId = extractVideoId(url);
    if (videoId.isEmpty()) {
        qWarning() << "Could not extract YouTube video ID from:" << url;
        return;
    }

    qDebug() << "Loading YouTube video:" << videoId;
    QString bg = Theme::panelBg().name(); // e.g. "#252525" or "#e8e8e8"
    QString html = buildHtml(videoId, bg);
    m_view->setHtml(html, QUrl(QStringLiteral("https://localhost")));
}

void YouTubePlayer::play()
{
    if (!m_ready) {
        qDebug() << "YT: play() queued — player not ready yet";
        m_pendingPlay = true;
        return;
    }
    qDebug() << "YT: play()";
    m_view->page()->runJavaScript(QStringLiteral("ytPlay();"));
}

void YouTubePlayer::pause()
{
    m_pendingPlay = false;
    if (!m_ready) {
        qDebug() << "YT: pause() ignored — player not ready";
        return;
    }
    qDebug() << "YT: pause()";
    m_view->page()->runJavaScript(QStringLiteral("ytPause();"));
}

void YouTubePlayer::stop()
{
    m_pendingPlay = false;
    if (!m_ready) return;
    qDebug() << "YT: stop()";
    m_view->page()->runJavaScript(QStringLiteral("ytStop();"));
}

void YouTubePlayer::seekTo(double seconds)
{
    if (!m_ready) return;
    qDebug() << "YT: seekTo(" << seconds << ")";
    // Reset local clock to seek target immediately
    m_syncPosition = seconds;
    m_currentTime = seconds;
    m_elapsed.restart();
    m_view->page()->runJavaScript(
        QString("ytSeek(%1);").arg(seconds, 0, 'f', 3));
}

void YouTubePlayer::setPlaybackRate(double rate)
{
    if (!m_ready) return;
    qDebug() << "YT: setPlaybackRate(" << rate << ")";
    m_view->page()->runJavaScript(
        QString("player.setPlaybackRate(%1);").arg(rate, 0, 'f', 2));
}

QString YouTubePlayer::extractVideoId(const QString& url)
{
    // Handle youtu.be/ID
    QRegularExpression shortRe(QStringLiteral(R"(youtu\.be/([A-Za-z0-9_-]{11}))"));
    auto match = shortRe.match(url);
    if (match.hasMatch()) return match.captured(1);

    // Handle youtube.com/watch?v=ID
    QUrl parsed(url);
    QUrlQuery query(parsed);
    QString v = query.queryItemValue(QStringLiteral("v"));
    if (v.length() == 11) return v;

    // Handle youtube.com/embed/ID
    QRegularExpression embedRe(QStringLiteral(R"(/embed/([A-Za-z0-9_-]{11}))"));
    match = embedRe.match(url);
    if (match.hasMatch()) return match.captured(1);

    return {};
}

QString YouTubePlayer::buildHtml(const QString& videoId, const QString& bgColor) const
{
    return QString(R"HTML(
<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<style>
  * { margin: 0; padding: 0; }
  html, body { width: 100%%; height: 100%%; overflow: hidden; background: %2; }
  #player, #player iframe { width: 100%% !important; height: 100%% !important; }
</style>
<script src="qrc:///qtwebchannel/qwebchannel.js"></script>
</head><body>
<div id="player"></div>
<script>
var bridge = null;
var player = null;
var playerReady = false;
var pollTimer = null;

console.log('YT-JS: initializing QWebChannel');
new QWebChannel(qt.webChannelTransport, function(channel) {
    bridge = channel.objects.bridge;
    console.log('YT-JS: bridge connected, loading IFrame API');
    var tag = document.createElement('script');
    tag.src = 'https://www.youtube.com/iframe_api';
    document.head.appendChild(tag);
});

function onYouTubeIframeAPIReady() {
    console.log('YT-JS: IFrame API ready, creating player for video %1');
    player = new YT.Player('player', {
        width: '100%%',
        height: '100%%',
        videoId: '%1',
        playerVars: {
            autoplay: 0,
            controls: 1,
            modestbranding: 1,
            rel: 0,
            fs: 0,
            enablejsapi: 1
        },
        events: {
            onReady: function(e) {
                playerReady = true;
                var dur = player.getDuration();
                console.log('YT-JS: onReady, duration=' + dur
                    + ' state=' + player.getPlayerState()
                    + ' typeof playVideo=' + typeof player.playVideo);
                if (bridge) bridge.onReady(dur);
            },
            onStateChange: function(e) {
                console.log('YT-JS: onStateChange=' + e.data);
                if (bridge) bridge.onStateChange(e.data);
                if (e.data === YT.PlayerState.PLAYING) {
                    startPolling();
                } else {
                    stopPolling();
                    if (bridge && playerReady && player.getCurrentTime)
                        bridge.onTimeUpdate(player.getCurrentTime());
                }
            },
            onError: function(e) {
                console.log('YT-JS: onError code=' + e.data);
                if (bridge) bridge.onError(e.data);
            },
            onAutoplayBlocked: function() {
                console.log('YT-JS: autoplay blocked by browser');
                if (bridge) bridge.onAutoplayBlocked();
            },
            onPlaybackRateChange: function(e) {
                console.log('YT-JS: playback rate changed to ' + e.data);
                if (bridge) bridge.onPlaybackRateChange(e.data);
            }
        }
    });
    console.log('YT-JS: YT.Player constructor returned');
}

function ytPlay() {
    console.log('YT-JS: ytPlay() playerReady=' + playerReady
        + ' state=' + (playerReady ? player.getPlayerState() : 'N/A'));
    if (!playerReady) { console.log('YT-JS: not ready'); return; }
    try {
        player.playVideo();
        console.log('YT-JS: playVideo() called OK');
        setTimeout(function() {
            console.log('YT-JS: state 500ms after play=' + player.getPlayerState());
        }, 500);
    } catch(e) {
        console.log('YT-JS: playVideo() threw: ' + e);
    }
}

function ytPause() {
    if (!playerReady) return;
    player.pauseVideo();
}

function ytStop() {
    if (!playerReady) return;
    player.seekTo(0, true);
    player.pauseVideo();
}

function ytSeek(seconds) {
    if (!playerReady) return;
    player.seekTo(seconds, true);
}

function startPolling() {
    stopPolling();
    pollTimer = setInterval(function() {
        if (playerReady && player.getCurrentTime && bridge) {
            bridge.onTimeUpdate(player.getCurrentTime());
        }
    }, 1000);
}

function stopPolling() {
    if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
}
</script>
</body></html>
)HTML").arg(videoId, bgColor);
}

} // namespace scoretracker
