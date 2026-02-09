#include "displaysettings.h"
#include "theme.h"

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
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Theme::panelBg());
    pal.setColor(QPalette::Base, Theme::panelBg());
    setPalette(pal);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 4, 12, 10);

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
    QJsonObject root = doc.object();

    // Support both old flat format and new nested "display" key
    QJsonObject obj = root.contains("display") ? root["display"].toObject() : root;

    int mode = obj.value("layoutMode").toInt(2); // default Continuous Vertical
    bool show = obj.value("showTitleFrame").toBool(false);
    qDebug() << "DisplaySettings: loaded settings.json from" << path
             << "- layoutMode:" << mode << "showTitleFrame:" << show;
    m_layoutModeCombo->setCurrentIndex(mode);
    m_showTitleCheckbox->setChecked(show);
}

void DisplaySettings::save()
{
    QString path = settingsPath();

    // Read existing file to preserve other sections
    QJsonObject root;
    QFile readFile(path);
    if (readFile.open(QIODevice::ReadOnly)) {
        root = QJsonDocument::fromJson(readFile.readAll()).object();
        readFile.close();
    }

    // Remove old flat keys if migrating
    root.remove("layoutMode");
    root.remove("showTitleFrame");

    QJsonObject displayObj;
    displayObj["layoutMode"] = m_layoutModeCombo->currentIndex();
    displayObj["showTitleFrame"] = m_showTitleCheckbox->isChecked();
    root["display"] = displayObj;

    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        qDebug() << "DisplaySettings: saved settings.json to" << path
                 << "- layoutMode:" << m_layoutModeCombo->currentIndex()
                 << "showTitleFrame:" << m_showTitleCheckbox->isChecked();
    } else {
        qWarning() << "DisplaySettings: failed to save settings.json to" << path;
    }
}

void DisplaySettings::applyTheme()
{
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Theme::panelBg());
    pal.setColor(QPalette::Base, Theme::panelBg());
    setPalette(pal);
}

QString DisplaySettings::settingsPath() const
{
    return QCoreApplication::applicationDirPath() + "/settings.json";
}

} // namespace scoretracker
