#pragma once

#include <QWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>

namespace scoretracker {

class TrackingSettings : public QWidget
{
    Q_OBJECT

public:
    explicit TrackingSettings(QWidget* parent = nullptr);

    bool trackingEnabled() const;
    void setTrackingEnabled(bool on);
    bool autoScrollEnabled() const;
    bool showTriggerLine() const;
    int triggerLine() const;    // 5–95 %
    int scrollAmount() const;   // 10–100 %
    int cursorAnchor() const;   // 0=Top, 1=Center, 2=Bottom
    void applyTheme();

signals:
    void trackingToggled(bool on);
    void settingChanged();

private:
    void load();
    void save();
    QString settingsPath() const;

    QCheckBox* m_trackingCheckbox = nullptr;
    QCheckBox* m_autoScrollCheckbox = nullptr;
    QCheckBox* m_showTriggerCheckbox = nullptr;
    QSpinBox*  m_triggerLineSpin     = nullptr;
    QSpinBox*  m_scrollAmountSpin   = nullptr;
    QComboBox* m_cursorAnchorCombo  = nullptr;
};

} // namespace scoretracker
