#pragma once

#include <QMainWindow>
#include <QToolBar>
#include <QAction>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QSplitter>
#include <QDockWidget>
#include <QJsonObject>
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
class YouTubePlayer;
class SyncTimer;
class SyncMode;
class SyncPanel;
class WaveformWidget;
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
    bool loadBeatData(const QString& jsonPath);
    void loadYouTube(const QString& url);
    void loadSources(const QString& jsonPath);
    void setVisibleParts(const QList<int>& partNumbers);
    void startSyncMode();

private slots:
    void togglePlayPause();
    void onSeekSliderMoved(int value);
    void onPositionChanged(double seconds);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void setupToolbar();
    void setupUI();
    void setSidebarVisible(bool visible);
    void repositionSidebar();
    void updateTrackingIcon();
    QString formatTime(double seconds) const;

    void enterSyncMode();
    void exitSyncMode();
    void setupSyncSidebar();
    void repositionSyncSidebar();
    void saveSyncData();

    // Player dispatch helpers (route to active player)
    void playerPlay();
    void playerPause();
    void playerStop();
    void playerSeekTo(double seconds);
    double playerCurrentTime() const;
    double playerDuration() const;
    bool playerIsPlaying() const;

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
    YouTubePlayer* m_youtubePlayer = nullptr;
    QDockWidget* m_videoDock = nullptr;
    QPushButton* m_sourceButton = nullptr;
    QAction* m_sourceButtonAction = nullptr;
    QPushButton* m_speedButton = nullptr;
    bool m_useYouTube = false;
    SyncTimer* m_syncTimer = nullptr;
    SyncMode* m_syncMode = nullptr;
    QPushButton* m_syncModeButton = nullptr;
    QWidget* m_syncSidebarWidget = nullptr;
    QWidget* m_syncSidebarHandle = nullptr;
    SyncPanel* m_syncPanel = nullptr;
    WaveformWidget* m_waveformWidget = nullptr;
    QSplitter* m_centralSplitter = nullptr;
    bool m_savedSidebarVisible = true;
    bool m_savedTrackingOn = false;
    QJsonObject m_savedSyncState;

    // Score
    mu::engraving::MasterScore* m_score = nullptr;
    std::shared_ptr<mu::engraving::rendering::IScoreRenderer> m_renderer;

    QString m_audioFilePath;
    bool m_sliderDragging = false;
    bool m_userForcedAutoScroll = false;
    int m_sidebarWidth = 300;
    bool m_sidebarDragging = false;
    int m_dragStartX = 0;
    int m_dragStartWidth = 0;
};

} // namespace scoretracker
