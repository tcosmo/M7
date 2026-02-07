#pragma once

#include <QWidget>
#include <QCheckBox>
#include <QComboBox>

namespace scoretracker {

class DisplaySettings : public QWidget
{
    Q_OBJECT

public:
    explicit DisplaySettings(QWidget* parent = nullptr);

    bool showTitleFrame() const;
    int layoutMode() const; // 0=Page, 1=Continuous Horizontal, 2=Continuous Vertical

signals:
    void settingChanged();

private:
    void load();
    void save();
    QString settingsPath() const;

    QComboBox* m_layoutModeCombo = nullptr;
    QCheckBox* m_showTitleCheckbox = nullptr;
};

} // namespace scoretracker
