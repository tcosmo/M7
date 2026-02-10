#pragma once

#include <QMainWindow>
#include <QToolBar>
#include <QAction>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QSplitter>
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
class DisplaySettings;
class TrackingSettings;

class App : public QMainWindow
{
    Q_OBJECT

public:
    explicit App(QWidget* parent = nullptr);
    ~App();

    bool loadScore(const QString& musicXmlPath);
    bool loadAudio(const QString& audioPath);
    void setVisibleParts(const QList<int>& partNumbers);

private slots:
    void togglePlayPause();
    void onSeekSliderMoved(int value);
    void onPositionChanged(double seconds);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void setupToolbar();
    void setupUI();
    void setSidebarVisible(bool visible);
    void repositionSidebar();
    void updateTrackingIcon();
    QString formatTime(double seconds) const;

    // UI
    ScoreWidget* m_scoreWidget = nullptr;
    QWidget* m_sidebarWidget = nullptr;
    QWidget* m_sidebarHandle = nullptr;
    QSplitter* m_sidebarSplitter = nullptr;
    PartPanel* m_partPanel = nullptr;
    DisplaySettings* m_displaySettings = nullptr;
    TrackingSettings* m_trackingSettings = nullptr;
    QToolBar* m_toolbar = nullptr;
    QAction* m_playPauseAction = nullptr;
    QAction* m_stopAction = nullptr;
    QAction* m_trackingAction = nullptr;
    QAction* m_sidebarAction = nullptr;
    QPushButton* m_trackingButton = nullptr;
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
    bool m_userForcedAutoScroll = false;
    int m_sidebarWidth = 300;
    bool m_sidebarDragging = false;
    int m_dragStartX = 0;
    int m_dragStartWidth = 0;
};

} // namespace scoretracker
