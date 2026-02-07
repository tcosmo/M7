#pragma once

#include <QDockWidget>
#include <QCheckBox>

namespace scoretracker {

class DisplaySettings : public QDockWidget
{
    Q_OBJECT

public:
    explicit DisplaySettings(QWidget* parent = nullptr);

    bool showTitleFrame() const;

signals:
    void settingChanged();

private:
    void load();
    void save();
    QString settingsPath() const;

    QCheckBox* m_showTitleCheckbox = nullptr;
};

} // namespace scoretracker
