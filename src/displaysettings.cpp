#include "displaysettings.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

namespace scoretracker {

DisplaySettings::DisplaySettings(QWidget* parent)
    : QWidget(parent)
{
    setStyleSheet(
        "DisplaySettings { background-color: #2d2d2d; }"
        "QLabel { background-color: transparent; }"
        "QComboBox { background-color: #3d3d3d; border: 1px solid #555; padding: 2px 4px; border-radius: 3px; }"
        "QCheckBox { background-color: transparent; }"
    );
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    layout->addWidget(new QLabel("Layout", this));
    m_layoutModeCombo = new QComboBox(this);
    m_layoutModeCombo->addItem("Page");
    m_layoutModeCombo->addItem("Continuous Horizontal");
    m_layoutModeCombo->addItem("Continuous Vertical");
    layout->addWidget(m_layoutModeCombo);

    m_showTitleCheckbox = new QCheckBox("Show title and composer", this);
    layout->addWidget(m_showTitleCheckbox);

    load();

    connect(m_layoutModeCombo, &QComboBox::currentIndexChanged, this, [this]() {
        save();
        emit settingChanged();
    });

    connect(m_showTitleCheckbox, &QCheckBox::toggled, this, [this]() {
        save();
        emit settingChanged();
    });
}

bool DisplaySettings::showTitleFrame() const
{
    return m_showTitleCheckbox->isChecked();
}

int DisplaySettings::layoutMode() const
{
    return m_layoutModeCombo->currentIndex();
}

void DisplaySettings::load()
{
    QString path = settingsPath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "DisplaySettings: no settings.json found at" << path << "- using defaults";
        m_layoutModeCombo->setCurrentIndex(2); // Continuous Vertical
        m_showTitleCheckbox->setChecked(false);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject obj = doc.object();
    int mode = obj.value("layoutMode").toInt(2); // default Continuous Vertical
    bool show = obj.value("showTitleFrame").toBool(false);
    qDebug() << "DisplaySettings: loaded settings.json from" << path
             << "- layoutMode:" << mode << "showTitleFrame:" << show;
    m_layoutModeCombo->setCurrentIndex(mode);
    m_showTitleCheckbox->setChecked(show);
}

void DisplaySettings::save()
{
    QJsonObject obj;
    obj["layoutMode"] = m_layoutModeCombo->currentIndex();
    obj["showTitleFrame"] = m_showTitleCheckbox->isChecked();

    QString path = settingsPath();
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        qDebug() << "DisplaySettings: saved settings.json to" << path
                 << "- layoutMode:" << m_layoutModeCombo->currentIndex()
                 << "showTitleFrame:" << m_showTitleCheckbox->isChecked();
    } else {
        qWarning() << "DisplaySettings: failed to save settings.json to" << path;
    }
}

QString DisplaySettings::settingsPath() const
{
    return QCoreApplication::applicationDirPath() + "/settings.json";
}

} // namespace scoretracker
