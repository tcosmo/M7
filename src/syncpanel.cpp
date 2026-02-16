#include "syncpanel.h"
#include "syncmode.h"
#include "theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <algorithm>

namespace scoretracker {

SyncPanel::SyncPanel(QWidget* parent)
    : QWidget(parent)
{
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Theme::panelBg());
    setPalette(pal);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 4, 12, 10);
    layout->setSpacing(8);

    m_instructionLabel = new QLabel("Play audio and press 'O' to tap beats.\nDouble-click a sync point to allow overwrite.");
    m_instructionLabel->setWordWrap(true);
    m_instructionLabel->setStyleSheet(QString("color: %1; font-size: 11px;")
        .arg(Theme::textSecondary().name()));
    layout->addWidget(m_instructionLabel);

    m_statusLabel = new QLabel("Synced: 0 / 0 beats");
    m_statusLabel->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: bold;")
        .arg(Theme::textPrimary().name()));
    layout->addWidget(m_statusLabel);

    m_beatInfoLabel = new QLabel("");
    m_beatInfoLabel->setWordWrap(true);
    m_beatInfoLabel->setStyleSheet(QString(
        "color: %1; font-size: 11px; background: %2; border-radius: 4px; padding: 6px;")
        .arg(Theme::textPrimary().name(),
             Theme::panelBg().lighter(130).name()));
    m_beatInfoLabel->setVisible(false);
    layout->addWidget(m_beatInfoLabel);

    // Time editor row
    m_timeEditWidget = new QWidget;
    auto* timeRow = new QHBoxLayout(m_timeEditWidget);
    timeRow->setContentsMargins(0, 0, 0, 0);
    timeRow->setSpacing(6);
    auto* timeLabel = new QLabel("Time (s):");
    timeLabel->setStyleSheet(QString("color: %1; font-size: 11px;")
        .arg(Theme::textSecondary().name()));
    m_timeSpinBox = new QDoubleSpinBox;
    m_timeSpinBox->setDecimals(3);
    m_timeSpinBox->setRange(0.0, 99999.0);
    m_timeSpinBox->setSingleStep(0.01);
    m_timeSpinBox->setFocusPolicy(Qt::ClickFocus);
    m_timeSpinBox->setKeyboardTracking(false);
    timeRow->addWidget(timeLabel);
    timeRow->addWidget(m_timeSpinBox);
    m_timeEditWidget->setVisible(false);
    layout->addWidget(m_timeEditWidget);

    connect(m_timeSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double val) {
        if (m_selectedBeatIndex >= 0) {
            emit beatTimeChanged(m_selectedBeatIndex, val);
        }
    });

    // Waveform zoom controls
    auto* zoomRow = new QWidget;
    auto* zoomLayout = new QHBoxLayout(zoomRow);
    zoomLayout->setContentsMargins(0, 0, 0, 0);
    zoomLayout->setSpacing(4);

    auto* zoomTitle = new QLabel("Waveform Zoom:");
    zoomTitle->setStyleSheet(QString("color: %1; font-size: 11px;")
        .arg(Theme::textSecondary().name()));
    zoomLayout->addWidget(zoomTitle);

    auto* zoomOutBtn = new QPushButton("\u2212"); // minus sign
    zoomOutBtn->setFixedSize(24, 24);
    zoomOutBtn->setCursor(Qt::PointingHandCursor);
    zoomOutBtn->setFocusPolicy(Qt::NoFocus);
    zoomLayout->addWidget(zoomOutBtn);

    m_zoomLabel = new QLabel("1.0x");
    m_zoomLabel->setFixedWidth(40);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    m_zoomLabel->setStyleSheet(QString("color: %1; font-size: 11px;")
        .arg(Theme::textPrimary().name()));
    zoomLayout->addWidget(m_zoomLabel);

    auto* zoomInBtn = new QPushButton("+");
    zoomInBtn->setFixedSize(24, 24);
    zoomInBtn->setCursor(Qt::PointingHandCursor);
    zoomInBtn->setFocusPolicy(Qt::NoFocus);
    zoomLayout->addWidget(zoomInBtn);

    zoomLayout->addStretch();
    layout->addWidget(zoomRow);

    connect(zoomInBtn, &QPushButton::clicked, this, [this]() {
        m_waveformZoom = std::min(m_waveformZoom * 1.5, 100.0);
        m_zoomLabel->setText(QString("%1x").arg(m_waveformZoom, 0, 'f', 1));
        emit waveformZoomRequested(m_waveformZoom);
    });
    connect(zoomOutBtn, &QPushButton::clicked, this, [this]() {
        m_waveformZoom = std::max(m_waveformZoom / 1.5, 1.0);
        m_zoomLabel->setText(QString("%1x").arg(m_waveformZoom, 0, 'f', 1));
        emit waveformZoomRequested(m_waveformZoom);
    });

    // Spectrogram toggle
    auto* spectrogramCheck = new QCheckBox("Show Spectrogram");
    spectrogramCheck->setFocusPolicy(Qt::NoFocus);
    spectrogramCheck->setStyleSheet(QString("color: %1; font-size: 11px;")
        .arg(Theme::textSecondary().name()));
    layout->addWidget(spectrogramCheck);
    connect(spectrogramCheck, &QCheckBox::toggled, this, &SyncPanel::spectrogramToggled);

    layout->addSpacing(4);

    m_exportButton = new QPushButton("Export beatdata.json");
    m_exportButton->setCursor(Qt::PointingHandCursor);
    m_exportButton->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(m_exportButton);

    m_clearButton = new QPushButton("Clear All");
    m_clearButton->setCursor(Qt::PointingHandCursor);
    m_clearButton->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(m_clearButton);

    layout->addStretch();

    connect(m_exportButton, &QPushButton::clicked, this, &SyncPanel::exportRequested);
    connect(m_clearButton, &QPushButton::clicked, this, &SyncPanel::clearRequested);
}

