#include "trackingsettings.h"
#include "theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

namespace scoretracker {

TrackingSettings::TrackingSettings(QWidget* parent)
    : QWidget(parent)
{
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Theme::panelBg());
    pal.setColor(QPalette::Base, Theme::panelBg());
    setPalette(pal);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 4, 12, 10);

    auto* checkboxRow = new QHBoxLayout();
    m_autoScrollCheckbox = new QCheckBox("Auto-scroll", this);
    checkboxRow->addWidget(m_autoScrollCheckbox);
    m_showTriggerCheckbox = new QCheckBox("Show trigger point", this);
    checkboxRow->addWidget(m_showTriggerCheckbox);
    checkboxRow->addStretch();
    layout->addLayout(checkboxRow);

    layout->addWidget(new QLabel("Trigger point", this));
    m_triggerPointSpin = new QSpinBox(this);
    m_triggerPointSpin->setRange(5, 95);
    m_triggerPointSpin->setSuffix("%");
    layout->addWidget(m_triggerPointSpin);

    layout->addWidget(new QLabel("Scroll amount", this));
    m_scrollAmountSpin = new QSpinBox(this);
    m_scrollAmountSpin->setRange(10, 100);
    m_scrollAmountSpin->setSuffix("%");
    layout->addWidget(m_scrollAmountSpin);

    load();

    auto updateEnabled = [this]() {
        bool on = m_autoScrollCheckbox->isChecked();
        m_triggerPointSpin->setEnabled(on);
        m_scrollAmountSpin->setEnabled(on);
    };
    updateEnabled();

    connect(m_autoScrollCheckbox, &QCheckBox::toggled, this, [this, updateEnabled]() {
        updateEnabled();
        save();
        emit settingChanged();
    });

    connect(m_showTriggerCheckbox, &QCheckBox::toggled, this, [this]() {
        save();
        emit settingChanged();
    });

    connect(m_triggerPointSpin, &QSpinBox::valueChanged, this, [this]() {
        save();
        emit settingChanged();
    });

    connect(m_scrollAmountSpin, &QSpinBox::valueChanged, this, [this]() {
        save();
        emit settingChanged();
    });
}

bool TrackingSettings::autoScrollEnabled() const
{
    return m_autoScrollCheckbox->isChecked();
}

bool TrackingSettings::showTriggerLine() const
{
    return m_showTriggerCheckbox->isChecked();
}

int TrackingSettings::triggerPoint() const
{
    return m_triggerPointSpin->value();
}

int TrackingSettings::scrollAmount() const
{
    return m_scrollAmountSpin->value();
}

void TrackingSettings::load()
{
    QString path = settingsPath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_autoScrollCheckbox->setChecked(true);
        m_showTriggerCheckbox->setChecked(false);
        m_triggerPointSpin->setValue(60);
        m_scrollAmountSpin->setValue(100);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject root = doc.object();
    QJsonObject obj = root.contains("tracking") ? root["tracking"].toObject() : QJsonObject();

    m_autoScrollCheckbox->setChecked(obj.value("autoScroll").toBool(true));
    m_showTriggerCheckbox->setChecked(obj.value("showTriggerLine").toBool(false));
    m_triggerPointSpin->setValue(obj.value("triggerPoint").toInt(60));
    m_scrollAmountSpin->setValue(obj.value("scrollAmount").toInt(100));
}

void TrackingSettings::save()
{
    QString path = settingsPath();

    QJsonObject root;
    QFile readFile(path);
    if (readFile.open(QIODevice::ReadOnly)) {
        root = QJsonDocument::fromJson(readFile.readAll()).object();
        readFile.close();
    }

    QJsonObject trackingObj;
    trackingObj["autoScroll"] = m_autoScrollCheckbox->isChecked();
    trackingObj["showTriggerLine"] = m_showTriggerCheckbox->isChecked();
    trackingObj["triggerPoint"] = m_triggerPointSpin->value();
    trackingObj["scrollAmount"] = m_scrollAmountSpin->value();
    root["tracking"] = trackingObj;

    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
}

void TrackingSettings::applyTheme()
{
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Theme::panelBg());
    pal.setColor(QPalette::Base, Theme::panelBg());
    setPalette(pal);
}

QString TrackingSettings::settingsPath() const
{
    return QCoreApplication::applicationDirPath() + "/settings.json";
}

} // namespace scoretracker
