#include "gamebar.h"
#include "theme.h"
#include <QMouseEvent>

namespace scoretracker {

// --- ToggleSwitch ---

ToggleSwitch::ToggleSwitch(QWidget* parent) : QWidget(parent)
{
    setFixedSize(44, 22);
    setCursor(Qt::PointingHandCursor);
}

void ToggleSwitch::setChecked(bool on)
{
    if (m_checked == on) return;
    m_checked = on;
    update();
    emit toggled(on);
}

void ToggleSwitch::mousePressEvent(QMouseEvent*)
{
    if (!m_enabled) return;
    setChecked(!m_checked);
}

void ToggleSwitch::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QColor trackColor = m_enabled ? Theme::accent() : Theme::inputBg();

    p.setPen(Qt::NoPen);
    p.setBrush(trackColor);
    p.drawRoundedRect(rect(), 11, 11);

    double knobX = m_checked ? width() - 20.0 : 2.0;
    p.setBrush(Qt::white);
    p.drawEllipse(QRectF(knobX, 2, 18, 18));
}

// --- GameBar ---

GameBar::GameBar(QWidget* parent)
    : QWidget(parent)
    , m_gameMode(true)
{
    setFixedHeight(32);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Theme::panelBg());
    setPalette(pal);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 2, 12, 2);
    layout->setSpacing(8);

    // Left: toggle switch
    m_performLabel = new QLabel("Performance", this);
    layout->addWidget(m_performLabel);

    m_toggle = new ToggleSwitch(this);
    connect(m_toggle, &ToggleSwitch::toggled, this, [this](bool checked) {
        m_gameMode = checked;
        reset();
        updateDisplay();
        emit gameModeChanged(m_gameMode);
    });
    layout->addWidget(m_toggle);

    m_gameLabel = new QLabel("Game", this);
    layout->addWidget(m_gameLabel);

    layout->addStretch();

    // Center: hit/miss feedback + accuracy
    m_feedbackLabel = new QLabel(this);
    m_feedbackLabel->setAlignment(Qt::AlignCenter);
    m_feedbackLabel->setFixedWidth(130);
    m_feedbackLabel->setStyleSheet("font-size: 13px; font-weight: bold;");
    layout->addWidget(m_feedbackLabel);

    m_accuracyLabel = new QLabel(this);
    m_accuracyLabel->setAlignment(Qt::AlignCenter);
    m_accuracyLabel->setFixedWidth(130);
    m_accuracyLabel->setStyleSheet(QString("color: %1; font-size: 13px; font-weight: bold;").arg(Theme::textPrimary().name()));
    layout->addWidget(m_accuracyLabel);

    layout->addStretch();

    // Fade timer: clears feedback after a short delay so it doesn't go stale
    m_fadeTimer = new QTimer(this);
    m_fadeTimer->setSingleShot(true);
    m_fadeTimer->setInterval(600);
    connect(m_fadeTimer, &QTimer::timeout, this, [this]() {
        m_feedbackLabel->clear();
    });

    updateDisplay();
}

void GameBar::setGameModeAvailable(bool available)
{
    m_available = available;
    m_toggle->setEnabled(available);
    if (!available && m_gameMode) {
        m_toggle->setChecked(false);
        m_gameMode = false;
        updateDisplay();
        emit gameModeChanged(false);
    }
}

void GameBar::reset()
{
    m_hits = 0;
    m_totalTaps = 0;
    m_hitStreak = 0;
    m_feedbackLabel->clear();
    m_fadeTimer->stop();
    updateDisplay();
}

void GameBar::recordHit(double pixelDistance)
{
    if (!m_gameMode) return;

    m_totalTaps++;

    if (pixelDistance <= 12.0) {
        m_hits++;
        m_hitStreak++;
        QString text;
        QString color = "#4CAF50";
        if (m_hitStreak >= 20) {
            text = QString("Virtuoso! x%1").arg(m_hitStreak);
        } else if (m_hitStreak >= 10) {
            text = QString("On Fire! x%1").arg(m_hitStreak);
        } else if (m_hitStreak >= 5) {
            text = QString("Combo! x%1").arg(m_hitStreak);
        } else {
            text = "Hit!";
        }
        m_feedbackLabel->setText(text);
        m_feedbackLabel->setStyleSheet(QString("color: %1; font-size: 13px; font-weight: bold;").arg(color));
    } else {
        m_hitStreak = 0;
        m_feedbackLabel->setText("Miss!");
        m_feedbackLabel->setStyleSheet("color: #F44336; font-size: 13px; font-weight: bold;");
    }

    // Auto-clear after a moment so feedback doesn't go stale
    m_fadeTimer->start();

    updateDisplay();
}

double GameBar::accuracy() const
{
    if (m_totalTaps == 0) return -1.0;
    return (static_cast<double>(m_hits) / m_totalTaps) * 100.0;
}

void GameBar::updateDisplay()
{
    if (m_gameMode) {
        m_gameLabel->setStyleSheet(QString("color: %1; font-size: 11px; font-weight: bold;").arg(Theme::textPrimary().name()));
        m_performLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(Theme::textSecondary().name()));
        m_feedbackLabel->show();
        m_accuracyLabel->show();
        if (m_totalTaps > 0)
            m_accuracyLabel->setText(QString("Accuracy: %1%").arg(accuracy(), 0, 'f', 0));
        else
            m_accuracyLabel->setText("Accuracy: --");
    } else {
        m_performLabel->setStyleSheet(QString("color: %1; font-size: 11px; font-weight: bold;").arg(Theme::textPrimary().name()));
        m_gameLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(Theme::textSecondary().name()));
        m_feedbackLabel->hide();
        m_accuracyLabel->hide();
    }
}

} // namespace scoretracker
