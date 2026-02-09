#pragma once

#include <QWidget>
#include <QCheckBox>
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
    int triggerPoint() const;   // 5–95 %
    int scrollAmount() const;   // 10–100 %
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
    QSpinBox*  m_triggerPointSpin   = nullptr;
    QSpinBox*  m_scrollAmountSpin   = nullptr;
};

} // namespace scoretracker
