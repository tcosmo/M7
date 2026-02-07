#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/miniaudio.h"

#include "audioplayer.h"

#include <QDebug>
#include <QFileInfo>

namespace scoretracker {

AudioPlayer::AudioPlayer(QObject* parent)
    : QObject(parent)
{
    m_pollTimer.setInterval(16); // ~60Hz
    connect(&m_pollTimer, &QTimer::timeout, this, [this]() {
        if (m_playing && m_sound) {
            double pos = currentTime();
            emit positionChanged(pos);

            // Check if playback reached the end
            if (ma_sound_at_end(m_sound)) {
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

    if (m_sound) {
        ma_sound_uninit(m_sound);
        delete m_sound;
        m_sound = nullptr;
    }

    if (m_engine) {
        ma_engine_uninit(m_engine);
        delete m_engine;
        m_engine = nullptr;
    }
}

bool AudioPlayer::load(const QString& filePath)
{
    cleanup();

    QFileInfo fi(filePath);
    if (!fi.exists()) {
        qWarning() << "Audio file not found:" << filePath;
        return false;
    }

    m_engine = new ma_engine;
    ma_engine_config config = ma_engine_config_init();
    ma_result result = ma_engine_init(&config, m_engine);
    if (result != MA_SUCCESS) {
        qWarning() << "Failed to initialize audio engine, error:" << result;
        delete m_engine;
        m_engine = nullptr;
        return false;
    }

    m_sound = new ma_sound;
    result = ma_sound_init_from_file(m_engine, filePath.toUtf8().constData(),
                                     MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION,
                                     nullptr, nullptr, m_sound);
    if (result != MA_SUCCESS) {
        qWarning() << "Failed to load audio file:" << filePath << "error:" << result;
        ma_engine_uninit(m_engine);
        delete m_engine;
        m_engine = nullptr;
        delete m_sound;
        m_sound = nullptr;
        return false;
    }

    // Get duration
    float lengthSeconds = 0;
    result = ma_sound_get_length_in_seconds(m_sound, &lengthSeconds);
    if (result == MA_SUCCESS) {
        m_duration = static_cast<double>(lengthSeconds);
    }

    qDebug() << "Loaded audio:" << filePath << "duration:" << m_duration << "s";
    return true;
}

void AudioPlayer::play()
{
    if (!m_sound) return;

    ma_sound_start(m_sound);
    m_playing = true;
    m_pollTimer.start();
    emit playbackStarted();
}

void AudioPlayer::pause()
{
    if (!m_sound) return;

    ma_sound_stop(m_sound);
    m_playing = false;
    m_pollTimer.stop();
    emit playbackPaused();
}

void AudioPlayer::stop()
{
    if (!m_sound) return;

    ma_sound_stop(m_sound);
    ma_sound_seek_to_pcm_frame(m_sound, 0);
    m_playing = false;
    m_pollTimer.stop();
    emit playbackStopped();
    emit positionChanged(0.0);
}

void AudioPlayer::seekTo(double seconds)
{
    if (!m_sound || !m_engine) return;

    ma_uint32 sampleRate = ma_engine_get_sample_rate(m_engine);
    ma_uint64 frame = static_cast<ma_uint64>(seconds * sampleRate);
    ma_sound_seek_to_pcm_frame(m_sound, frame);
    emit positionChanged(seconds);
}

double AudioPlayer::currentTime() const
{
    if (!m_sound) return 0.0;

    float cursor = 0;
    ma_sound_get_cursor_in_seconds(m_sound, &cursor);
    return static_cast<double>(cursor);
}

double AudioPlayer::duration() const
{
    return m_duration;
}

bool AudioPlayer::isPlaying() const
{
    return m_playing;
}

} // namespace scoretracker