void SyncPanel::setSyncMode(SyncMode* syncMode)
{
    m_syncMode = syncMode;
    updateStatus();
}

void SyncPanel::updateStatus()
{
    if (!m_syncMode) {
        m_statusLabel->setText("Synced: 0 / 0 beats");
        return;
    }
    m_statusLabel->setText(QString("Synced: %1 / %2 beats")
        .arg(m_syncMode->syncedCount())
        .arg(m_syncMode->totalBeats()));
}

void SyncPanel::showBeatInfo(int beatIndex)
{
    m_selectedBeatIndex = beatIndex;

    if (!m_syncMode || beatIndex < 0 || beatIndex >= m_syncMode->totalBeats()) {
        m_beatInfoLabel->setVisible(false);
        m_timeEditWidget->setVisible(false);
        return;
    }

    const auto& beat = m_syncMode->beats()[beatIndex];
    QString info = QString("Measure %1, Beat %2")
        .arg(beat.measureIndex + 1)
        .arg(beat.beatInMeasure + 1);

    if (beat.synced) {
        info += QString("  \u2022  %1s").arg(beat.effectiveTime(), 0, 'f', 3);
    } else {
        info += QString("  \u2022  not synced");
    }

    m_beatInfoLabel->setText(info);
    m_beatInfoLabel->setVisible(true);

    // Show time editor for synced beats
    if (beat.synced) {
        m_timeSpinBox->blockSignals(true);
        m_timeSpinBox->setValue(beat.effectiveTime());
        m_timeSpinBox->blockSignals(false);
        m_timeEditWidget->setVisible(true);
    } else {
        m_timeEditWidget->setVisible(false);
    }
}

void SyncPanel::setWaveformZoom(double zoom)
{
    m_waveformZoom = zoom;
    if (m_zoomLabel) {
        m_zoomLabel->setText(QString("%1x").arg(zoom, 0, 'f', 1));
    }
}

} // namespace scoretracker
