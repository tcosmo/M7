#pragma once

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QWebEngineView>
#include <QWebEngineProfile>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebChannel>
#include <QString>

namespace scoretracker {

// Sets Referer header on outgoing requests to YouTube
class YTRequestInterceptor : public QWebEngineUrlRequestInterceptor
{
    Q_OBJECT
public:
    explicit YTRequestInterceptor(const QUrl& referer, QObject* parent = nullptr)
        : QWebEngineUrlRequestInterceptor(parent), m_referer(referer) {}
    void interceptRequest(QWebEngineUrlRequestInfo& info) override;
private:
    QUrl m_referer;
};

// Bridge object exposed to JavaScript via QWebChannel
class YTBridge : public QObject
{
    Q_OBJECT

public:
    explicit YTBridge(QObject* parent = nullptr) : QObject(parent) {}

public slots:
    void onReady(double duration);
    void onStateChange(int state);
    void onTimeUpdate(double seconds);
    void onError(int code);
    void onAutoplayBlocked();
    void onPlaybackRateChange(double rate);

signals:
    void videoReady(double duration);
    void playbackStarted();
    void playbackPaused();
    void playbackStopped();
    void positionChanged(double seconds);
    void errorOccurred(int code);
    void autoplayBlocked();
    void playbackRateChanged(double rate);
};

class YouTubePlayer : public QObject
{
    Q_OBJECT

public:
    explicit YouTubePlayer(QObject* parent = nullptr);
    ~YouTubePlayer();

    void load(const QString& url, const QString& bgColor = "#000000");
    void play();
    void pause();
    void stop();
    void seekTo(double seconds);
    void setPlaybackRate(double rate);
    void setVolume(int volume); // 0–100
    void resizePlayer(int width, int height);
    int volume() const { return m_volume; }

    double currentTime() const { return m_currentTime; }
    double duration() const { return m_duration; }
    bool isPlaying() const { return m_playing; }
    double playbackRate() const { return m_playbackRate; }

    QWidget* videoWidget() const { return m_view; }

signals:
    void positionChanged(double seconds);
    void playbackStarted();
    void playbackPaused();
    void playbackStopped();
    void videoReady(double duration);
    void playbackRateChanged(double rate);

private:
    static QString extractVideoId(const QString& url);
    QString buildHtml(const QString& videoId, const QString& bgColor) const;

    QWebEngineView* m_view = nullptr;
    QWebChannel* m_channel = nullptr;
    YTBridge* m_bridge = nullptr;

    double m_currentTime = 0.0;
    double m_duration = 0.0;
    double m_playbackRate = 1.0;
    int m_volume = 100;
    bool m_playing = false;
    bool m_ready = false;
    bool m_pendingPlay = false;

    // Local clock for smooth position updates
    QTimer m_localTimer;
    QElapsedTimer m_elapsed;
    double m_syncPosition = 0.0; // YouTube position at last sync
};

} // namespace scoretracker
