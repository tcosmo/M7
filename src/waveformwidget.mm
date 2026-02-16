#include "waveformwidget.h"
#include "syncmode.h"
#include "theme.h"

#import <AudioToolbox/AudioToolbox.h>
#import <Accelerate/Accelerate.h>

#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QNativeGestureEvent>
#include <QScrollBar>
#include <cmath>

namespace scoretracker {

static const double KEY_ADJUST_STEP = 0.01; // 10ms per arrow key press
static const int WAVEFORM_HEIGHT = 250;
static const int SPECTROGRAM_HEIGHT = 100;

// --- WaveformCanvas ---

WaveformCanvas::WaveformCanvas(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(WAVEFORM_HEIGHT);
    setAutoFillBackground(true);
    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(30, 30, 30));
    setPalette(pal);
}

bool WaveformCanvas::loadAudio(const QString& filePath)
{
    m_samples.clear();
    m_peaks.clear();

    NSString* nsPath = filePath.toNSString();
    NSURL* nsUrl = [NSURL fileURLWithPath:nsPath];
    CFURLRef url = (__bridge CFURLRef)nsUrl;

    ExtAudioFileRef audioFile = nullptr;
    OSStatus status = ExtAudioFileOpenURL(url, &audioFile);
    if (status != noErr || !audioFile) return false;

    AudioStreamBasicDescription outFormat = {};
    outFormat.mSampleRate = 44100;
    outFormat.mFormatID = kAudioFormatLinearPCM;
    outFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    outFormat.mBitsPerChannel = 32;
    outFormat.mChannelsPerFrame = 1;
    outFormat.mFramesPerPacket = 1;
    outFormat.mBytesPerFrame = 4;
    outFormat.mBytesPerPacket = 4;
    m_sampleRate = 44100;

    ExtAudioFileSetProperty(audioFile,
        kExtAudioFileProperty_ClientDataFormat,
        sizeof(outFormat), &outFormat);

    SInt64 totalFrames = 0;
    UInt32 propSize = sizeof(totalFrames);
    ExtAudioFileGetProperty(audioFile,
        kExtAudioFileProperty_FileLengthFrames,
        &propSize, &totalFrames);

    if (totalFrames > 0) {
        AudioStreamBasicDescription srcFormat = {};
        propSize = sizeof(srcFormat);
        ExtAudioFileGetProperty(audioFile,
            kExtAudioFileProperty_FileDataFormat,
            &propSize, &srcFormat);
        double ratio = 44100.0 / (srcFormat.mSampleRate > 0 ? srcFormat.mSampleRate : 44100);
        m_samples.reserve(static_cast<size_t>(totalFrames * ratio) + 4096);
    }

    const UInt32 bufferSize = 8192;
    std::vector<float> buffer(bufferSize);

    while (true) {
        AudioBufferList bufList;
        bufList.mNumberBuffers = 1;
        bufList.mBuffers[0].mNumberChannels = 1;
        bufList.mBuffers[0].mDataByteSize = bufferSize * sizeof(float);
        bufList.mBuffers[0].mData = buffer.data();

        UInt32 frames = bufferSize;
        status = ExtAudioFileRead(audioFile, &frames, &bufList);
        if (status != noErr || frames == 0) break;

        m_samples.insert(m_samples.end(), buffer.begin(), buffer.begin() + frames);
    }

    ExtAudioFileDispose(audioFile);

    if (!m_samples.empty()) {
        m_duration = static_cast<double>(m_samples.size()) / m_sampleRate;
    }

    updateWidth();
    computePeaks();
    update();
    return true;
}

