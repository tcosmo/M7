#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QPainter>
#include <QTimer>

namespace scoretracker {

// iOS-style toggle switch with sliding circle knob
class ToggleSwitch : public QWidget
{
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget* parent = nullptr);
    bool isChecked() const { return m_checked; }
    void setChecked(bool on);
    void setEnabled(bool on) { m_enabled = on; update(); }
signals:
    void toggled(bool checked);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
private:
    bool m_checked = true;
    bool m_enabled = true;
};

class GameBar : public QWidget
{
    Q_OBJECT
public:
    explicit GameBar(QWidget* parent = nullptr);

    bool isGameMode() const { return m_gameMode; }
    void setGameModeAvailable(bool available);
    void reset();

    void recordHit(double pixelDistance);
    double accuracy() const;

signals:
    void gameModeChanged(bool gameMode);

protected:
    void resizeEvent(QResizeEvent*) override;

private:
    void updateDisplay();

    bool m_gameMode = true;
    bool m_available = false;
    ToggleSwitch* m_toggle = nullptr;
    QLabel* m_performLabel = nullptr;
    QLabel* m_gameLabel = nullptr;
    QWidget* m_rightContainer = nullptr;
    QLabel* m_feedbackLabel = nullptr;
    QLabel* m_accuracyLabel = nullptr;
    QTimer* m_fadeTimer = nullptr;

    int m_hits = 0;
    int m_totalTaps = 0;
    int m_hitStreak = 0;
};

} // namespace scoretracker
