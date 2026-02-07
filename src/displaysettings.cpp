#include "displaysettings.h"

#include <QVBoxLayout>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

namespace scoretracker {

DisplaySettings::DisplaySettings(QWidget* parent)
    : QDockWidget("Score Display", parent)
{
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);

    m_showTitleCheckbox = new QCheckBox("Show title and composer", container);
    layout->addWidget(m_showTitleCheckbox);
    layout->addStretch();

    setWidget(container);

    load();

    connect(m_showTitleCheckbox, &QCheckBox::toggled, this, [this]() {
        save();
        emit settingChanged();
    });
}

bool DisplaySettings::showTitleFrame() const
{
    return m_showTitleCheckbox->isChecked();
}

void DisplaySettings::load()
{
    QString path = settingsPath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "DisplaySettings: no settings.json found at" << path << "- using defaults";
        m_showTitleCheckbox->setChecked(false);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    bool show = doc.object().value("showTitleFrame").toBool(false);
    qDebug() << "DisplaySettings: loaded settings.json from" << path << "- showTitleFrame:" << show;
    m_showTitleCheckbox->setChecked(show);
}

void DisplaySettings::save()
{
    QJsonObject obj;
    obj["showTitleFrame"] = m_showTitleCheckbox->isChecked();

    QString path = settingsPath();
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        qDebug() << "DisplaySettings: saved settings.json to" << path << "- showTitleFrame:" << m_showTitleCheckbox->isChecked();
    } else {
        qWarning() << "DisplaySettings: failed to save settings.json to" << path;
    }
}

QString DisplaySettings::settingsPath() const
{
    return QCoreApplication::applicationDirPath() + "/settings.json";
}

} // namespace scoretracker
