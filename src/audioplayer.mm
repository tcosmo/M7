#include "audioplayer.h"

#import <AVFoundation/AVFoundation.h>
#include <QDebug>
#include <QUrl>
#include <algorithm>

namespace scoretracker {

AudioPlayer::AudioPlayer(QObject* parent)
    : QObject(parent)
{
    m_pollTimer.setInterval(16); // ~60Hz
    connect(&m_pollTimer, &QTimer::timeout, this, [this]() {
        if (m_playing && m_player) {
            AVAudioPlayer* player = (AVAudioPlayer*)m_player;
            double pos = player.currentTime;
            emit positionChanged(pos);

            // Check if playback reached the end
            if (!player.isPlaying) {
                m_playing = false;
                m_pollTimer.stop();
                emit playbackStopped();
            }
        }
    });
}

AudioPlayer::~AudioPlayer()
{
    cleanup();
}

void AudioPlayer::cleanup()
{
    m_pollTimer.stop();
    m_playing = false;

    if (m_player) {
        AVAudioPlayer* player = (AVAudioPlayer*)m_player;
        [player stop];
        [player release]; // balance the alloc+init
        m_player = nullptr;
    }
}

bool AudioPlayer::load(const QString& filePath)
{
    cleanup();

    NSURL* url = [NSURL fileURLWithPath:filePath.toNSString()];
    NSError* error = nil;
    AVAudioPlayer* player = [[AVAudioPlayer alloc] initWithContentsOfURL:url error:&error];

    if (!player || error) {
        qWarning() << "Failed to load audio file:" << filePath;
        if (error) {
            qWarning() << "Error:" << QString::fromNSString(error.localizedDescription);
        }
        return false;
    }

    BOOL prepared = [player prepareToPlay];
    m_duration = player.duration;
    m_player = (void*)player; // alloc+init already gives retain count 1

    qDebug() << "Audio prepared:" << (bool)prepared
             << "channels:" << player.numberOfChannels
             << "duration:" << m_duration;

    qDebug() << "Loaded audio:" << filePath << "duration:" << m_duration << "s";
    return true;
}

void AudioPlayer::play()
{
    if (!m_player) return;

    AVAudioPlayer* player = (AVAudioPlayer*)m_player;
    if (m_hasPendingSeek) {
        player.currentTime = m_pendingSeek;
        m_hasPendingSeek = false;
    }
    BOOL success = [player play];
    qDebug() << "Audio play:" << (bool)success << "isPlaying:" << (bool)player.isPlaying
             << "at:" << player.currentTime;
    m_playing = true;
    m_pollTimer.start();
    emit playbackStarted();
}

void AudioPlayer::pause()
{
    if (!m_player) return;

    AVAudioPlayer* player = (AVAudioPlayer*)m_player;
    [player pause];
    m_playing = false;
    m_pollTimer.stop();
    emit playbackPaused();
}

void AudioPlayer::stop()
{
    if (!m_player) return;

    AVAudioPlayer* player = (AVAudioPlayer*)m_player;
    [player stop];
    player.currentTime = 0;
    m_playing = false;
    m_pollTimer.stop();
    emit playbackStopped();
    emit positionChanged(0.0);
}

void AudioPlayer::seekTo(double seconds)
{
    if (!m_player) return;

    AVAudioPlayer* player = (AVAudioPlayer*)m_player;
    player.currentTime = seconds;
    m_pendingSeek = seconds;
    m_hasPendingSeek = true;
    emit positionChanged(seconds);
}

double AudioPlayer::currentTime() const
{
    if (!m_player) return 0.0;

    AVAudioPlayer* player = (AVAudioPlayer*)m_player;
    return player.currentTime;
}

double AudioPlayer::duration() const
{
    return m_duration;
}

bool AudioPlayer::isPlaying() const
{
    return m_playing;
}

void AudioPlayer::setVolume(double volume)
{
    m_volume = std::clamp(volume, 0.0, 1.0);
    if (m_player) {
        AVAudioPlayer* player = (AVAudioPlayer*)m_player;
        player.volume = static_cast<float>(m_volume);
    }
}

double AudioPlayer::volume() const
{
    return m_volume;
}

} // namespace scoretracker