void WaveformCanvas::updateWidth()
{
    if (m_duration <= 0) return;
    // At zoom 1.0, fill the scroll area viewport. At higher zoom, expand.
    QScrollArea* sa = qobject_cast<QScrollArea*>(parentWidget() ? parentWidget()->parentWidget() : nullptr);
    int baseWidth = sa ? sa->viewport()->width() : 800;
    int vpHeight = sa ? sa->viewport()->height() : WAVEFORM_HEIGHT;
    if (baseWidth <= 0) baseWidth = 800;
    if (vpHeight <= 0) vpHeight = WAVEFORM_HEIGHT;
    int w = static_cast<int>(baseWidth * m_zoom);
    if (w < baseWidth) w = baseWidth;
    setMinimumWidth(w);
    setMinimumHeight(vpHeight);
    resize(w, vpHeight);
}

void WaveformCanvas::setWaveformZoom(double zoom)
{
    m_zoom = std::max(1.0, std::min(zoom, 100.0));
    updateWidth();
    computePeaks();
    update();
}

void WaveformCanvas::computePeaks()
{
    m_peaks.clear();
    if (m_samples.empty() || width() <= 0) return;

    int numBins = width();
    double samplesPerBin = static_cast<double>(m_samples.size()) / numBins;

    m_peaks.resize(numBins);
    for (int i = 0; i < numBins; ++i) {
        int start = static_cast<int>(i * samplesPerBin);
        int end = static_cast<int>((i + 1) * samplesPerBin);
        end = std::min(end, static_cast<int>(m_samples.size()));

        float minVal = 0, maxVal = 0;
        for (int j = start; j < end; ++j) {
            minVal = std::min(minVal, m_samples[j]);
            maxVal = std::max(maxVal, m_samples[j]);
        }
        m_peaks[i] = {minVal, maxVal};
    }
}

void WaveformCanvas::setSyncMode(SyncMode* syncMode)
{
    m_syncMode = syncMode;
    update();
}

void WaveformCanvas::setPlaybackTime(double time)
{
    m_playbackTime = time;
    update();
}

void WaveformCanvas::setDuration(double duration)
{
    m_duration = duration;
    updateWidth();
    computePeaks();
    update();
}

int WaveformCanvas::timeToX(double time) const
{
    if (m_duration <= 0) return 0;
    return static_cast<int>((time / m_duration) * width());
}

double WaveformCanvas::xToTime(int x) const
{
    if (width() <= 0 || m_duration <= 0) return 0;
    return (static_cast<double>(x) / width()) * m_duration;
}

int WaveformCanvas::hitTestDot(const QPoint& pos) const
{
    if (!m_syncMode || m_duration <= 0) return -1;

    int hitMargin = 5;
    const auto& beats = m_syncMode->beats();

    for (size_t i = 0; i < beats.size(); ++i) {
        if (!beats[i].synced) continue;
        int dx = timeToX(beats[i].effectiveTime());
        if (std::abs(pos.x() - dx) <= hitMargin) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void WaveformCanvas::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    int w = width();
    int h = height();
    p.setClipRect(0, 0, w, h);
    int waveH = m_showSpectrogram ? h / 2 : h;
    int midY = waveH / 2;

    // Clear background
    p.fillRect(0, 0, w, h, QColor(37, 37, 37));

    if (static_cast<int>(m_peaks.size()) != w && !m_samples.empty()) {
        computePeaks();
    }

    // Draw waveform
    p.setRenderHint(QPainter::Antialiasing, false);
    QColor waveColor(180, 120, 90);
    for (int x = 0; x < static_cast<int>(m_peaks.size()); ++x) {
        float minVal = m_peaks[x].first;
        float maxVal = m_peaks[x].second;

        int y1 = midY - static_cast<int>(maxVal * midY);
        int y2 = midY - static_cast<int>(minVal * midY);
        if (y2 - y1 < 1) y2 = y1 + 1;

        p.fillRect(x, y1, 1, y2 - y1, waveColor);
    }

    // Draw spectrogram below waveform
    if (m_showSpectrogram && !m_spectrogramImage.isNull()) {
        QRect destRect(0, waveH, w, h - waveH);
        p.drawImage(destRect, m_spectrogramImage);
    }

    // Draw sync lines
    if (m_syncMode && m_duration > 0) {
        int nextToTap = m_syncMode->nextUnsyncedBeat();
        const auto& beats = m_syncMode->beats();
        for (size_t i = 0; i < beats.size(); ++i) {
            if (!beats[i].synced) continue;
            int dx = timeToX(beats[i].effectiveTime());

            if (static_cast<int>(i) == nextToTap) {
                // Next-to-tap: bright orange line
                p.setPen(QPen(QColor(255, 180, 0), 2));
            } else {
                // Synced: blue line
                p.setPen(QPen(QColor(30, 120, 255, 180), 1));
            }
            p.drawLine(dx, 0, dx, waveH);
        }
    }

    // Draw playback cursor (full height including spectrogram)
    if (m_duration > 0 && m_playbackTime >= 0) {
        int cx = timeToX(m_playbackTime);
        p.setPen(QPen(Qt::white, 1));
        p.drawLine(cx, 0, cx, h);
    }
}

void WaveformCanvas::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || m_duration <= 0) return;

    int hit = hitTestDot(event->pos());
    if (hit >= 0) {
        if (m_syncMode) {
            m_syncMode->setNextUnsyncedFrom(hit);
            if (m_syncMode->beats()[hit].synced) {
                m_dragging = true;
                m_dragBeatIndex = hit;
                m_dragStartX = event->pos().x();
                m_dragStartTime = m_syncMode->beats()[hit].effectiveTime();
                setCursor(Qt::SizeHorCursor);
            }
        }
        setFocus();
        emit beatClicked(hit);
        update();
        return;
    }

    double t = xToTime(event->pos().x());
    m_playbackTime = t;
    emit seekRequested(t);
    update();
}

void WaveformCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && m_dragBeatIndex >= 0 && m_syncMode) {
        double newTime = xToTime(event->pos().x());
        newTime = std::max(0.0, std::min(newTime, m_duration));

        // Clamp between previous and next synced beats
        const auto& beats = m_syncMode->beats();
        double minTime = 0.0;
        double maxTime = m_duration;
        for (int i = m_dragBeatIndex - 1; i >= 0; --i) {
            if (beats[i].synced) { minTime = beats[i].effectiveTime(); break; }
        }
        for (int i = m_dragBeatIndex + 1; i < static_cast<int>(beats.size()); ++i) {
            if (beats[i].synced) { maxTime = beats[i].effectiveTime(); break; }
        }
        newTime = std::max(minTime, std::min(newTime, maxTime));

        double delta = newTime - beats[m_dragBeatIndex].effectiveTime();
        m_syncMode->adjustBeat(m_dragBeatIndex, delta);
        m_playbackTime = newTime;
        emit seekRequested(newTime);
        emit beatTimeChanged(m_dragBeatIndex, newTime);
        update();
        return;
    }

    int hit = hitTestDot(event->pos());
    setCursor(hit >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void WaveformCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        m_dragBeatIndex = -1;
        setCursor(Qt::ArrowCursor);
    }
}

void WaveformCanvas::keyPressEvent(QKeyEvent* event)
{
    QWidget::keyPressEvent(event);
}

void WaveformCanvas::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    computePeaks();
}

void WaveformCanvas::setShowSpectrogram(bool show)
{
    if (m_showSpectrogram == show) return;
    m_showSpectrogram = show;

    if (show && m_spectrogramData.empty() && !m_samples.empty()) {
        computeSpectrogram();
    }
    if (show) {
        renderSpectrogramImage();
    }

    updateWidth();
    update();
}

