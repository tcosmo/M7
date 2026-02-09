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

    m_trackingCheckbox = new QCheckBox("Tracking", this);
    layout->addWidget(m_trackingCheckbox);

    m_autoScrollCheckbox = new QCheckBox("Auto-scroll", this);
    layout->addWidget(m_autoScrollCheckbox);
    m_showTriggerCheckbox = new QCheckBox("Show trigger line", this);
    layout->addWidget(m_showTriggerCheckbox);

    layout->addWidget(new QLabel("Trigger line", this));
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
        bool tracking = m_trackingCheckbox->isChecked();
        bool autoScroll = m_autoScrollCheckbox->isChecked();
        m_autoScrollCheckbox->setEnabled(tracking);
        m_showTriggerCheckbox->setEnabled(tracking && autoScroll);
        m_triggerPointSpin->setEnabled(tracking && autoScroll);
        m_scrollAmountSpin->setEnabled(tracking && autoScroll);
    };
    updateEnabled();

    connect(m_trackingCheckbox, &QCheckBox::toggled, this, [this, updateEnabled](bool on) {
        updateEnabled();
        emit trackingToggled(on);
    });

    connect(m_autoScrollCheckbox, &QCheckBox::toggled, this, [this, updateEnabled]() {
        updateEnabled();
        save();
        emit settingChanged();
    });

    connect(m_showTriggerCheckbox, &QCheckBox::toggled, this, [this]() {
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

bool TrackingSettings::trackingEnabled() const
{
    return m_trackingCheckbox->isChecked();
}

void TrackingSettings::setTrackingEnabled(bool on)
{
    m_trackingCheckbox->setChecked(on);
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
        m_trackingCheckbox->setChecked(true);
        m_autoScrollCheckbox->setChecked(true);
        m_showTriggerCheckbox->setChecked(false);
        m_triggerPointSpin->setValue(60);
        m_scrollAmountSpin->setValue(88);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject root = doc.object();
    QJsonObject obj = root.contains("tracking") ? root["tracking"].toObject() : QJsonObject();

    m_trackingCheckbox->setChecked(true);
    m_autoScrollCheckbox->setChecked(obj.value("autoScroll").toBool(true));
    m_showTriggerCheckbox->setChecked(false);
    m_triggerPointSpin->setValue(obj.value("triggerPoint").toInt(60));
    m_scrollAmountSpin->setValue(obj.value("scrollAmount").toInt(88));
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
