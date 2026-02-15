#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QDoubleSpinBox>

namespace scoretracker {

class SyncMode;

class SyncPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SyncPanel(QWidget* parent = nullptr);

    void setSyncMode(SyncMode* syncMode);
    void updateStatus();
    void showBeatInfo(int beatIndex);

    void setWaveformZoom(double zoom);

signals:
    void exportRequested();
    void clearRequested();
    void beatTimeChanged(int beatIndex, double newTime);
    void waveformZoomRequested(double zoom);
    void spectrogramToggled(bool show);

private:
    SyncMode* m_syncMode = nullptr;
    QLabel* m_instructionLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_beatInfoLabel = nullptr;
    QWidget* m_timeEditWidget = nullptr;
    QDoubleSpinBox* m_timeSpinBox = nullptr;
    QLabel* m_zoomLabel = nullptr;
    QPushButton* m_exportButton = nullptr;
    QPushButton* m_clearButton = nullptr;
    int m_selectedBeatIndex = -1;
    double m_waveformZoom = 1.0;
};

} // namespace scoretracker