void WaveformCanvas::computeSpectrogram()
{
    m_spectrogramData.clear();
    if (m_samples.empty()) return;

    int n = static_cast<int>(m_samples.size());
    int log2n = static_cast<int>(std::log2(m_fftSize));
    FFTSetup fftSetup = vDSP_create_fftsetup(log2n, FFT_RADIX2);
    if (!fftSetup) return;

    int numBins = m_fftSize / 2;
    std::vector<float> window(m_fftSize);
    vDSP_hann_window(window.data(), m_fftSize, vDSP_HANN_NORM);

    std::vector<float> frame(m_fftSize);
    std::vector<float> realp(numBins);
    std::vector<float> imagp(numBins);
    DSPSplitComplex split = { realp.data(), imagp.data() };

    for (int pos = 0; pos + m_fftSize <= n; pos += m_hopSize) {
        // Window the frame
        vDSP_vmul(&m_samples[pos], 1, window.data(), 1, frame.data(), 1, m_fftSize);

        // Pack for real FFT
        vDSP_ctoz(reinterpret_cast<const DSPComplex*>(frame.data()), 2, &split, 1, numBins);

        // FFT
        vDSP_fft_zrip(fftSetup, &split, 1, log2n, FFT_FORWARD);

        // Magnitude (power spectrum)
        std::vector<float> mag(numBins);
        vDSP_zvmags(&split, 1, mag.data(), 1, numBins);

        // Convert to dB
        float ref = 1.0f;
        vDSP_vdbcon(mag.data(), 1, &ref, mag.data(), 1, numBins, 0);

        m_spectrogramData.push_back(std::move(mag));
    }

    vDSP_destroy_fftsetup(fftSetup);
}

void WaveformCanvas::renderSpectrogramImage()
{
    if (m_spectrogramData.empty()) return;

    int numSlices = static_cast<int>(m_spectrogramData.size());
    int numBins = m_fftSize / 2;
    // Only show lower frequencies (up to ~8kHz)
    int maxBin = std::min(numBins, static_cast<int>(8000.0 * m_fftSize / m_sampleRate));

    // Create source image at spectrogram data resolution
    QImage img(numSlices, maxBin, QImage::Format_RGB32);

    // Find range for normalization
    float minDb = -80.0f;
    float maxDb = 0.0f;

    for (int x = 0; x < numSlices; ++x) {
        const auto& slice = m_spectrogramData[x];
        for (int y = 0; y < maxBin; ++y) {
            float db = slice[y];
            float norm = (db - minDb) / (maxDb - minDb);
            norm = std::max(0.0f, std::min(1.0f, norm));

            // Color map: dark blue -> cyan -> yellow -> white
            int r, g, b;
            if (norm < 0.25f) {
                float t = norm / 0.25f;
                r = 0; g = 0; b = static_cast<int>(40 + 120 * t);
            } else if (norm < 0.5f) {
                float t = (norm - 0.25f) / 0.25f;
                r = 0; g = static_cast<int>(180 * t); b = static_cast<int>(160 - 40 * t);
            } else if (norm < 0.75f) {
                float t = (norm - 0.5f) / 0.25f;
                r = static_cast<int>(255 * t); g = static_cast<int>(180 + 75 * t); b = static_cast<int>(120 - 120 * t);
            } else {
                float t = (norm - 0.75f) / 0.25f;
                r = 255; g = 255; b = static_cast<int>(255 * t);
            }

            // Flip y: low frequencies at bottom
            img.setPixel(x, maxBin - 1 - y, qRgb(r, g, b));
        }
    }

    m_spectrogramImage = img;
}

// --- WaveformWidget (scroll area wrapper) ---

WaveformWidget::WaveformWidget(QWidget* parent)
    : QScrollArea(parent)
{
    setMinimumHeight(40);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);

    m_canvas = new WaveformCanvas(this);
    setWidget(m_canvas);
    setWidgetResizable(false);

    connect(m_canvas, &WaveformCanvas::seekRequested, this, &WaveformWidget::seekRequested);
    connect(m_canvas, &WaveformCanvas::beatTimeChanged, this, &WaveformWidget::beatTimeChanged);
    connect(m_canvas, &WaveformCanvas::beatClicked, this, &WaveformWidget::beatClicked);
}

bool WaveformWidget::loadAudio(const QString& filePath)
{
    return m_canvas->loadAudio(filePath);
}

void WaveformWidget::setSyncMode(SyncMode* syncMode)
{
    m_canvas->setSyncMode(syncMode);
}

