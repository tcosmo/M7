#pragma once

#include <QObject>
#include <QTimer>
#include <QString>

namespace scoretracker {

class AudioPlayer : public QObject
{
    Q_OBJECT

public:
    explicit AudioPlayer(QObject* parent = nullptr);
    ~AudioPlayer();

    bool load(const QString& filePath);
    void play();
    void pause();
    void stop();
    void seekTo(double seconds);
    double currentTime() const;
    double duration() const;
    bool isPlaying() const;
    void setVolume(double volume); // 0.0 – 1.0
    double volume() const;

signals:
    void positionChanged(double seconds);
    void playbackStarted();
    void playbackPaused();
    void playbackStopped();

private:
    void cleanup();

    void* m_player = nullptr; // AVAudioPlayer* (opaque)
    QTimer m_pollTimer;
    bool m_playing = false;
    double m_duration = 0.0;
    double m_pendingSeek = 0.0;
    bool m_hasPendingSeek = false;
    double m_volume = 1.0;
};

} // namespace scoretracker
