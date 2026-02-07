#pragma once

#include <QMainWindow>
#include <QToolBar>
#include <QAction>
#include <QSlider>
#include <QLabel>
#include <memory>

namespace mu::engraving {
class MasterScore;
namespace rendering {
class IScoreRenderer;
}
}

namespace scoretracker {

class ScoreWidget;
class AudioPlayer;
class SyncTimer;
class PartPanel;

class App : public QMainWindow
{
    Q_OBJECT

public:
    explicit App(QWidget* parent = nullptr);
    ~App();

    bool loadScore(const QString& musicXmlPath);
    bool loadAudio(const QString& audioPath);

private slots:
    void togglePlayPause();
    void onSeekSliderMoved(int value);
    void onPositionChanged(double seconds);

private:
    void setupToolbar();
    void setupUI();
    QString formatTime(double seconds) const;

    // UI
    ScoreWidget* m_scoreWidget = nullptr;
    PartPanel* m_partPanel = nullptr;
    QToolBar* m_toolbar = nullptr;
    QAction* m_playPauseAction = nullptr;
    QAction* m_stopAction = nullptr;
    QAction* m_trackingAction = nullptr;
    QSlider* m_seekSlider = nullptr;
    QLabel* m_timeLabel = nullptr;
    QLabel* m_zoomLabel = nullptr;

    // Backend
    AudioPlayer* m_audioPlayer = nullptr;
    SyncTimer* m_syncTimer = nullptr;

    // Score
    mu::engraving::MasterScore* m_score = nullptr;
    std::shared_ptr<mu::engraving::rendering::IScoreRenderer> m_renderer;

    bool m_sliderDragging = false;
};

} // namespace scoretracker
