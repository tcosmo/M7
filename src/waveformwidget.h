#pragma once

#ifdef USE_MUSESCORE

#include <QWidget>
#include <QScrollArea>
#include <QImage>
#include <vector>

namespace scoretracker {

class SyncMode;

class WaveformCanvas : public QWidget
{
    Q_OBJECT

public:
    explicit WaveformCanvas(QWidget* parent = nullptr);

    bool loadAudio(const QString& filePath);
    void setSyncMode(SyncMode* syncMode);
    void setPlaybackTime(double time);
    void setDuration(double duration);
    void setWaveformZoom(double zoom);
    double waveformZoom() const { return m_zoom; }
    double duration() const { return m_duration; }
    double playbackTime() const { return m_playbackTime; }
    int timeToX(double time) const;
    double xToTime(int x) const;
    void computePeaks();
    void updateWidth();
    void setShowSpectrogram(bool show);
    bool showSpectrogram() const { return m_showSpectrogram; }
    void renderSpectrogramImage();

signals:
    void seekRequested(double seconds);
    void beatTimeChanged(int beatIndex, double newTime);
    void beatClicked(int beatIndex);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    int hitTestDot(const QPoint& pos) const;
    void computeSpectrogram();

    std::vector<float> m_samples;
    int m_sampleRate = 44100;
    std::vector<std::pair<float, float>> m_peaks;
    double m_duration = 0.0;
    double m_playbackTime = -1.0;
    double m_zoom = 1.0;
    SyncMode* m_syncMode = nullptr;

    bool m_dragging = false;
    int m_dragBeatIndex = -1;
    int m_dragStartX = 0;
    double m_dragStartTime = 0.0;

    // Spectrogram
    bool m_showSpectrogram = false;
    std::vector<std::vector<float>> m_spectrogramData; // [timeSlice][freqBin]
    int m_fftSize = 2048;
    int m_hopSize = 512;
    QImage m_spectrogramImage;
};

class WaveformWidget : public QScrollArea
{
    Q_OBJECT

public:
    explicit WaveformWidget(QWidget* parent = nullptr);

    bool loadAudio(const QString& filePath);
    void setSyncMode(SyncMode* syncMode);
    void setPlaybackTime(double time);
    void setDuration(double duration);
    void setWaveformZoom(double zoom);
    double waveformZoom() const;
    void setRightMargin(int margin);
    void setShowSpectrogram(bool show);
    void scrollToTime(double time);

signals:
    void seekRequested(double seconds);
    void beatTimeChanged(int beatIndex, double newTime);
    void zoomChanged(double zoom);
    void beatClicked(int beatIndex);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool event(QEvent* event) override;

private:
    void ensureCursorVisible();
    void updateHeight();

    WaveformCanvas* m_canvas = nullptr;
};

} // namespace scoretracker

#endif // USE_MUSESCORE