void WaveformWidget::setPlaybackTime(double time)
{
    m_canvas->setPlaybackTime(time);
    ensureCursorVisible();
}

void WaveformWidget::setDuration(double duration)
{
    m_canvas->setDuration(duration);
}


void WaveformWidget::setRightMargin(int margin)
{
    setViewportMargins(0, 0, margin, 0);
    m_canvas->updateWidth();
    m_canvas->computePeaks();
    m_canvas->update();
}

void WaveformWidget::setWaveformZoom(double zoom)
{
    m_canvas->setWaveformZoom(zoom);
    emit zoomChanged(m_canvas->waveformZoom());
    ensureCursorVisible();
}

double WaveformWidget::waveformZoom() const
{
    return m_canvas->waveformZoom();
}

bool WaveformWidget::event(QEvent* event)
{
    if (event->type() == QEvent::NativeGesture) {
        auto* gesture = static_cast<QNativeGestureEvent*>(event);
        if (gesture->gestureType() == Qt::ZoomNativeGesture) {
            double delta = gesture->value(); // +/- fraction
            double newZoom = m_canvas->waveformZoom() * (1.0 + delta);
            m_canvas->setWaveformZoom(newZoom);
            emit zoomChanged(m_canvas->waveformZoom());
            ensureCursorVisible();
            return true;
        }
    }
    return QScrollArea::event(event);
}

void WaveformWidget::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::MetaModifier) {
        // Cmd+scroll to zoom
        double delta = event->angleDelta().y() > 0 ? 1.2 : 1.0 / 1.2;
        double newZoom = m_canvas->waveformZoom() * delta;
        m_canvas->setWaveformZoom(newZoom);
        emit zoomChanged(m_canvas->waveformZoom());
        ensureCursorVisible();
        event->accept();
    } else {
        // Horizontal panning: use x delta (trackpad) or y delta (mouse wheel)
        int dx = event->angleDelta().x();
        if (dx == 0) dx = event->angleDelta().y();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - dx);
        event->accept();
    }
}

void WaveformWidget::resizeEvent(QResizeEvent* event)
{
    QScrollArea::resizeEvent(event);
    m_canvas->updateWidth();
    m_canvas->computePeaks();
    m_canvas->renderSpectrogramImage();
    m_canvas->update();
}

void WaveformWidget::scrollToTime(double time)
{
    if (time < 0 || m_canvas->duration() <= 0) return;
    int cx = m_canvas->timeToX(time);
    int vpW = viewport()->width();
    int target = cx - vpW / 3;
    if (target < 0) target = 0;
    horizontalScrollBar()->setValue(target);
}

void WaveformWidget::ensureCursorVisible()
{
    double t = m_canvas->playbackTime();
    if (t < 0 || m_canvas->duration() <= 0) return;

    int cx = m_canvas->timeToX(t);
    int vpW = viewport()->width();
    int scrollX = horizontalScrollBar()->value();
    int target = vpW * 3 / 4;

    // Keep cursor at ~3/4 mark by scrolling continuously
    int desiredScroll = cx - target;
    if (desiredScroll < 0) desiredScroll = 0;
    if (desiredScroll > scrollX) {
        horizontalScrollBar()->setValue(desiredScroll);
    } else if (cx < scrollX) {
        horizontalScrollBar()->setValue(cx);
    }
}

void WaveformWidget::setShowSpectrogram(bool show)
{
    m_canvas->setShowSpectrogram(show);
    updateHeight();
}

void WaveformWidget::updateHeight()
{
    int desiredH = m_canvas->showSpectrogram() ? WAVEFORM_HEIGHT + SPECTROGRAM_HEIGHT + 2 : WAVEFORM_HEIGHT + 2;
    // Only resize up when spectrogram is toggled on, never enforce a minimum
    if (m_canvas->showSpectrogram() && height() < desiredH) {
        resize(width(), desiredH);
    }
}

} // namespace scoretracker
