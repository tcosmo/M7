#pragma once

#include <QMainWindow>
#include <QToolBar>
#include <QAction>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QVBoxLayout>
#include <QSplitter>
#include <QStackedWidget>
#include <QJsonObject>
#include <QStringList>

#include <memory>
#include <vector>

#include "worldmodel.h"

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
class PlayAlongSynth;
class WorldSidebar;
class LevelBrowser;

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
    void startPlayMode();
    void selectFileSource();
    void loadWorlds(const QString& worldsDir);
    void showWorldBrowser();

private slots:
    void togglePlayPause();
    void onSeekSliderMoved(int value);
    void onPositionChanged(double seconds);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void setupToolbar();
    void setupUI();
    void setSidebarVisible(bool visible);
    void updateTrackingIcon();
    QString formatTime(double seconds) const;

    void enterPlayMode();
    void exitPlayMode();
    void loadLevel(int worldIndex, int sectionIndex, int levelIndex);
    void setupInstrumentPanelForVoices(int voiceCount);
    void startRecordTracking();
    void stopRecordTracking();
    void finalizeRecordedTracking();
    void saveRecordedTracking();

    void enterSyncMode();
    void exitSyncMode();
    void setupSyncSidebar();
    void repositionSyncSidebar();
    void saveSyncData();
    void updateSyncTimerFromSyncMode();

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
    QSplitter* m_mainSplitter = nullptr;
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
    QAction* m_instrumentAction = nullptr;
    QWidget* m_instrumentPanel = nullptr;
    QVBoxLayout* m_instrumentPanelLayout = nullptr;
    QComboBox* m_instrumentCombo = nullptr;     // single-voice instrument combo
    QComboBox* m_soundfontCombo = nullptr;      // single-voice soundfont combo
    QWidget* m_singleVoiceRow = nullptr;        // row for single-voice mode
    QList<QComboBox*> m_voiceSfCombos;          // per-voice soundfont combos
    QList<QComboBox*> m_voiceInstrCombos;       // per-voice instrument combos
    QList<QWidget*> m_voiceRows;                // per-voice row widgets
    QDoubleSpinBox* m_transposeSpin = nullptr;
    QSlider* m_instrumentVolSlider = nullptr;
    QStringList m_soundfontPaths;  // absolute paths to available soundfonts
    QString m_soundfontsDir;       // resources/sounds/ directory

    // Backend
    AudioPlayer* m_audioPlayer = nullptr;
    YouTubePlayer* m_youtubePlayer = nullptr;
    QPushButton* m_speedButton = nullptr;
    bool m_useYouTube = false;
    QStringList m_sourceYouTubeUrls;
    QStringList m_sourceLabels;
    QList<double> m_sourceTunings; // per-interpretation pitch offset in semitones (fractional OK)
    QList<int> m_sourceInstrumentVols; // per-interpretation instrument volume (0–150), -1 = use default
    QList<int> m_sourceVolumes;        // per-interpretation master volume (0–100), -1 = use default
    QStringList m_sourceBeatsFiles;    // per-interpretation beats JSON path, empty = no beats
    QList<double> m_sourceStartTimes;  // per-interpretation start offset in seconds, 0 = from beginning
    QList<double> m_sourceEndTimes;    // per-interpretation end time in seconds, 0 = no limit
    int m_activeInterpretation = 0;
    double m_interpStart = 0;          // active interpretation start offset
    double m_interpEnd = 0;            // active interpretation end time (0 = no limit)
    QString m_sourceAudioFile;
    SyncTimer* m_syncTimer = nullptr;
    SyncMode* m_syncMode = nullptr;
    bool m_playModeActive = false;
    QPushButton* m_playModeButton = nullptr;
    // Sync mode archived — button removed from toolbar but code retained
    // QPushButton* m_syncModeButton = nullptr;
    QWidget* m_syncSidebarWidget = nullptr;
    QWidget* m_syncSidebarHandle = nullptr;
    SyncPanel* m_syncPanel = nullptr;
    WaveformWidget* m_waveformWidget = nullptr;
    QSplitter* m_centralSplitter = nullptr;
    bool m_savedSidebarVisible = true;
    bool m_savedTrackingOn = false;
    bool m_savedAutoScroll = true;
    QJsonObject m_savedSyncState;
    std::vector<double> m_savedBeatTimes;
    std::vector<int> m_savedBeatTicks;
    std::vector<double> m_savedMeasureStarts;
    int m_savedBeatsPerMeasure = 3;

    // Score
    mu::engraving::MasterScore* m_score = nullptr;
    std::shared_ptr<mu::engraving::rendering::IScoreRenderer> m_renderer;

    PlayAlongSynth* m_playAlongSynth = nullptr;
    int m_keysHeld = 0;
    // Multi-voice support
    bool m_multiVoice = false;
    QList<int> m_voiceKeysHeld;     // per-voice key counters
    QList<QString> m_voiceKeyZones; // "left", "right", "all" per voice

    QString m_audioFilePath;
    QString m_beatDataPath;
    QString m_sourcesPath;
    bool m_sliderDragging = false;
    int m_sidebarWidth = 300;

    // Record tracking
    bool m_recordTrackingActive = false;
    bool m_beatDataFromRecording = false;
    struct RecordedNote { double wallTime; int tick; };
    std::vector<RecordedNote> m_recordedNotes;

    // Worlds
    WorldSidebar* m_worldSidebar = nullptr;
    LevelBrowser* m_levelBrowser = nullptr;
    QStackedWidget* m_centralStack = nullptr;  // switches between level browser and score view
    QList<World> m_worlds;
    int m_currentWorldIndex = -1;
    // Active level (for Resume support)
    int m_activeWorldIndex = -1;
    int m_activeSectionIndex = -1;
    int m_activeLevelIndex = -1;

    // Video expand
    bool m_videoExpanded = false;
    QWidget* m_expandedVideoContainer = nullptr;
    QPushButton* m_videoExpandButton = nullptr;
    void toggleVideoExpand();
};

} // namespace scoretracker
