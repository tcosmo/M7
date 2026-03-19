#include "app.h"
#include "scorewidget.h"
#include "audioplayer.h"
#include "youtubeplayer.h"
#include "synctimer.h"
#ifdef USE_MUSESCORE
#include "syncmode.h"
#include "syncpanel.h"
#include "waveformwidget.h"
#endif
#include "partpanel.h"
#include "displaysettings.h"
#include "trackingsettings.h"
#include "playalongsynth.h"
#include "collapsiblesection.h"
#include "worldbrowser.h"
#include "theme.h"
#include "engine/ScoreEngine.h"
#ifdef USE_MUSESCORE
#include "engine/MuseScoreEngine.h"
#endif
#include "engine/VerovioEngine.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMenu>
#include <QRegularExpression>

#ifdef USE_MUSESCORE
#include "modularity/ioc.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/part.h"
#include "engraving/dom/instrument.h"
#include "engraving/compat/scoreaccess.h"
#include "engraving/rendering/iscorerenderer.h"
#include "engraving/infrastructure/localfileinfoprovider.h"
#include "engraving/style/defaultstyle.h"
#include "engraving/style/styledef.h"
#include "engraving/types/types.h"
#include "engraving/types/fraction.h"
#include "engraving/dom/tempo.h"
#include "engraving/rendering/layoutoptions.h"
#include "importexport/musicxml/internal/import/importmusicxml.h"
#endif // USE_MUSESCORE

#include <QDebug>
#include <QPushButton>
#include <QScrollBar>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QSplitter>
#include <numeric>

#ifdef USE_MUSESCORE
using namespace mu::engraving;
using namespace mu::engraving::rendering;
#endif

namespace scoretracker {

#ifdef USE_MUSESCORE
static LayoutMode comboIndexToLayoutMode(int index)
{
    switch (index) {
    case 0: return LayoutMode::PAGE;
    case 1: return LayoutMode::LINE;
    case 2: return LayoutMode::SYSTEM;
    default: return LayoutMode::SYSTEM;
    }
}
#endif // USE_MUSESCORE

App::App(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("PlayBach");
    resize(1250, 850);

    m_audioPlayer = new AudioPlayer(this);
    m_syncTimer = new SyncTimer(this);
#ifdef USE_MUSESCORE
    m_syncMode = new SyncMode(this);
#endif
    m_playAlongSynth = new PlayAlongSynth();

    setupUI();
    setupToolbar();
    m_toolbar->hide(); // hidden until a level is loaded or playback starts

#ifdef USE_MUSESCORE
    // Sync mode signals
    connect(m_syncMode, &SyncMode::beatSynced, this, [this](int) {
        m_scoreWidget->widget()->update();
        if (m_syncPanel) m_syncPanel->updateStatus();
        if (m_waveformWidget) m_waveformWidget->update();
        int next = m_syncMode->nextUnsyncedBeat();
        if (next >= 0) m_scoreWidget->ensureBeatVisible(next);
        updateSyncTimerFromSyncMode();
    });
    connect(m_syncMode, &SyncMode::beatAdjusted, this, [this](int) {
        m_scoreWidget->widget()->update();
        updateSyncTimerFromSyncMode();
    });
    connect(m_syncMode, &SyncMode::beatUnsynced, this, [this](int) {
        m_scoreWidget->widget()->update();
        if (m_syncPanel) m_syncPanel->updateStatus();
        updateSyncTimerFromSyncMode();
    });
    connect(m_syncMode, &SyncMode::entered, this, [this]() {
        m_scoreWidget->setScore(m_score);
    });
    connect(m_syncMode, &SyncMode::exited, this, [this]() {
        m_scoreWidget->setScore(m_score);
    });
#endif // USE_MUSESCORE

    // Wire audio position to sync timer to score widget
    connect(m_audioPlayer, &AudioPlayer::positionChanged,
            this, &App::onPositionChanged);

    connect(m_syncTimer, &SyncTimer::cursorRectChanged,
            m_scoreWidget, &ScoreWidget::setCursorRect);

    connect(m_partPanel, &PartPanel::partsChanged, [this]() {
#ifdef USE_MUSESCORE
        m_scoreWidget->setScore(m_score); // refresh
#endif
        m_syncTimer->refresh();
    });

    // Bidirectional sync: sidebar checkbox <-> toolbar tracking button
    connect(m_trackingSettings, &TrackingSettings::trackingToggled, [this](bool on) {
        m_trackingAction->setChecked(on);
    });
    connect(m_trackingAction, &QAction::toggled, [this](bool on) {
        m_trackingSettings->setTrackingEnabled(on);
        m_scoreWidget->setCursorVisible(on);
        // Show/hide Verovio cursor
        if (m_useVerovio) {
            m_scoreWidget->runWebJavaScript(on ? "" : "hideCursor()");
        }
        // When tracking is off but auto-scroll is on, feed current position
        // so auto-scroll still works with an invisible cursor
        if (!on && m_trackingSettings->autoScrollEnabled() && playerDuration() > 0) {
            m_syncTimer->setTime(playerCurrentTime());
        }
    });

    connect(m_trackingSettings, &TrackingSettings::settingChanged, [this]() {
        bool autoScroll = m_trackingSettings->autoScrollEnabled();
        bool tracking = m_trackingSettings->trackingEnabled();

        m_scoreWidget->setAutoScrollEnabled(autoScroll);
        m_scoreWidget->setShowTriggerLine(m_trackingSettings->showTriggerLine());
        double trigger = m_trackingSettings->triggerLine() / 100.0;
        double scrollAmt = m_trackingSettings->scrollAmount() / 100.0;
        m_scoreWidget->setAutoScrollTrigger(trigger);
        m_scoreWidget->setAutoScrollTarget(trigger * (1.0 - scrollAmt));
        m_scoreWidget->setCursorAnchor(m_trackingSettings->cursorAnchor());

        // When auto-scroll enabled without tracking, use invisible cursor
        if (!tracking && autoScroll) {
            m_scoreWidget->setCursorVisible(false);
            if (playerDuration() > 0) {
                m_syncTimer->setTime(playerCurrentTime());
            }
        } else if (!tracking && !autoScroll) {
            m_scoreWidget->setCursorRect(QRectF(), -1);
        }
    });

#ifdef USE_MUSESCORE
    connect(m_displaySettings, &DisplaySettings::settingChanged, [this]() {
        if (!m_score || !m_renderer) return;
        m_score->setLayoutMode(comboIndexToLayoutMode(m_displaySettings->layoutMode()));
        bool showTitle = m_displaySettings->showTitleFrame();
        m_score->setShowVBox(showTitle);
        double topMargin = showTitle ? 0.39 : 0.10;
        m_score->style().set(Sid::pageOddTopMargin, topMargin);
        m_score->style().set(Sid::pageEvenTopMargin, topMargin);
        m_renderer->layoutScore(m_score, Fraction(0, 1), Fraction(-1, 1));
        m_scoreWidget->setScore(m_score); // refresh
        m_scoreWidget->scrollToTop();
    });
#endif

    // Apply initial tracking settings
    {
        double trigger = m_trackingSettings->triggerLine() / 100.0;
        double scrollAmt = m_trackingSettings->scrollAmount() / 100.0;
        m_scoreWidget->setAutoScrollEnabled(m_trackingSettings->autoScrollEnabled());
        m_scoreWidget->setShowTriggerLine(m_trackingSettings->showTriggerLine());
        m_scoreWidget->setAutoScrollTrigger(trigger);
        m_scoreWidget->setAutoScrollTarget(trigger * (1.0 - scrollAmt));
        m_scoreWidget->setCursorAnchor(m_trackingSettings->cursorAnchor());
    }

    // Set initial splitter sizes: score takes remaining, sidebar gets fixed width
    QTimer::singleShot(0, this, [this]() {
        m_mainSplitter->setSizes({width() - m_sidebarWidth, m_sidebarWidth});
    });
}

App::~App()
{
    delete m_playAlongSynth;
    // Score is owned by MuseScoreEngine (via m_engine), don't delete here.
    // m_score is just a non-owning pointer for legacy access.
#ifdef USE_MUSESCORE
    m_score = nullptr;
#endif
}

void App::setupUI()
{
    // Central splitter: waveform (top) + score (bottom)
    m_centralSplitter = new QSplitter(Qt::Vertical, this);
    m_centralSplitter->setChildrenCollapsible(false);
    m_centralSplitter->setHandleWidth(5);
    m_centralSplitter->setFocusPolicy(Qt::NoFocus);
    m_centralSplitter->setStyleSheet(
        "QSplitter::handle:vertical { background: #202225; }");

#ifdef USE_MUSESCORE
    m_waveformWidget = new WaveformWidget(m_centralSplitter);
    m_waveformWidget->hide();
    m_centralSplitter->addWidget(m_waveformWidget);

    connect(m_waveformWidget, &WaveformWidget::seekRequested, this, [this](double t) {
        playerSeekTo(t);
        m_syncTimer->setTime(t);
        onPositionChanged(t);
        m_scoreWidget->clearLastTappedBeat();
    });
#endif

    m_scoreWidget = new ScoreWidget(m_centralSplitter);
    m_centralSplitter->addWidget(m_scoreWidget);

#ifdef USE_MUSESCORE
    // Beat click on score → seek audio + scroll waveform + update waveform display
    connect(m_scoreWidget, &ScoreWidget::beatClicked, this, [this](int beatIndex) {
        if (!m_syncMode || beatIndex < 0 || beatIndex >= m_syncMode->totalBeats()) return;
        m_scoreWidget->clearLastTappedBeat();
        if (m_waveformWidget) m_waveformWidget->widget()->update();
        const auto& beat = m_syncMode->beats()[beatIndex];
        if (beat.synced) {
            playerSeekTo(beat.effectiveTime());
            m_syncTimer->setTime(beat.effectiveTime());
            onPositionChanged(beat.effectiveTime());
            if (m_waveformWidget) m_waveformWidget->scrollToTime(beat.effectiveTime());
        }
    });

    // Beat click on waveform → seek audio + update score display
    connect(m_waveformWidget, &WaveformWidget::beatClicked, this, [this](int beatIndex) {
        if (!m_syncMode || beatIndex < 0 || beatIndex >= m_syncMode->totalBeats()) return;
        m_scoreWidget->clearLastTappedBeat();
        m_scoreWidget->widget()->update();
        const auto& beat = m_syncMode->beats()[beatIndex];
        if (beat.synced) {
            playerSeekTo(beat.effectiveTime());
            m_syncTimer->setTime(beat.effectiveTime());
            onPositionChanged(beat.effectiveTime());
        }
    });
#endif

    // Instrument panel (hidden by default, shown via toolbar button)
    m_instrumentPanel = new QWidget();
    {
        m_instrumentPanelLayout = new QVBoxLayout(m_instrumentPanel);
        m_instrumentPanelLayout->setContentsMargins(16, 6, 16, 6);
        m_instrumentPanelLayout->setSpacing(4);

        // Scan soundfonts directory
        m_soundfontsDir = QCoreApplication::applicationDirPath() + "/../../resources/sounds";
        QDir sfDir(m_soundfontsDir);
        QStringList sfFiles = sfDir.entryList({"*.sf2", "*.sf3"}, QDir::Files, QDir::Name);
        for (const auto& fn : sfFiles) {
            m_soundfontPaths.append(sfDir.absoluteFilePath(fn));
        }

        // Single-voice row: Soundfont + Instrument
        m_singleVoiceRow = new QWidget();
        {
            auto* row1 = new QHBoxLayout(m_singleVoiceRow);
            row1->setContentsMargins(0, 0, 0, 0);
            row1->setSpacing(16);

            auto* sfLabel = new QLabel("Soundfont");
            sfLabel->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: bold;").arg(Theme::textPrimary().name()));
            row1->addWidget(sfLabel);

            m_soundfontCombo = new QComboBox();
            m_soundfontCombo->setMinimumWidth(160);
            for (const auto& path : m_soundfontPaths) {
                QString fn = QFileInfo(path).fileName();
                m_soundfontCombo->addItem(fn.left(fn.lastIndexOf('.')));
            }
            connect(m_soundfontCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this](int idx) {
                if (idx < 0 || idx >= m_soundfontPaths.size()) return;
                m_instrumentCombo->setEnabled(false);
                m_instrumentCombo->clear();
                m_instrumentCombo->addItem("Loading...");
                QTimer::singleShot(0, this, [this, idx]() {
                    m_playAlongSynth->loadSoundfont(m_soundfontPaths[idx]);
                    int curProg = m_playAlongSynth->gmProgram();
                    m_instrumentCombo->blockSignals(true);
                    m_instrumentCombo->clear();
                    auto presets = m_playAlongSynth->presets();
                    for (const auto& p : presets) {
                        m_instrumentCombo->addItem(p.second, p.first);
                    }
                    for (int i = 0; i < m_instrumentCombo->count(); ++i) {
                        if (m_instrumentCombo->itemData(i).toInt() == curProg) {
                            m_instrumentCombo->setCurrentIndex(i);
                            break;
                        }
                    }
                    m_instrumentCombo->blockSignals(false);
                    m_instrumentCombo->setEnabled(true);
                });
            });
            row1->addWidget(m_soundfontCombo);

            row1->addSpacing(16);

            auto* instrLabel = new QLabel("Instrument");
            instrLabel->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: bold;").arg(Theme::textPrimary().name()));
            row1->addWidget(instrLabel);

            m_instrumentCombo = new QComboBox();
            m_instrumentCombo->setMinimumWidth(180);
            connect(m_instrumentCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this](int idx) {
                if (idx >= 0) {
                    m_playAlongSynth->setGmProgram(m_instrumentCombo->itemData(idx).toInt());
                }
            });
            row1->addWidget(m_instrumentCombo);
            row1->addStretch();
        }
        m_instrumentPanelLayout->addWidget(m_singleVoiceRow);

        // Row: Transpose + Volume (always visible)
        auto* row2 = new QHBoxLayout();
        row2->setSpacing(16);

        auto* pitchLabel = new QLabel("Transpose");
        pitchLabel->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: bold;").arg(Theme::textPrimary().name()));
        row2->addWidget(pitchLabel);

        m_transposeSpin = new QDoubleSpinBox();
        m_transposeSpin->setRange(-24.0, 24.0);
        m_transposeSpin->setValue(0.0);
        m_transposeSpin->setSingleStep(0.05);
        m_transposeSpin->setDecimals(2);
        m_transposeSpin->setSuffix(" semitones");
        m_transposeSpin->setMinimumWidth(140);
        m_transposeSpin->setFocusPolicy(Qt::NoFocus);
        if (auto* le = m_transposeSpin->findChild<QLineEdit*>()) {
            le->setReadOnly(true);
            le->setFocusPolicy(Qt::NoFocus);
        }
        connect(m_transposeSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this](double val) {
            m_playAlongSynth->setPitchOffset(val);
        });
        row2->addWidget(m_transposeSpin);

        row2->addSpacing(16);

        auto* instVolLabel = new QLabel("Instrument Vol.");
        instVolLabel->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: bold;").arg(Theme::textPrimary().name()));
        row2->addWidget(instVolLabel);

        m_instrumentVolSlider = new QSlider(Qt::Horizontal);
        m_instrumentVolSlider->setRange(0, 150);
        m_instrumentVolSlider->setValue(60);
        m_instrumentVolSlider->setMinimumWidth(120);
        m_instrumentVolSlider->setToolTip(QString("Instrument volume: %1%").arg(60));
        connect(m_instrumentVolSlider, &QSlider::valueChanged, this, [this](int val) {
            m_instrumentVolSlider->setToolTip(QString("Instrument volume: %1%").arg(val));
            m_playAlongSynth->setGain(val / 100.0);
        });
        row2->addWidget(m_instrumentVolSlider);
        row2->addStretch();

        m_instrumentPanelLayout->addLayout(row2);

        QPalette pal = m_instrumentPanel->palette();
        pal.setColor(QPalette::Window, Theme::panelBg());
        m_instrumentPanel->setPalette(pal);
        m_instrumentPanel->setAutoFillBackground(true);
    }
    m_centralSplitter->addWidget(m_instrumentPanel);
    m_instrumentPanel->hide();

    // Score gets most of the space by default
    m_centralSplitter->setStretchFactor(0, 0); // waveform: don't stretch
    m_centralSplitter->setStretchFactor(1, 1); // score: stretch
    m_centralSplitter->setStretchFactor(2, 0); // instrument panel: don't stretch

    // Sidebar sits beside the score in a horizontal splitter
    m_sidebarWidget = new QWidget();
    m_sidebarWidget->setAutoFillBackground(true);
    m_sidebarWidget->setFocusPolicy(Qt::ClickFocus);

    auto* sidebarLayout = new QVBoxLayout(m_sidebarWidget);
    sidebarLayout->setContentsMargins(2, 0, 0, 0);

    m_sidebarSplitter = new QSplitter(Qt::Vertical);
    m_sidebarSplitter->setChildrenCollapsible(false);

    m_partPanel = new PartPanel();
    m_sidebarSplitter->addWidget(new CollapsibleSection("Parts", m_partPanel));

    m_trackingSettings = new TrackingSettings();
    auto* trackingSection = new CollapsibleSection("Tracking", m_trackingSettings);
    trackingSection->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_sidebarSplitter->addWidget(trackingSection);

    m_displaySettings = new DisplaySettings();
    auto* displaySection = new CollapsibleSection("Score Display", m_displaySettings);
    displaySection->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_sidebarSplitter->addWidget(displaySection);

    // Spacer absorbs extra space at the bottom
    auto* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_sidebarSplitter->addWidget(spacer);

    m_sidebarSplitter->setStretchFactor(0, 0);
    m_sidebarSplitter->setStretchFactor(1, 0);
    m_sidebarSplitter->setStretchFactor(2, 0);
    m_sidebarSplitter->setStretchFactor(3, 1);

    m_sidebarSplitter->setHandleWidth(12);

    // Hide the last handle (between Tracking and spacer)
    if (auto* lastHandle = m_sidebarSplitter->handle(3)) {
        lastHandle->setDisabled(true);
        lastHandle->setFixedHeight(0);
    }

    sidebarLayout->addWidget(m_sidebarSplitter);

    // Score view: score (left) + right sidebar (right)
    m_mainSplitter = new QSplitter(Qt::Horizontal);
    m_mainSplitter->setChildrenCollapsible(false);
    m_mainSplitter->addWidget(m_centralSplitter);
    m_mainSplitter->addWidget(m_sidebarWidget);
    m_mainSplitter->setStretchFactor(0, 1); // score stretches
    m_mainSplitter->setStretchFactor(1, 0); // sidebar fixed

    // Level browser (shown when picking levels)
    m_levelBrowser = new LevelBrowser();
    connect(m_levelBrowser, &LevelBrowser::levelSelected, this, [this](int si, int li) {
        loadLevel(m_currentWorldIndex, si, li);
    });
    connect(m_levelBrowser, &LevelBrowser::resumeRequested, this, [this]() {
        // Always reload the level (no resume — clean slate every time)
        if (m_activeWorldIndex >= 0 && m_activeSectionIndex >= 0 && m_activeLevelIndex >= 0) {
            loadLevel(m_activeWorldIndex, m_activeSectionIndex, m_activeLevelIndex);
        }
    });
    connect(m_levelBrowser, &LevelBrowser::interpretationSelected, this, [this](int index) {
        m_preselectedInterpretation = index;
    });

    // Central stacked widget: 0 = level browser, 1 = score view
    m_centralStack = new QStackedWidget();
    m_centralStack->setAutoFillBackground(true);
    QPalette csPal = m_centralStack->palette();
    csPal.setColor(QPalette::Window, Theme::contentBg());
    m_centralStack->setPalette(csPal);
    m_centralStack->addWidget(m_levelBrowser);
    m_centralStack->addWidget(m_mainSplitter);
    m_centralStack->setCurrentIndex(0);

    // World sidebar on the left
    m_worldSidebar = new WorldSidebar();
    connect(m_worldSidebar, &WorldSidebar::worldSelected, this, [this](int index) {
        m_currentWorldIndex = index;
        if (index >= 0 && index < m_worlds.size()) {
            // Sync interpretation selection back to level browser
            if (index == m_activeWorldIndex)
                m_levelBrowser->setSelectedInterpretation(m_preselectedInterpretation);
            m_levelBrowser->setWorld(m_worlds[index]);
            if (index == m_activeWorldIndex && m_activeSectionIndex >= 0) {
                m_levelBrowser->setCurrentLevel(m_activeSectionIndex, m_activeLevelIndex);
            }
            // Kill YouTube player — stop audio, destroy widget, clean up
            if (m_youtubePlayer) {
                m_youtubePlayer->stop();
                m_youtubePlayer->pause();
                if (m_expandedVideoContainer && m_expandedVideoContainer->layout()) {
                    QLayoutItem* item;
                    while ((item = m_expandedVideoContainer->layout()->takeAt(0)) != nullptr)
                        delete item;
                }
                auto* view = m_youtubePlayer->videoWidget();
                if (view) {
                    view->setParent(nullptr);
                    delete view;
                }
                delete m_youtubePlayer;
                m_youtubePlayer = nullptr;
                m_currentYoutubeUrl.clear();
                m_useYouTube = false;
                m_speedButton->setEnabled(false);
                m_speedButton->setText("Speed: 1x");
            }
            if (m_expandedVideoContainer) {
                m_expandedVideoContainer->hide();
                m_expandedVideoContainer->setParent(nullptr);
            }
            m_videoExpanded = false;
            // Reset score cursor immediately
            m_scoreWidget->setCursorRect(QRectF(), -1);
            m_scoreWidget->setCursorVisible(false);
            m_scoreWidget->scrollToTop();
            if (m_useVerovio)
                m_scoreWidget->runWebJavaScript("hideCursor()");
            m_syncTimer->setTime(0);
            m_centralStack->setCurrentIndex(0);
            m_toolbar->hide();
            m_worldSidebar->clearInterpretations();
        }
    });
    // Sidebar interpretation combo removed — interpretation selection happens
    // only in the level browser thumbnails.
    connect(m_worldSidebar, &WorldSidebar::volumeChanged, this, [this](int val) {
        double vol = val / 100.0;
        if (m_useYouTube && m_youtubePlayer) {
            m_youtubePlayer->setVolume(std::min(val, 100));
        } else if (m_audioPlayer) {
            m_audioPlayer->setVolume(std::min(vol, 1.0));
        }
    });

    // Top-level layout: world sidebar | central stack
    auto* topSplitter = new QSplitter(Qt::Horizontal, this);
    topSplitter->setChildrenCollapsible(false);
    topSplitter->addWidget(m_worldSidebar);
    topSplitter->addWidget(m_centralStack);
    topSplitter->setStretchFactor(0, 0);
    topSplitter->setStretchFactor(1, 1);
    // Make the world sidebar handle invisible (fixed width)
    if (auto* h = topSplitter->handle(1)) {
        h->setDisabled(true);
        h->setFixedWidth(0);
    }
    setCentralWidget(topSplitter);
}

void App::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
#ifdef USE_MUSESCORE
    repositionSyncSidebar();
#endif
}

bool App::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_expandedVideoContainer && event->type() == QEvent::Resize && m_videoExpanded) {
        auto* re = static_cast<QResizeEvent*>(event);
        int h = re->size().height();
        int w = re->size().width();
        if (h > 0 && w > 0 && m_youtubePlayer) {
            m_youtubePlayer->resizePlayer(w, h);
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void App::setSidebarVisible(bool visible)
{
    m_sidebarWidget->setVisible(visible);
}

void App::setupToolbar()
{
    m_toolbar = addToolBar("Playback");
    m_toolbar->setContextMenuPolicy(Qt::PreventContextMenu);
    m_toolbar->setMovable(false);

    m_playPauseAction = m_toolbar->addAction("Play");
    m_playPauseAction->setShortcut(QKeySequence(Qt::Key_Space));
    connect(m_playPauseAction, &QAction::triggered, this, &App::togglePlayPause);
    m_toolbar->widgetForAction(m_playPauseAction)->setCursor(Qt::PointingHandCursor);

    m_stopAction = m_toolbar->addAction("Restart");
    m_stopAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    connect(m_stopAction, &QAction::triggered, this, [this]() {
        playerSeekTo(0);
        m_playAlongSynth->resetPosition();
        m_keysHeld = 0;
        if (m_useVerovio && !m_vrvVoices.empty()) {
            // Reset scroll position and highlight first notes
            m_scoreWidget->runWebJavaScript("resetScroll()");
            for (int vi = 0; vi < static_cast<int>(m_vrvVoices.size()); ++vi) {
                auto& vv = m_vrvVoices[vi];
                if (!vv.elementIds.empty())
                    m_scoreWidget->overlayHighlight(vi, vv.elementIds[0]);
            }
        } else if (m_multiVoice) {
            for (int vi = 0; vi < m_voiceKeysHeld.size(); ++vi)
                m_voiceKeysHeld[vi] = 0;
            if (m_playAlongSynth->voiceCount() > 0)
                m_scoreWidget->setHighlightElement(m_playAlongSynth->nextNoteElementForVoice(0));
            if (m_playAlongSynth->voiceCount() > 1)
                m_scoreWidget->setHighlightElement2(m_playAlongSynth->nextNoteElementForVoice(1));
        } else {
            m_scoreWidget->setHighlightElement(m_playAlongSynth->nextNoteElement());
        }
        m_syncTimer->setTime(0);
        onPositionChanged(0);
        playerPlay();
        m_playPauseAction->setText("Pause");
    });
    m_toolbar->widgetForAction(m_stopAction)->setCursor(Qt::PointingHandCursor);

    m_toolbar->addSeparator();

    m_seekSlider = new QSlider(Qt::Horizontal, this);
    m_seekSlider->setCursor(Qt::PointingHandCursor);
    m_seekSlider->setRange(0, 1000);
    m_seekSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    connect(m_seekSlider, &QSlider::sliderPressed, [this]() { m_sliderDragging = true; });
    connect(m_seekSlider, &QSlider::sliderReleased, [this]() {
        m_sliderDragging = false;
        onSeekSliderMoved(m_seekSlider->value());
    });
    connect(m_seekSlider, &QSlider::sliderMoved, this, &App::onSeekSliderMoved);
    m_toolbar->addWidget(m_seekSlider);

    m_timeLabel = new QLabel("0:00 / 0:00", this);
    m_timeLabel->setMinimumWidth(120);
    m_toolbar->addWidget(m_timeLabel);

    m_toolbar->addSeparator();

    // Interpretation button with popup menu
    m_interpButton = new QPushButton("Interpretation", this);
    m_interpButton->setFlat(true);
    m_interpButton->setCursor(Qt::PointingHandCursor);
    m_interpButton->setFocusPolicy(Qt::NoFocus);
    m_interpButton->setStyleSheet(
        "QPushButton { padding: 2px 4px; }"
        "QPushButton:pressed { background: transparent; }");
    m_interpMenu = new QMenu(m_interpButton);
    m_interpMenu->setStyleSheet(QString(
        "QMenu { background: %1; color: %2; border: 1px solid %3; }"
        "QMenu::item { padding: 6px 20px; }"
        "QMenu::item:selected { background: %3; }"
    ).arg(Theme::panelBg().name(), Theme::textPrimary().name(), Theme::inputBg().name()));
    m_interpButton->setMenu(m_interpMenu);
    m_toolbar->addWidget(m_interpButton);

    m_toolbar->addSeparator();

    // Volume button with popup vertical slider
    auto* volButton = new QPushButton("Volume", this);
    volButton->setFlat(true);
    volButton->setCursor(Qt::PointingHandCursor);
    volButton->setFocusPolicy(Qt::NoFocus);
    volButton->setStyleSheet(
        "QPushButton { padding: 2px 4px; }"
        "QPushButton:pressed { background: transparent; }");

    m_volumeSlider = new QSlider(Qt::Vertical);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(80);
    m_volumeSlider->setFixedSize(30, 120);
    m_volumeSlider->setToolTip("Volume: 80%");
    m_volumeSlider->setStyleSheet(QString(
        "QSlider::groove:vertical { background: %1; width: 4px; border-radius: 2px; }"
        "QSlider::handle:vertical { background: %2; width: 16px; height: 16px;"
        "  margin: 0 -6px; border-radius: 8px; }"
        "QSlider::sub-page:vertical { background: %1; }"
        "QSlider::add-page:vertical { background: %3; }"
    ).arg(Theme::inputBg().name(), Theme::accent().name(), Theme::accent().name()));
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int val) {
        m_volumeSlider->setToolTip(QString("Volume: %1%").arg(val));
        if (m_youtubePlayer)
            m_youtubePlayer->setVolume(std::min(val, 100));
    });

    auto* volPopup = new QWidget(this, Qt::Popup);
    volPopup->setFixedSize(36, 130);
    volPopup->setStyleSheet(QString(
        "background: %1; border: none; border-radius: 8px;"
    ).arg(Theme::panelBg().name()));
    auto* volLay = new QVBoxLayout(volPopup);
    volLay->setContentsMargins(8, 8, 8, 8);
    volLay->addWidget(m_volumeSlider);

    connect(volButton, &QPushButton::clicked, this, [volButton, volPopup]() {
        QPoint pos = volButton->mapToGlobal(QPoint(volButton->width() / 2 - 20, volButton->height()));
        volPopup->move(pos);
        volPopup->show();
    });
    m_toolbar->addWidget(volButton);

    m_toolbar->addSeparator();

    // Speed button (enabled later when YouTube player is ready)
    m_speedButton = new QPushButton("Speed: 1x", this);
    m_speedButton->setFlat(true);
    m_speedButton->setCursor(Qt::PointingHandCursor);
    m_speedButton->setFocusPolicy(Qt::NoFocus);
    m_speedButton->setStyleSheet(
        "QPushButton { padding: 2px 4px; }"
        "QPushButton:pressed { background: transparent; padding: 2px 4px; }");
    m_speedButton->setEnabled(false);
    auto* speedMenu = new QMenu(m_speedButton);
    for (double r : {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0}) {
        auto* action = speedMenu->addAction(QString("%1x").arg(r));
        action->setData(r);
    }
    m_speedButton->setMenu(speedMenu);
    m_toolbar->addWidget(m_speedButton);

    m_toolbar->addSeparator();

    m_trackingAction = new QAction(this);
    m_trackingAction->setCheckable(true);
    m_trackingAction->setChecked(true);
    // shortcut is on trackingButton, not the action

    m_trackingButton = new QPushButton("Tracking", this);
    m_trackingButton->setFlat(true);
    m_trackingButton->setCursor(Qt::PointingHandCursor);
    m_trackingButton->setFocusPolicy(Qt::NoFocus);
    m_trackingButton->setStyleSheet(
        "QPushButton { padding: 2px 4px; }"
        "QPushButton:pressed { background: transparent; padding: 2px 4px; }");
    m_trackingButton->setIconSize(QSize(8, 8));

    updateTrackingIcon();
    // Size for the wider label so it doesn't jump when switching text
    m_trackingButton->setText("Tracking");
    QSize trackSz = m_trackingButton->sizeHint();
    m_trackingButton->setText("Record");
    QSize recSz = m_trackingButton->sizeHint();
    m_trackingButton->setFixedSize(QSize(std::max(trackSz.width(), recSz.width()),
                                         std::max(trackSz.height(), recSz.height())));
    updateTrackingIcon(); // restore correct text

    m_toolbar->addWidget(m_trackingButton);
    m_trackingAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    addAction(m_trackingAction); // register with window for shortcut dispatch

    connect(m_trackingButton, &QPushButton::clicked, [this]() {
        if (m_recordTrackingActive) {
            stopRecordTracking();
        } else {
            m_trackingAction->toggle();
        }
    });
    connect(m_trackingAction, &QAction::toggled, [this](bool on) {
        updateTrackingIcon();
        if (!on && !m_trackingSettings->autoScrollEnabled()) {
            m_scoreWidget->setCursorRect(QRectF(), -1);
        }
    });

    // Right-click context menu on tracking button for Record / Save
    m_trackingButton->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_trackingButton, &QPushButton::customContextMenuRequested, [this](const QPoint& pos) {
        if (!m_playModeActive) return;
        QMenu menu;
        if (m_recordTrackingActive) {
            auto* stopAction = menu.addAction("Stop Recording");
            connect(stopAction, &QAction::triggered, this, [this]() { stopRecordTracking(); });
        } else {
            auto* recAction = menu.addAction("Record Tracking");
            connect(recAction, &QAction::triggered, this, [this]() {
                if (m_beatDataFromRecording) {
                    m_syncTimer->setBeatTimes({}, 0);
                    m_syncTimer->setBeatTicks({});
                    m_syncTimer->setMeasureStarts({});
                    m_beatDataFromRecording = false;
                }
                startRecordTracking();
            });
            if (m_beatDataFromRecording) {
                auto* saveAction = menu.addAction("Save Tracking Data");
                connect(saveAction, &QAction::triggered, this, &App::saveRecordedTracking);
            }
        }
        menu.exec(m_trackingButton->mapToGlobal(pos));
    });

    // Play Mode — hidden toggle (activated programmatically by loadLevel)
    m_playModeButton = new QPushButton(this);
    m_playModeButton->setCheckable(true);
    m_playModeButton->hide();
    connect(m_playModeButton, &QPushButton::toggled, this, [this](bool on) {
        if (on) enterPlayMode();
        else exitPlayMode();
    });

    m_toolbar->addSeparator();

    m_instrumentAction = m_toolbar->addAction("Instrument");
    m_instrumentAction->setCheckable(true);
    m_instrumentAction->setEnabled(false); // enabled when entering play mode
    auto* instrWidget = m_toolbar->widgetForAction(m_instrumentAction);
    instrWidget->setCursor(Qt::PointingHandCursor);
    instrWidget->setStyleSheet("font-size: 12px;");
    connect(m_instrumentAction, &QAction::toggled, [this](bool on) {
        m_instrumentPanel->setVisible(on);
    });

    // Sidebar action (hidden from toolbar but still functional for programmatic use)
    m_sidebarAction = new QAction(this);
    m_sidebarAction->setCheckable(true);
    m_sidebarAction->setChecked(true);
    m_sidebarAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    connect(m_sidebarAction, &QAction::toggled, [this](bool on) {
        setSidebarVisible(on);
    });

    // Zoom label kept for internal use but hidden
    m_zoomLabel = new QLabel(this);
    m_zoomLabel->hide();
    connect(m_scoreWidget, &ScoreWidget::zoomChanged, [this](double zoom) {
        m_zoomLabel->setText(QString("%1%").arg(static_cast<int>(zoom * 100)));
    });

    connect(m_audioPlayer, &AudioPlayer::playbackStarted, [this]() {
        m_playPauseAction->setText("Pause");
        m_scoreWidget->setPlaying(true);
    });
    connect(m_audioPlayer, &AudioPlayer::playbackPaused, [this]() {
        m_playPauseAction->setText("Play");
        m_scoreWidget->setPlaying(false);
    });
    connect(m_audioPlayer, &AudioPlayer::playbackStopped, [this]() {
        m_playPauseAction->setText("Play");
        m_scoreWidget->setPlaying(false);
    });
}

bool App::loadScore(const QString& musicXmlPath)
{
    QFileInfo fi(musicXmlPath);
    if (!fi.exists()) {
        qWarning() << "MusicXML file not found:" << musicXmlPath;
        return false;
    }

    // Disconnect old part-panel signals to avoid duplicate/stale connections
    disconnect(m_partPanel, nullptr, this, nullptr);

    // Clean up old engine (which owns the score).
    // Clear ScoreWidget's reference first to avoid dangling pointer during resize.
    m_scoreWidget->setEngine(nullptr);
    m_engine.reset();
#ifdef USE_MUSESCORE
    m_score = nullptr;
    m_renderer.reset();
#endif

    // Create the appropriate engine
    if (m_useVerovio) {
        auto engine = std::make_unique<scoretracker::VerovioEngine>();

        // Set Verovio pageWidth directly in Verovio units (bypasses DPI calc).
        // Default is 2100 (≈A4 width). Wider = more measures per system.
        // CSS width:100% scales the SVG to fit the viewport.
        engine->setPageWidthDirect(3200);
        engine->setMarginsInches(0.20, 0.20, 0.6, 0.6);
        engine->setSpatiumInches(0.046);

        if (!engine->loadMusicXML(musicXmlPath)) {
            qWarning() << "Verovio failed to load:" << musicXmlPath;
            return false;
        }

        engine->layout();
        m_engine = std::move(engine);

        qDebug() << "Score loaded (Verovio):" << musicXmlPath
                 << "pages:" << m_engine->pageCount()
                 << "parts:" << m_engine->partCount();

        // Set up sync timer with engine (don't call setEngine on ScoreWidget yet —
        // setVisibleParts in loadLevel will do it with filtered parts, avoiding a double reload)
        m_syncTimer->setEngine(m_engine.get());

        // Init FluidSynth for play-along
        QString sf3Path;
        if (!m_soundfontPaths.isEmpty()) {
            sf3Path = m_soundfontPaths.first();
        } else {
            sf3Path = QCoreApplication::applicationDirPath()
                + "/../../resources/sounds/MS Basic.sf3";
        }
        m_playAlongSynth->init(sf3Path);

        // PartPanel not used in Verovio mode
#ifdef USE_MUSESCORE
        m_partPanel->setScore(nullptr);
        m_partPanel->setRenderer(nullptr);
#endif

        setWindowTitle("PlayBach");
        QTimer::singleShot(0, m_scoreWidget, &ScoreWidget::zoomToFit);
        return true;
    }

#ifdef USE_MUSESCORE
    // --- MuseScore engine path ---
    auto msEngine = std::make_unique<scoretracker::MuseScoreEngine>();

    if (!msEngine->loadMusicXML(musicXmlPath)) {
        return false;
    }

    // Get the raw score for MuseScore-specific features
    m_score = msEngine->score();
    m_renderer = std::shared_ptr<IScoreRenderer>(
        msEngine->renderer(), [](IScoreRenderer*) {}); // non-owning shared_ptr

    qDebug() << "Score loaded:" << musicXmlPath;
    qDebug() << "Parts:" << m_score->parts().size();
    qDebug() << "Measures:" << m_score->nmeasures();

    // Page layout: Letter, matching MuseScore defaults
    msEngine->setPageSizeInches(8.5, 11.0);
    msEngine->setMarginsInches(0.39, 0.79, 0.39, 0.39);
    msEngine->setSpatiumInches(0.046);

    // Ensure instrument names show on all systems
    m_score->style().set(Sid::firstSystemInstNameVisibility,
        mu::engraving::PropertyValue(int(InstrumentLabelVisibility::LONG)));
    m_score->style().set(Sid::subsSystemInstNameVisibility,
        mu::engraving::PropertyValue(int(InstrumentLabelVisibility::SHORT)));
    m_score->style().set(Sid::hideInstrumentNameIfOneInstrument, false);

    // Generate short instrument names from long names if missing
    static const QMap<QString, QString> FULL_ABBREVS = {
        {"Tromba in D. I",       "Tr. I"},
        {"Tromba in D. II",      "Tr. II"},
        {"Tromba in D. III",     "Tr. III"},
        {"Timpani in D.A.",      "Timp."},
        {"Flauto traverso I.",   "Fl. tr. I"},
        {"Flauto traverso II.",  "Fl. tr. II"},
        {"Oboe I.",              "Ob. I"},
        {"Oboe II.",             "Ob. II"},
        {"Violino I.",           "Vl. I"},
        {"Violino II.",          "Vl. II"},
        {"Viola.",               "Vla."},
        {"Soprano I.",           "S. I"},
        {"Soprano II.",          "S. II"},
        {"Alto.",                "A."},
        {"Tenore.",              "T."},
        {"Basso.",               "B."},
        {"Organo e\nContinuo.",  "Org. / B.c."},
        {"Organo e Continuo.",   "Org. / B.c."},
    };
    for (auto* part : m_score->parts()) {
        Instrument* instr = part->instrument();
        if (instr->shortNames().empty() && !instr->longNames().empty()) {
            QString longName = instr->longNames().front().name().toQString();
            QString shortName = FULL_ABBREVS.value(longName, longName);
            instr->setShortName(muse::String::fromQString(shortName));
        }
    }

    // Apply display settings before layout
    msEngine->setLayoutMode(m_displaySettings->layoutMode());
    bool showTitle = m_displaySettings->showTitleFrame();
    msEngine->setShowTitleFrame(showTitle);
    {
        double topMargin = showTitle ? 0.39 : 0.10;
        m_score->style().set(Sid::pageOddTopMargin, topMargin);
        m_score->style().set(Sid::pageEvenTopMargin, topMargin);
    }

    // Layout the score
    msEngine->layout();
    m_engine = std::move(msEngine);

    qDebug() << "Score layout complete, pages:" << m_score->pages().size();

    // Set up widgets with engine
    m_scoreWidget->setEngine(m_engine.get());
    m_scoreWidget->setRenderer(m_renderer.get());
    m_scoreWidget->setScore(m_score);

    m_syncTimer->setEngine(m_engine.get());
    m_syncTimer->setScore(m_score);

    // Initialize play-along synth (no voice until user clicks ear icon)
    // Initialize with first available soundfont
    QString sf3Path;
    if (!m_soundfontPaths.isEmpty()) {
        sf3Path = m_soundfontPaths.first();
    } else {
        sf3Path = QCoreApplication::applicationDirPath() + "/../../thirdparty/musescore_a/share/sound/MS Basic.sf3";
    }
    m_playAlongSynth->init(sf3Path);

    // Populate instrument combo from loaded soundfont
    if (m_instrumentCombo) {
        m_instrumentCombo->blockSignals(true);
        m_instrumentCombo->clear();
        auto presets = m_playAlongSynth->presets();
        for (const auto& p : presets) {
            m_instrumentCombo->addItem(p.second, p.first);
        }
        m_instrumentCombo->blockSignals(false);
    }

    m_partPanel->setScore(m_score);
    m_partPanel->setRenderer(m_renderer.get());
    m_partPanel->setScoreFileName(fi.fileName());

    // Connect play-along part selection
    connect(m_partPanel, &PartPanel::playAlongChanged, this, [this](mu::engraving::Part* part, int gmProgram) {
        m_playAlongSynth->setVoice(part, gmProgram, m_score);
        m_scoreWidget->setHighlightElement(m_playAlongSynth->nextNoteElement());
    });
    connect(m_partPanel, &PartPanel::playAlongInstrChanged, this, [this](int gmProgram) {
        m_playAlongSynth->setGmProgram(gmProgram);
    });
    connect(m_partPanel, &PartPanel::playAlongVolumeChanged, this, [this](double gain) {
        m_playAlongSynth->setGain(gain);
    });

    // Size the Parts section: show all parts or cap at 60% of window
    int sectionHeaderH = 28; // CollapsibleSection header
    int desiredPartsHeight = sectionHeaderH + m_partPanel->desiredHeight();
    int maxPartsHeight = height() * 6 / 10;
    int partsHeight = std::min(desiredPartsHeight, maxPartsHeight);
    int trackingHeight = m_sidebarSplitter->widget(1)->sizeHint().height();
    int displayHeight = m_sidebarSplitter->widget(2)->sizeHint().height();
    int remaining = height() - partsHeight - trackingHeight - displayHeight;
    if (remaining < 0) remaining = 0;
    m_sidebarSplitter->setSizes({partsHeight, trackingHeight, displayHeight, remaining});

    setWindowTitle("PlayBach");

    // Fit score to viewport after layout settles
    QTimer::singleShot(0, m_scoreWidget, &ScoreWidget::zoomToFit);

    return true;
#else
    // No MuseScore engine available
    return false;
#endif // USE_MUSESCORE
}

void App::setVisibleParts(const QList<int>& partNumbers)
{
    if (m_engine && m_engine->usesWebRendering()) {
        // Verovio: reload with parts in the requested order (with bracket grouping)
        auto* vrvEngine = dynamic_cast<scoretracker::VerovioEngine*>(m_engine.get());
        if (vrvEngine) {
            vrvEngine->selectParts(partNumbers);
            // Refresh the web view
            m_scoreWidget->setEngine(m_engine.get());
        }
        return;
    }
#ifdef USE_MUSESCORE
    m_partPanel->showOnlyParts(partNumbers);
#endif
}

#ifdef USE_MUSESCORE
void App::startSyncMode()
{
    // Sync mode archived — enter directly via CLI flag
    enterSyncMode();
    m_trackingAction->setChecked(false);
}
#endif

void App::startPlayMode()
{
    m_playModeButton->setChecked(true);
}

void App::selectFileSource()
{
    if (!m_useYouTube || m_sourceAudioFile.isEmpty()) return;
    if (m_youtubePlayer) {
        m_youtubePlayer->pause();
        m_speedButton->setEnabled(false);
        m_speedButton->setText("Speed: 1x");
    }
    m_useYouTube = false;
    loadAudio(m_sourceAudioFile);
    playerPause();
}

bool App::loadBeatData(const QString& jsonPath)
{
    m_beatDataPath = QFileInfo(jsonPath).absoluteFilePath();

    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open beat data file:" << jsonPath;
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        qWarning() << "Invalid beat data JSON:" << jsonPath;
        return false;
    }

    QJsonObject obj = doc.object();
    int beatsPerMeasure = obj.value("beats_per_measure").toInt(3);

    std::vector<double> beatTimes;
    std::vector<double> measureStarts;

    std::vector<int> beatTicks;

    if (obj.contains("control_points")) {
        // Recorded tracking format: array of {tick, time} + optional measure_starts
        QJsonArray cpArr = obj.value("control_points").toArray();
        beatTimes.reserve(cpArr.size());
        beatTicks.reserve(cpArr.size());
        for (const auto& v : cpArr) {
            QJsonObject pt = v.toObject();
            beatTicks.push_back(pt["tick"].toInt());
            beatTimes.push_back(pt["time"].toDouble());
        }
        if (obj.contains("measure_starts")) {
            QJsonArray msArr = obj.value("measure_starts").toArray();
            for (const auto& v : msArr)
                measureStarts.push_back(v.toDouble());
        }
    } else if (obj.contains("measures")) {
        // Measures format: array of {beats: [{beat, time}, ...]}
        QJsonArray measuresArr = obj.value("measures").toArray();
        for (const auto& mv : measuresArr) {
            QJsonArray beatsArr = mv.toObject().value("beats").toArray();
            bool first = true;
            for (const auto& bv : beatsArr) {
                double t = bv.toObject().value("time").toDouble();
                beatTimes.push_back(t);
                if (first) {
                    measureStarts.push_back(t);
                    first = false;
                }
            }
        }
    } else if (obj.contains("beat_times")) {
        // Legacy flat array format
        QJsonArray arr = obj.value("beat_times").toArray();
        beatTimes.reserve(arr.size());
        for (const auto& v : arr) {
            beatTimes.push_back(v.toDouble());
        }
        for (size_t i = 0; i < beatTimes.size(); i += beatsPerMeasure) {
            measureStarts.push_back(beatTimes[i]);
        }
    }

    // Compute tick positions if not already provided (beat-based formats)
    if (beatTicks.empty()) {
        beatTicks.reserve(beatTimes.size());
        for (size_t i = 0; i < beatTimes.size(); ++i) {
            beatTicks.push_back(static_cast<int>(i) * 480);
        }
    }

    m_syncTimer->setBeatTimes(beatTimes, beatsPerMeasure);
    m_syncTimer->setBeatTicks(beatTicks);
    m_syncTimer->setMeasureStarts(measureStarts);

    qDebug() << "Loaded beat data:" << beatTimes.size() << "beats,"
             << measureStarts.size() << "measures, beats_per_measure:" << beatsPerMeasure;
    return true;
}

bool App::loadAudio(const QString& audioPath)
{
    m_audioFilePath = audioPath;
    if (!m_audioPlayer->load(audioPath)) {
        return false;
    }

    m_seekSlider->setRange(0, static_cast<int>(playerDuration() * 10));
    m_timeLabel->setText(QString("0:00 / %1").arg(formatTime(playerDuration())));

    return true;
}

void App::togglePlayPause()
{
    qDebug() << "togglePlayPause called, isPlaying:" << playerIsPlaying()
             << "duration:" << playerDuration();
    if (playerIsPlaying()) {
        playerPause();
    } else {
        playerPlay();
    }
}

void App::onSeekSliderMoved(int value)
{
    double duration = playerDuration();
    double seconds = (static_cast<double>(value) / m_seekSlider->maximum()) * duration;
    playerSeekTo(seconds);
    m_syncTimer->setTime(seconds);
    if (m_useVerovio)
        m_scoreWidget->setCursorTick(m_syncTimer->currentTick());
    m_scoreWidget->clearLastTappedBeat();
}

void App::onPositionChanged(double seconds)
{
    // Adjust for interpretation start offset when coming from raw player position
    double adjusted = std::max(0.0, seconds - m_interpStart);

    // Auto-pause at interpretation end
    if (m_interpEnd > 0 && seconds >= m_interpEnd && playerIsPlaying()) {
        if (m_recordTrackingActive) {
            // Finalize recorded data but stay in recording state visually —
            // let the user click to switch to tracking mode
            m_recordTrackingActive = false;
            finalizeRecordedTracking();
            updateTrackingIcon();
        }
        playerPause();
        m_playPauseAction->setText("Play");
        return;
    }

    // Update slider position
    if (!m_sliderDragging) {
        double duration = playerDuration();
        if (duration > 0) {
            int sliderVal = static_cast<int>((adjusted / duration) * m_seekSlider->maximum());
            m_seekSlider->blockSignals(true);
            m_seekSlider->setValue(sliderVal);
            m_seekSlider->blockSignals(false);
        }
    }

    // Update time label
    m_timeLabel->setText(QString("%1 / %2")
        .arg(formatTime(adjusted))
        .arg(formatTime(playerDuration())));

    // Update sync timer -> cursor if tracking is on, auto-scroll, or play mode
    bool shouldTrack = m_trackingAction->isChecked() || m_trackingSettings->autoScrollEnabled() || m_playModeActive;
    if (shouldTrack) {
        m_syncTimer->setTime(adjusted);
        // Verovio: send tick to web view for cursor overlay
        if (m_useVerovio && m_trackingAction->isChecked()) {
            m_syncTimer->setTime(adjusted + 0.08); // forward compensation for IPC latency
            m_scoreWidget->setCursorTick(m_syncTimer->currentTick());
            m_syncTimer->setTime(adjusted);
        }
    }

    // Play mode: auto-advance past tied notes when cursor reaches them
    if (m_playModeActive && playerIsPlaying()) {
        if (m_playAlongSynth->advanceTiedNotes(m_syncTimer->currentTick())) {
            m_scoreWidget->setHighlightElement(m_playAlongSynth->nextNoteElement());
        }
    }

#ifdef USE_MUSESCORE
    // Update sync mode widgets
    if (m_syncMode && m_syncMode->isActive()) {
        m_scoreWidget->setPlaybackTime(seconds);
        if (m_waveformWidget) m_waveformWidget->setPlaybackTime(seconds);

        // Clear selection when playback moves past the selected beat to the next synced one
        int sel = m_scoreWidget->selectedBeatIndex();
        if (sel >= 0 && playerIsPlaying()) {
            const auto& beats = m_syncMode->beats();
            if (beats[sel].synced) {
                // Find next synced beat after sel
                for (int i = sel + 1; i < static_cast<int>(beats.size()); ++i) {
                    if (beats[i].synced) {
                        if (seconds >= beats[i].effectiveTime()) {
                            m_scoreWidget->setSelectedBeat(-1);
                        }
                        break;
                    }
                }
            }
        }
    }
#endif
}

QString App::formatTime(double seconds) const
{
    int totalSecs = static_cast<int>(seconds);
    int mins = totalSecs / 60;
    int secs = totalSecs % 60;
    return QString("%1:%2").arg(mins).arg(secs, 2, 10, QChar('0'));
}

void App::updateTrackingIcon()
{
    if (!m_trackingButton || !m_trackingAction) return;

    bool hasBeatData = !m_syncTimer->beatTimes().empty();

    int sz = 16; // render at 2x for retina
    QPixmap px(sz + 7, sz + 3);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);

    if (m_recordTrackingActive) {
        // Recording: solid red circle
        p.setBrush(Theme::red());
        p.setPen(QPen(Theme::red(), 1.5));
        m_trackingButton->setText("Record");
    } else {
        bool on = m_trackingAction->isChecked() && hasBeatData;
        if (on) {
            p.setBrush(Theme::green());
            p.setPen(QPen(Theme::green(), 1.5));
        } else {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(Theme::textHint(), 1.5));
        }
        m_trackingButton->setText("Tracking");
    }
    p.drawEllipse(QRectF(2, 3, sz - 2, sz - 2));
    px.setDevicePixelRatio(2);
    m_trackingButton->setIcon(QIcon(px));
}

void App::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::ApplicationPaletteChange
        || event->type() == QEvent::PaletteChange
        || event->type() == QEvent::ThemeChange) {
        updateTrackingIcon();
        if (m_partPanel) m_partPanel->applyTheme();
        if (m_displaySettings) m_displaySettings->applyTheme();
        if (m_trackingSettings) m_trackingSettings->applyTheme();
        if (m_scoreWidget) m_scoreWidget->applyTheme();
    }
    QMainWindow::changeEvent(event);
}

// --- Player dispatch helpers ---

void App::playerPlay()
{
    // On first play after entering a level, always seek to interpretation start
    if (m_needsSeekOnPlay || (m_interpStart > 0 && playerCurrentTime() < m_interpStart)) {
        m_needsSeekOnPlay = false;
        double seekTo = m_interpStart;
        if (m_useYouTube && m_youtubePlayer)
            m_youtubePlayer->seekTo(seekTo);
        else
            m_audioPlayer->seekTo(seekTo);
    }
    if (m_useYouTube && m_youtubePlayer)
        m_youtubePlayer->play();
    else
        m_audioPlayer->play();
}

void App::playerPause()
{
    if (m_useYouTube && m_youtubePlayer)
        m_youtubePlayer->pause();
    else
        m_audioPlayer->pause();
}

void App::playerStop()
{
    if (m_useYouTube && m_youtubePlayer)
        m_youtubePlayer->stop();
    else
        m_audioPlayer->stop();
}

void App::playerSeekTo(double seconds)
{
    double raw = seconds + m_interpStart;
    if (m_useYouTube && m_youtubePlayer)
        m_youtubePlayer->seekTo(raw);
    else
        m_audioPlayer->seekTo(raw);
}

double App::playerCurrentTime() const
{
    double raw;
    if (m_useYouTube && m_youtubePlayer)
        raw = m_youtubePlayer->currentTime();
    else
        raw = m_audioPlayer->currentTime();
    return std::max(0.0, raw - m_interpStart);
}

double App::playerDuration() const
{
    double raw;
    if (m_useYouTube && m_youtubePlayer)
        raw = m_youtubePlayer->duration();
    else
        raw = m_audioPlayer->duration();
    double end = (m_interpEnd > 0) ? m_interpEnd : raw;
    return std::max(0.0, end - m_interpStart);
}

bool App::playerIsPlaying() const
{
    if (m_useYouTube && m_youtubePlayer)
        return m_youtubePlayer->isPlaying();
    return m_audioPlayer->isPlaying();
}

void App::loadSources(const QString& jsonPath)
{
    m_sourcesPath = QFileInfo(jsonPath).absoluteFilePath();

    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open sources file:" << jsonPath;
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        qWarning() << "Invalid sources JSON:" << jsonPath;
        return;
    }

    QJsonObject obj = doc.object();
    QDir sourceDir = QFileInfo(jsonPath).absoluteDir();

    m_sourceYouTubeUrls.clear();
    m_sourceLabels.clear();
    m_sourceTunings.clear();
    m_sourceInstrumentVols.clear();
    m_sourceVolumes.clear();
    m_sourceBeatsFiles.clear();
    m_sourceStartTimes.clear();
    m_sourceEndTimes.clear();
    m_sourceAudioFile.clear();

    if (obj.contains("file")) {
        m_sourceAudioFile = sourceDir.absoluteFilePath(obj.value("file").toString());
    }

    // YouTube sources — single string or array of {url, label} objects
    if (obj.contains("youtube")) {
        QJsonValue ytVal = obj.value("youtube");
        if (ytVal.isArray()) {
            QJsonArray ytArr = ytVal.toArray();
            for (const auto& item : ytArr) {
                if (item.isObject()) {
                    QJsonObject ytObj = item.toObject();
                    m_sourceYouTubeUrls.append(ytObj["url"].toString());
                    m_sourceLabels.append(ytObj.value("label").toString("YouTube"));
                    m_sourceTunings.append(ytObj.value("tuning").toDouble(0));
                    m_sourceInstrumentVols.append(ytObj.contains("instrumentVolume") ? ytObj["instrumentVolume"].toInt() : -1);
                    m_sourceVolumes.append(ytObj.contains("volume") ? ytObj["volume"].toInt() : -1);
                    m_sourceBeatsFiles.append(ytObj.contains("beats") ? sourceDir.absoluteFilePath(ytObj["beats"].toString()) : QString());
                    m_sourceStartTimes.append(ytObj.value("start").toDouble(0));
                    m_sourceEndTimes.append(ytObj.value("end").toDouble(0));
                } else if (item.isString()) {
                    m_sourceYouTubeUrls.append(item.toString());
                    m_sourceLabels.append("YouTube");
                    m_sourceTunings.append(0.0);
                    m_sourceInstrumentVols.append(-1);
                    m_sourceVolumes.append(-1);
                    m_sourceBeatsFiles.append(QString());
                    m_sourceStartTimes.append(0);
                    m_sourceEndTimes.append(0);
                }
            }
        } else {
            m_sourceYouTubeUrls.append(ytVal.toString());
            m_sourceLabels.append("YouTube");
            m_sourceTunings.append(0.0);
            m_sourceInstrumentVols.append(-1);
            m_sourceVolumes.append(-1);
            m_sourceBeatsFiles.append(QString());
            m_sourceStartTimes.append(0);
            m_sourceEndTimes.append(0);
        }
    }

    // Use preselected interpretation from the level browser, clamped to valid range
    int sel = m_preselectedInterpretation;
    if (sel < 0 || sel >= m_sourceLabels.size()) sel = 0;

    // Sidebar interpretation combo removed — selection happens in level browser thumbnails.
    m_worldSidebar->clearInterpretations();

    // Apply initial tuning and balance for selected interpretation
    if (sel < m_sourceTunings.size()) {
        double tuning = m_sourceTunings[sel];
        m_playAlongSynth->setPitchOffset(tuning);
        if (m_transposeSpin) {
            m_transposeSpin->blockSignals(true);
            m_transposeSpin->setValue(tuning);
            m_transposeSpin->blockSignals(false);
        }
    }
    if (sel < m_sourceInstrumentVols.size() && m_sourceInstrumentVols[sel] >= 0 && m_instrumentVolSlider) {
        m_instrumentVolSlider->setValue(m_sourceInstrumentVols[sel]);
    }
    if (sel < m_sourceVolumes.size() && m_sourceVolumes[sel] >= 0 && m_worldSidebar) {
        m_worldSidebar->setVolume(m_sourceVolumes[sel]);
    }

    // Apply initial start/end times
    m_activeInterpretation = sel;
    m_interpStart = (sel < m_sourceStartTimes.size()) ? m_sourceStartTimes[sel] : 0;
    m_interpEnd = (sel < m_sourceEndTimes.size()) ? m_sourceEndTimes[sel] : 0;

    // Load per-interpretation beats (overrides section-level beats)
    if (sel < m_sourceBeatsFiles.size() && !m_sourceBeatsFiles[sel].isEmpty()) {
        loadBeatData(m_sourceBeatsFiles[sel]);
        m_trackingButton->setEnabled(true);
    } else if (sel < m_sourceBeatsFiles.size()) {
        if (m_beatDataPath.isEmpty()) {
            m_trackingAction->setChecked(false);
        }
    }

    // Apply tuning for selected interpretation
    if (sel < m_sourceTunings.size()) {
        double tuning = m_sourceTunings[sel];
        m_playAlongSynth->setPitchOffset(tuning);
        if (m_transposeSpin) {
            m_transposeSpin->blockSignals(true);
            m_transposeSpin->setValue(tuning);
            m_transposeSpin->blockSignals(false);
        }
    }

    // Apply instrument volume for selected interpretation
    if (sel < m_sourceInstrumentVols.size() && m_sourceInstrumentVols[sel] >= 0
        && m_instrumentVolSlider) {
        m_instrumentVolSlider->setValue(m_sourceInstrumentVols[sel]);
    }

    // Apply master volume for selected interpretation
    if (sel < m_sourceVolumes.size() && m_sourceVolumes[sel] >= 0) {
        // volume is 0-100, map to 0.0-1.0 for the audio player
    }

    // Auto-load: selected YouTube if present, otherwise file
    // Load as preview (no seek) — seek to interpStart happens on first play
    if (sel < m_sourceYouTubeUrls.size() && !m_sourceYouTubeUrls[sel].isEmpty()) {
        loadYouTube(m_sourceYouTubeUrls[sel], true);
    } else if (!m_sourceAudioFile.isEmpty()) {
        loadAudio(m_sourceAudioFile);
    }
}

void App::loadYouTube(const QString& url, bool /*preview*/)
{
    m_useYouTube = true;

    // Always destroy existing player and create fresh
    if (m_youtubePlayer) {
        m_youtubePlayer->stop();
        m_youtubePlayer->pause();
        auto* oldView = m_youtubePlayer->videoWidget();
        if (oldView) {
            oldView->setParent(nullptr);
            delete oldView;
        }
        delete m_youtubePlayer;
        m_youtubePlayer = nullptr;
    }

    m_currentYoutubeUrl = url;
    m_youtubePlayer = new YouTubePlayer(this);

    // Connect position updates
    connect(m_youtubePlayer, &YouTubePlayer::positionChanged,
            this, &App::onPositionChanged);

    // Connect playback state to toolbar
    connect(m_youtubePlayer, &YouTubePlayer::playbackStarted, [this]() {
        m_playPauseAction->setText("Pause");
        m_scoreWidget->setPlaying(true);
        // Seek to interpretation start if needed (covers clicks on YouTube UI)
        if (m_needsSeekOnPlay || (m_interpStart > 0 && m_youtubePlayer->currentTime() < m_interpStart)) {
            m_needsSeekOnPlay = false;
            m_youtubePlayer->seekTo(m_interpStart);
        }
    });
    connect(m_youtubePlayer, &YouTubePlayer::playbackPaused, [this]() {
        m_playPauseAction->setText("Play");
        m_scoreWidget->setPlaying(false);
    });
    connect(m_youtubePlayer, &YouTubePlayer::playbackStopped, [this]() {
        m_playPauseAction->setText("Play");
        m_scoreWidget->setPlaying(false);
    });

    // When video is ready, set up seek slider and resize to fill container
    connect(m_youtubePlayer, &YouTubePlayer::videoReady, [this](double duration) {
        m_seekSlider->setRange(0, static_cast<int>(duration * 10));
        m_timeLabel->setText(QString("0:00 / %1").arg(formatTime(duration)));
        m_speedButton->setEnabled(m_useYouTube);
        // Resize to fill the expanded container
        if (m_expandedVideoContainer && m_expandedVideoContainer->isVisible()) {
            QSize sz = m_expandedVideoContainer->size();
            if (sz.width() > 0 && sz.height() > 0)
                m_youtubePlayer->resizePlayer(sz.width(), sz.height());
        }
    });

    // Video widget goes directly into the expanded video container
    auto* videoWidget = m_youtubePlayer->videoWidget();
    videoWidget->setMinimumSize(0, 0);
    videoWidget->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    videoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QPalette videoPal = videoWidget->palette();
    videoPal.setColor(QPalette::Window, Qt::black);
    videoWidget->setPalette(videoPal);
    videoWidget->setAutoFillBackground(true);

    // Create expanded container if it doesn't exist yet
    if (!m_expandedVideoContainer) {
        m_expandedVideoContainer = new QWidget();
        m_expandedVideoContainer->setStyleSheet("background: black;");
        m_expandedVideoContainer->hide();
    }

    // Clear any old widgets from the layout, then add new video widget
    if (!m_expandedVideoContainer->layout()) {
        auto* lay = new QVBoxLayout(m_expandedVideoContainer);
        lay->setContentsMargins(0, 0, 0, 0);
    } else {
        // Remove all old items from the layout
        QLayoutItem* item;
        while ((item = m_expandedVideoContainer->layout()->takeAt(0)) != nullptr) {
            delete item;
        }
    }
    videoWidget->setParent(m_expandedVideoContainer);
    m_expandedVideoContainer->layout()->addWidget(videoWidget);

    // Speed button connections
    connect(m_speedButton->menu(), &QMenu::triggered, this, [this](QAction* action) {
        if (m_youtubePlayer)
            m_youtubePlayer->setPlaybackRate(action->data().toDouble());
    });
    connect(m_youtubePlayer, &YouTubePlayer::playbackRateChanged,
            this, [this](double rate) {
        m_speedButton->setText(QString("Speed: %1x").arg(rate));
    });

    m_youtubePlayer->load(url);
}

// toggleVideoExpand removed — video is always expanded in a level, doesn't exist in navigation

void App::enterPlayMode()
{
#ifdef USE_MUSESCORE
    if (m_syncMode && m_syncMode->isActive()) {
        exitSyncMode();
    }
#endif
    m_playModeActive = true;
#ifdef USE_MUSESCORE
    m_partPanel->setPlayModeActive(true);
#endif
    m_scoreWidget->setPlayModeActive(true);
    m_instrumentAction->setEnabled(true);
    updateTrackingIcon(); // show "Record" if no beat data
}

void App::setupInstrumentPanelForVoices(int voiceCount)
{
    // Remove old multi-voice rows
    for (auto* w : m_voiceRows) {
        m_instrumentPanelLayout->removeWidget(w);
        delete w;
    }
    m_voiceRows.clear();
    m_voiceSfCombos.clear();
    m_voiceInstrCombos.clear();

    m_instrumentAction->setText(voiceCount > 1 ? "Instruments" : "Instrument");

    if (voiceCount <= 1) {
        // Single-voice mode: show the default row
        m_singleVoiceRow->show();
        m_instrumentPanel->setFixedHeight(
            m_singleVoiceRow->sizeHint().height() + 40 + 16);
    } else {
        // Multi-voice mode: hide default row, create per-voice rows
        m_singleVoiceRow->hide();

        // Voice colors: voice 0 = blue, voice 1 = violet, etc.
        QList<QColor> voiceColors = {QColor(80, 140, 220), QColor(180, 80, 220)};

        for (int vi = 0; vi < voiceCount; ++vi) {
            auto* row = new QWidget();
            auto* hbox = new QHBoxLayout(row);
            hbox->setContentsMargins(0, 0, 0, 0);
            hbox->setSpacing(16);

            QColor color = (vi < voiceColors.size()) ? voiceColors[vi] : Theme::textPrimary();
            QString colorStyle = QString("color: %1; font-size: 12px; font-weight: bold;").arg(color.name());

            auto* voiceLabel = new QLabel(QString("Voice %1").arg(vi + 1));
            voiceLabel->setStyleSheet(colorStyle);
            voiceLabel->setFixedWidth(50);
            hbox->addWidget(voiceLabel);

            auto* sfLabel = new QLabel("Soundfont");
            sfLabel->setStyleSheet(colorStyle);
            hbox->addWidget(sfLabel);

            auto* sfCombo = new QComboBox();
            sfCombo->setMinimumWidth(160);
            for (const auto& path : m_soundfontPaths) {
                QString fn = QFileInfo(path).fileName();
                sfCombo->addItem(fn.left(fn.lastIndexOf('.')));
            }
            hbox->addWidget(sfCombo);

            hbox->addSpacing(16);

            auto* instrLabel = new QLabel("Instrument");
            instrLabel->setStyleSheet(colorStyle);
            hbox->addWidget(instrLabel);

            auto* instrCombo = new QComboBox();
            instrCombo->setMinimumWidth(180);
            hbox->addWidget(instrCombo);
            hbox->addStretch();

            m_voiceSfCombos.append(sfCombo);
            m_voiceInstrCombos.append(instrCombo);
            m_voiceRows.append(row);

            // Append after the controls row (controls are at the top)
            m_instrumentPanelLayout->addWidget(row);

            // Connect soundfont combo
            connect(sfCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this, vi, instrCombo](int idx) {
                if (idx < 0 || idx >= m_soundfontPaths.size()) return;
                instrCombo->setEnabled(false);
                instrCombo->clear();
                instrCombo->addItem("Loading...");
                QTimer::singleShot(0, this, [this, vi, idx, instrCombo]() {
                    m_playAlongSynth->setSoundfontForVoice(vi, m_soundfontPaths[idx]);
                    int sfId = m_playAlongSynth->soundfontIdForVoice(vi);
                    int curProg = m_playAlongSynth->gmProgramForVoice(vi);
                    instrCombo->blockSignals(true);
                    instrCombo->clear();
                    auto presets = m_playAlongSynth->presetsForSoundfont(sfId);
                    for (const auto& p : presets) {
                        instrCombo->addItem(p.second, p.first);
                    }
                    for (int i = 0; i < instrCombo->count(); ++i) {
                        if (instrCombo->itemData(i).toInt() == curProg) {
                            instrCombo->setCurrentIndex(i);
                            break;
                        }
                    }
                    instrCombo->blockSignals(false);
                    instrCombo->setEnabled(true);
                });
            });

            // Connect instrument combo
            connect(instrCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this, vi, instrCombo](int idx) {
                if (idx >= 0) {
                    m_playAlongSynth->setGmProgramForVoice(vi, instrCombo->itemData(idx).toInt());
                }
            });
        }

        // Height: controls row (~36) + voice rows (28 each) + margins
        m_instrumentPanel->setFixedHeight(36 + 28 * voiceCount + 16);
    }
}

void App::startRecordTracking()
{
    m_recordTrackingActive = true;
    m_recordedNotes.clear();
    m_beatDataFromRecording = false;
    updateTrackingIcon();
    // Auto-restart playback from the beginning
    playerSeekTo(m_interpStart);
    m_playAlongSynth->resetPosition();
    if (m_multiVoice) {
        if (m_playAlongSynth->voiceCount() > 0)
            m_scoreWidget->setHighlightElement(m_playAlongSynth->nextNoteElementForVoice(0));
        if (m_playAlongSynth->voiceCount() > 1)
            m_scoreWidget->setHighlightElement2(m_playAlongSynth->nextNoteElementForVoice(1));
    } else {
        m_scoreWidget->setHighlightElement(m_playAlongSynth->nextNoteElement());
    }
    m_syncTimer->setTime(0);
    onPositionChanged(0);
    playerPlay();
    m_playPauseAction->setText("Pause");
}

void App::stopRecordTracking()
{
    m_recordTrackingActive = false;
    finalizeRecordedTracking();
    updateTrackingIcon();
    // Auto-enable tracking if we got enough data
    if (!m_syncTimer->beatTimes().empty()) {
        m_trackingAction->setChecked(true);
        // Restart playback to test
        playerSeekTo(m_interpStart);
        m_playAlongSynth->resetPosition();
        if (m_multiVoice) {
            if (m_playAlongSynth->voiceCount() > 0)
                m_scoreWidget->setHighlightElement(m_playAlongSynth->nextNoteElementForVoice(0));
            if (m_playAlongSynth->voiceCount() > 1)
                m_scoreWidget->setHighlightElement2(m_playAlongSynth->nextNoteElementForVoice(1));
        } else {
            m_scoreWidget->setHighlightElement(m_playAlongSynth->nextNoteElement());
        }
        m_syncTimer->setTime(0);
        onPositionChanged(0);
    }
}

void App::finalizeRecordedTracking()
{
    if (m_recordedNotes.size() < 2) {
        m_recordedNotes.clear();
        return;
    }

    // Sort by tick
    std::sort(m_recordedNotes.begin(), m_recordedNotes.end(),
              [](const RecordedNote& a, const RecordedNote& b) {
                  return a.tick < b.tick;
              });

    // Remove duplicate ticks (keep first)
    m_recordedNotes.erase(
        std::unique(m_recordedNotes.begin(), m_recordedNotes.end(),
                    [](const RecordedNote& a, const RecordedNote& b) {
                        return a.tick == b.tick;
                    }),
        m_recordedNotes.end());

    if (m_recordedNotes.size() < 2) {
        m_recordedNotes.clear();
        return;
    }

    // Build beat data arrays
    std::vector<double> beatTimes;
    std::vector<int> beatTicks;
    beatTimes.reserve(m_recordedNotes.size());
    beatTicks.reserve(m_recordedNotes.size());

    for (const auto& rn : m_recordedNotes) {
        beatTimes.push_back(rn.wallTime);
        beatTicks.push_back(rn.tick);
    }

    // Compute measure starts by interpolating score measure boundaries
    std::vector<double> measureStarts;
#ifdef USE_MUSESCORE
    if (m_score) {
        for (auto* measure = m_score->firstMeasure(); measure; measure = measure->nextMeasure()) {
            int mTick = measure->tick().ticks();
            // Only include measures within our recorded range
            if (mTick < beatTicks.front() || mTick > beatTicks.back()) continue;
            // Interpolate time for this tick
            auto it = std::lower_bound(beatTicks.begin(), beatTicks.end(), mTick);
            if (it == beatTicks.end()) continue;
            size_t idx = static_cast<size_t>(it - beatTicks.begin());
            if (*it == mTick) {
                measureStarts.push_back(beatTimes[idx]);
            } else if (idx > 0) {
                double t0 = beatTimes[idx - 1];
                double t1 = beatTimes[idx];
                int tk0 = beatTicks[idx - 1];
                int tk1 = beatTicks[idx];
                double frac = (tk1 > tk0) ? double(mTick - tk0) / double(tk1 - tk0) : 0.0;
                measureStarts.push_back(t0 + frac * (t1 - t0));
            }
        }
    }
#endif

    // Feed into SyncTimer
    m_syncTimer->setBeatTimes(beatTimes, 0);
    m_syncTimer->setBeatTicks(beatTicks);
    m_syncTimer->setMeasureStarts(measureStarts);

    m_beatDataFromRecording = true;
    m_recordedNotes.clear();

    qDebug() << "Record tracking: finalized" << beatTimes.size() << "control points,"
             << measureStarts.size() << "measure starts";
}

void App::saveRecordedTracking()
{
    if (!m_beatDataFromRecording) return;

    const auto& beatTimes = m_syncTimer->beatTimes();
    const auto& beatTicks = m_syncTimer->beatTicks();
    const auto& measureStarts = m_syncTimer->measureStarts();

    if (beatTimes.empty()) return;

    // Build JSON with control_points format (preserves tick positions)
    QJsonArray cpArray;
    for (size_t i = 0; i < beatTimes.size(); ++i) {
        QJsonObject pt;
        pt["tick"] = (i < beatTicks.size()) ? beatTicks[i] : 0;
        pt["time"] = beatTimes[i];
        cpArray.append(pt);
    }

    QJsonArray msArray;
    for (double t : measureStarts)
        msArray.append(t);

    QJsonObject root;
    root["control_points"] = cpArray;
    root["measure_starts"] = msArray;

    // Determine output directory and filename
    QDir outDir;
    if (!m_sourcesPath.isEmpty()) {
        outDir = QFileInfo(m_sourcesPath).absoluteDir();
    } else if (!m_beatDataPath.isEmpty()) {
        outDir = QFileInfo(m_beatDataPath).absoluteDir();
    } else {
        qWarning() << "No sources path — cannot save tracking";
        return;
    }

    // Filename: beatdata_<label>.json (sanitised)
    QString label;
    if (m_activeInterpretation >= 0 && m_activeInterpretation < m_sourceLabels.size())
        label = m_sourceLabels[m_activeInterpretation];
    if (label.isEmpty()) label = "recorded";
    label = label.toLower().replace(QRegularExpression("[^a-z0-9]+"), "_").replace(QRegularExpression("^_|_$"), "");
    QString filename = QString("beatdata_%1.json").arg(label);
    QString filePath = outDir.absoluteFilePath(filename);

    // Write beat data file
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to write beat data:" << filePath;
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    qDebug() << "Saved beat data:" << filePath;

    // Update sources.json
    if (!m_sourcesPath.isEmpty()) {
        QFile srcFile(m_sourcesPath);
        if (srcFile.open(QIODevice::ReadOnly)) {
            QJsonDocument srcDoc = QJsonDocument::fromJson(srcFile.readAll());
            srcFile.close();

            if (srcDoc.isObject()) {
                QJsonObject srcObj = srcDoc.object();
                QString relPath = QDir(QFileInfo(m_sourcesPath).absolutePath()).relativeFilePath(filePath);
                if (!relPath.startsWith("./")) relPath = "./" + relPath;

                if (srcObj.contains("youtube") && srcObj["youtube"].isArray()) {
                    QJsonArray ytArr = srcObj["youtube"].toArray();
                    if (m_activeInterpretation >= 0 && m_activeInterpretation < ytArr.size()) {
                        QJsonObject interp = ytArr[m_activeInterpretation].toObject();
                        interp["beats"] = relPath;
                        ytArr[m_activeInterpretation] = interp;
                        srcObj["youtube"] = ytArr;
                    }
                }

                QFile outFile(m_sourcesPath);
                if (outFile.open(QIODevice::WriteOnly)) {
                    // Re-read to preserve unknown keys; just write the modified object
                    outFile.write(QJsonDocument(srcObj).toJson(QJsonDocument::Indented));
                    outFile.close();
                    qDebug() << "Updated sources.json:" << m_sourcesPath;
                }
            }
        }
    }

    // Update in-memory state
    if (m_activeInterpretation >= 0 && m_activeInterpretation < m_sourceBeatsFiles.size())
        m_sourceBeatsFiles[m_activeInterpretation] = filePath;

    m_beatDataFromRecording = false;
    m_beatDataPath = filePath;
    updateTrackingIcon();
}

void App::exitPlayMode()
{
    // Stop recording if active
    if (m_recordTrackingActive) {
        m_recordTrackingActive = false;
        finalizeRecordedTracking();
    }
    m_playModeActive = false;
    m_beatDataFromRecording = false;
    m_playAlongSynth->stopNote();
    m_scoreWidget->setHighlightElement(nullptr);
    m_scoreWidget->setHighlightElement2(nullptr);
    m_scoreWidget->setPlayModeActive(false);
#ifdef USE_MUSESCORE
    m_partPanel->setPlayModeActive(false);
#endif
    m_keysHeld = 0;
    m_multiVoice = false;
    m_voiceKeysHeld.clear();
    m_voiceKeyZones.clear();
    setupInstrumentPanelForVoices(1); // reset to single-voice layout
    m_instrumentAction->setChecked(false);
    m_instrumentAction->setEnabled(false);
}

#ifdef USE_MUSESCORE
void App::enterSyncMode()
{
    if (!m_score || !m_renderer) return;

    if (m_playModeButton->isChecked()) {
        m_playModeButton->setChecked(false);
    }

    // Save sidebar state, close it, and disable the button
    m_savedSidebarVisible = m_sidebarWidget->isVisible();
    setSidebarVisible(false);
    m_sidebarAction->blockSignals(true);
    m_sidebarAction->setChecked(false);
    m_sidebarAction->blockSignals(false);
    m_sidebarAction->setEnabled(false);

    // Save original CLI tracking data and auto-scroll state
    m_savedTrackingOn = m_trackingAction->isChecked();
    m_savedAutoScroll = m_trackingSettings->autoScrollEnabled();
    m_trackingSettings->setAutoScrollEnabled(false);
    m_scoreWidget->setAutoScrollEnabled(false);
    m_savedBeatTimes = m_syncTimer->beatTimes();
    m_savedBeatTicks = m_syncTimer->beatTicks();
    m_savedMeasureStarts = m_syncTimer->measureStarts();
    m_savedBeatsPerMeasure = m_syncTimer->beatsPerMeasure();

    // Switch to file source if currently using YouTube (without auto-playing)
    if (m_useYouTube && !m_sourceAudioFile.isEmpty()) {
        selectFileSource();
    }

    m_syncMode->enter(m_score, m_renderer.get());

    // Load sync state: prefer saved in-session state, then CLI file data
    if (!m_savedSyncState.isEmpty()) {
        m_syncMode->fromJson(m_savedSyncState);
    } else if (!m_beatDataPath.isEmpty()) {
        QFile f(m_beatDataPath);
        if (f.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            if (doc.isObject()) {
                m_syncMode->fromJson(doc.object());
            }
        }
    }

    // Feed sync beat data to SyncTimer for tracking
    updateSyncTimerFromSyncMode();

    // Show waveform
    if (m_waveformWidget) {
        if (!m_audioFilePath.isEmpty()) {
            m_waveformWidget->loadAudio(m_audioFilePath);
        }
        m_waveformWidget->setSyncMode(m_syncMode);
        m_waveformWidget->setDuration(playerDuration());
        m_waveformWidget->show();

        // Set initial waveform zoom based on score tempo so dots are well-spaced
        double duration = playerDuration();
        if (duration > 0 && m_score->tempomap()) {
            double bps = m_score->tempomap()->tempo(0).val; // beats per second
            double bpm = bps * 60.0;
            if (bpm > 0) {
                int totalBeats = m_syncMode->totalBeats();
                int vpWidth = m_waveformWidget->viewport()->width();
                if (vpWidth <= 0) vpWidth = 800;
                double targetSpacing = 40.0; // pixels between dots
                double zoom = (totalBeats * targetSpacing) / vpWidth;
                if (zoom < 1.0) zoom = 1.0;
                m_waveformWidget->setWaveformZoom(zoom);
            }
        }

        connect(m_waveformWidget, &WaveformWidget::beatTimeChanged, this, [this](int, double) {
            m_scoreWidget->widget()->update();
        }, Qt::UniqueConnection);
    }

    // Setup and show sync sidebar
    setupSyncSidebar();
    m_syncPanel->setSyncMode(m_syncMode);
    m_syncPanel->setBeatDataPath(m_beatDataPath);
    m_syncSidebarWidget->show();
    m_syncSidebarHandle->show();
    repositionSyncSidebar();

    int scrollbarW = m_scoreWidget->verticalScrollBar()->sizeHint().width();
    int sidebarTotal = m_sidebarWidth + scrollbarW;
    m_scoreWidget->setOverlayWidth(sidebarTotal);

    // Keep waveform from going under the sync sidebar
    if (m_waveformWidget) {
        m_waveformWidget->setRightMargin(sidebarTotal);
    }

    m_scoreWidget->setSyncMode(m_syncMode);

    // Fit score after sidebar overlay is set so available width is correct
    QTimer::singleShot(0, this, [this]() {
        m_scoreWidget->zoomToFit();
        int next = m_syncMode->nextUnsyncedBeat();
        if (next >= 0) m_scoreWidget->ensureBeatVisible(next);
    });

    connect(m_scoreWidget, &ScoreWidget::beatClicked, m_syncPanel, &SyncPanel::showBeatInfo,
            Qt::UniqueConnection);

    connect(m_syncPanel, &SyncPanel::beatTimeChanged, this, [this](int beatIndex, double newTime) {
        if (!m_syncMode) return;
        const auto& beat = m_syncMode->beats()[beatIndex];
        double delta = newTime - beat.effectiveTime();
        m_syncMode->adjustBeat(beatIndex, delta);
        m_scoreWidget->widget()->update();
        m_syncPanel->showBeatInfo(beatIndex);
    }, Qt::UniqueConnection);

    connect(m_scoreWidget, &ScoreWidget::beatDoubleClicked, this, [this](int beatIndex) {
        if (!m_syncMode) return;
        m_syncMode->setNextUnsyncedFrom(beatIndex);
        m_syncPanel->showBeatInfo(beatIndex);
    }, Qt::UniqueConnection);

    // Waveform zoom: sync panel buttons → waveform, Cmd+scroll → sync panel label
    if (m_waveformWidget) {
        connect(m_syncPanel, &SyncPanel::waveformZoomRequested, m_waveformWidget, &WaveformWidget::setWaveformZoom,
                Qt::UniqueConnection);
        connect(m_waveformWidget, &WaveformWidget::zoomChanged, m_syncPanel, &SyncPanel::setWaveformZoom,
                Qt::UniqueConnection);

        // Waveform beat click also shows info in sync panel
        connect(m_waveformWidget, &WaveformWidget::beatClicked, m_syncPanel, &SyncPanel::showBeatInfo,
                Qt::UniqueConnection);

        // Spectrogram toggle
        connect(m_syncPanel, &SyncPanel::spectrogramToggled, m_waveformWidget, &WaveformWidget::setShowSpectrogram,
                Qt::UniqueConnection);
    }
}
#endif // USE_MUSESCORE — enterSyncMode

#ifdef USE_MUSESCORE
void App::exitSyncMode()
{
    // Save sync state before exiting
    m_savedSyncState = m_syncMode->toJson();

    m_scoreWidget->setSyncMode(nullptr);
    m_syncMode->exit();

    // Hide waveform
    if (m_waveformWidget) {
        m_waveformWidget->hide();
        m_waveformWidget->setSyncMode(nullptr);
        m_waveformWidget->setRightMargin(0);
    }

    // Hide sync sidebar
    if (m_syncSidebarWidget) {
        m_syncSidebarWidget->hide();
        m_syncSidebarHandle->hide();
    }

    // Restore original CLI tracking data
    m_syncTimer->setBeatTimes(m_savedBeatTimes, m_savedBeatsPerMeasure);
    m_syncTimer->setBeatTicks(m_savedBeatTicks);
    m_syncTimer->setMeasureStarts(m_savedMeasureStarts);
    m_syncTimer->setMeasureIndices({}); // CLI data uses contiguous indices

    // Restore tracking, auto-scroll, and refresh cursor to current time
    m_trackingButton->setEnabled(true);
    m_trackingSettings->setAutoScrollEnabled(m_savedAutoScroll);
    m_scoreWidget->setAutoScrollEnabled(m_savedAutoScroll);
    if (m_savedTrackingOn) {
        m_trackingAction->setChecked(true);
        m_syncTimer->setTime(playerCurrentTime());
    }

    // Restore sidebar
    m_sidebarAction->setEnabled(true);
    if (m_savedSidebarVisible) {
        m_sidebarAction->blockSignals(true);
        m_sidebarAction->setChecked(true);
        m_sidebarAction->blockSignals(false);
        setSidebarVisible(true);
    } else {
        m_scoreWidget->setOverlayWidth(0);
    }
}
#endif // USE_MUSESCORE — exitSyncMode

#ifdef USE_MUSESCORE
void App::setupSyncSidebar()
{
    if (m_syncSidebarWidget) return; // already created

    m_syncSidebarWidget = new QWidget(this);
    m_syncSidebarWidget->setAutoFillBackground(true);
    m_syncSidebarWidget->setFocusPolicy(Qt::ClickFocus);

    auto* layout = new QVBoxLayout(m_syncSidebarWidget);
    layout->setContentsMargins(2, 0, 0, 0);

    m_syncPanel = new SyncPanel();
    auto* syncSection = new CollapsibleSection("Sync", m_syncPanel);
    layout->addWidget(syncSection);

    auto* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    layout->addWidget(spacer);

    connect(m_syncPanel, &SyncPanel::saveRequested, this, &App::saveSyncData);
    connect(m_syncPanel, &SyncPanel::newSyncRequested, this, [this]() {
        // Re-enter sync mode to clear all data
        if (m_syncMode->isActive()) {
            m_syncMode->exit();
            m_syncMode->enter(m_score, m_renderer.get());
            m_syncPanel->setSyncMode(m_syncMode);
            m_syncPanel->updateStatus();
            m_scoreWidget->setSyncMode(m_syncMode);
            m_scoreWidget->setScore(m_score);
            if (m_waveformWidget) m_waveformWidget->setSyncMode(m_syncMode);
            updateSyncTimerFromSyncMode();
        }
    });
    connect(m_syncPanel, &SyncPanel::reloadRequested, this, [this]() {
        if (!m_syncMode->isActive() || m_beatDataPath.isEmpty()) return;
        QFile f(m_beatDataPath);
        if (!f.open(QIODevice::ReadOnly)) return;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject()) return;
        // Re-enter and load from file
        m_syncMode->exit();
        m_syncMode->enter(m_score, m_renderer.get());
        m_syncMode->fromJson(doc.object());
        m_syncPanel->setSyncMode(m_syncMode);
        m_syncPanel->updateStatus();
        m_scoreWidget->setSyncMode(m_syncMode);
        m_scoreWidget->setScore(m_score);
        if (m_waveformWidget) m_waveformWidget->setSyncMode(m_syncMode);
        updateSyncTimerFromSyncMode();
    });

    // Drag handle
    m_syncSidebarHandle = new QWidget(this);
    m_syncSidebarHandle->setFixedWidth(5);
    m_syncSidebarHandle->setCursor(Qt::SplitHCursor);
    m_syncSidebarHandle->hide();
    m_syncSidebarWidget->hide();
}
#endif // USE_MUSESCORE — setupSyncSidebar

#ifdef USE_MUSESCORE
void App::repositionSyncSidebar()
{
    if (!m_syncSidebarWidget || !m_syncSidebarWidget->isVisible()) return;
    QRect cr = centralWidget()->geometry();
    int scrollbarW = m_scoreWidget->verticalScrollBar()->sizeHint().width();
    int x = cr.right() - m_sidebarWidth - scrollbarW + 1;
    m_syncSidebarWidget->setGeometry(x, cr.top(), m_sidebarWidth, cr.height());
    m_syncSidebarHandle->setGeometry(x - 5, cr.top(), 5, cr.height());
    m_syncSidebarWidget->raise();
    m_syncSidebarHandle->raise();
}
#endif // USE_MUSESCORE — repositionSyncSidebar

#ifdef USE_MUSESCORE
void App::saveSyncData()
{
    if (!m_syncMode->isActive() || m_beatDataPath.isEmpty()) return;

    QJsonObject obj = m_syncMode->toJson();
    QJsonDocument doc(obj);

    QFile file(m_beatDataPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        qDebug() << "Saved sync data to" << m_beatDataPath;
    }
}
#endif // USE_MUSESCORE — saveSyncData

#ifdef USE_MUSESCORE
void App::updateSyncTimerFromSyncMode()
{
    if (!m_syncMode || !m_syncMode->isActive()) return;

    const auto& beats = m_syncMode->beats();
    const auto& bpmVec = m_syncMode->beatsPerMeasure();
    std::vector<double> beatTimes;
    std::vector<int> beatTicks;
    std::vector<double> measureStarts;
    std::vector<int> measureIndices;

    int beatIdx = 0;
    for (size_t mi = 0; mi < bpmVec.size(); ++mi) {
        bool firstInMeasure = true;
        for (int b = 0; b < bpmVec[mi] && beatIdx < static_cast<int>(beats.size()); ++b, ++beatIdx) {
            if (beats[beatIdx].synced) {
                beatTimes.push_back(beats[beatIdx].effectiveTime());
                beatTicks.push_back(beatIdx * 480);
                if (firstInMeasure) {
                    measureStarts.push_back(beats[beatIdx].effectiveTime());
                    measureIndices.push_back(static_cast<int>(mi));
                    firstInMeasure = false;
                }
            }
        }
    }

    int beatsPerMeasure = bpmVec.empty() ? 3 : bpmVec[0];
    m_syncTimer->setBeatTimes(beatTimes, beatsPerMeasure);
    m_syncTimer->setBeatTicks(beatTicks);
    m_syncTimer->setMeasureStarts(measureStarts);
    m_syncTimer->setMeasureIndices(measureIndices);
}
#endif // USE_MUSESCORE — updateSyncTimerFromSyncMode

void App::loadWorlds(const QString& worldsDir)
{
    m_worlds = scoretracker::loadWorlds(worldsDir);
    m_worldSidebar->setWorlds(m_worlds);
    // No auto-load of video at startup — video is created fresh when entering a level
}

void App::showWorldBrowser()
{
    if (m_playModeActive) {
        m_playModeButton->setChecked(false);
    }
    // Kill YouTube player — stop audio, destroy widget, clean up
    if (m_youtubePlayer) {
        m_youtubePlayer->stop();
        m_youtubePlayer->pause();
        // Remove the video widget from the container before deleting
        if (m_expandedVideoContainer && m_expandedVideoContainer->layout()) {
            QLayoutItem* item;
            while ((item = m_expandedVideoContainer->layout()->takeAt(0)) != nullptr)
                delete item;
        }
        auto* view = m_youtubePlayer->videoWidget();
        if (view) {
            view->setParent(nullptr);
            delete view;
        }
        delete m_youtubePlayer;
        m_youtubePlayer = nullptr;
        m_currentYoutubeUrl.clear();
        m_useYouTube = false;
        m_speedButton->setEnabled(false);
        m_speedButton->setText("Speed: 1x");
    }
    if (m_expandedVideoContainer) {
        m_expandedVideoContainer->hide();
        m_expandedVideoContainer->setParent(nullptr);
    }
    m_videoExpanded = false;

    // Reset score cursor immediately so it doesn't flash at old position on next level
    m_scoreWidget->setCursorRect(QRectF(), -1);
    m_scoreWidget->setCursorVisible(false);
    m_scoreWidget->scrollToTop();
    if (m_useVerovio)
        m_scoreWidget->runWebJavaScript("hideCursor()");
    m_syncTimer->setTime(0);

    // Show the active world in the level browser
    if (m_activeWorldIndex >= 0 && m_activeWorldIndex < m_worlds.size()) {
        m_currentWorldIndex = m_activeWorldIndex;
        m_levelBrowser->setSelectedInterpretation(m_preselectedInterpretation);
        m_levelBrowser->setWorld(m_worlds[m_activeWorldIndex]);
        m_levelBrowser->setCurrentLevel(m_activeSectionIndex, m_activeLevelIndex);
    }
    m_centralStack->setCurrentIndex(0);
    m_toolbar->hide();
    m_worldSidebar->clearInterpretations();
}

void App::switchInterpretation(int index)
{
    // Lightweight interpretation switch — same level, same score, just different
    // YouTube video + beat data + tuning. No score reload, no re-entrancy issues.
    m_preselectedInterpretation = index;
    m_activeInterpretation = index;
    m_levelBrowser->setSelectedInterpretation(index);

    // Stop play-along and reset UI
    m_playAlongSynth->stopNote();
    m_playAlongSynth->resetPosition();
    m_keysHeld = 0;
    m_scoreWidget->setCursorRect(QRectF(), -1);
    m_scoreWidget->setCursorVisible(false);
    m_scoreWidget->setHighlightElement(nullptr);
    m_scoreWidget->setHighlightElement2(nullptr);
    m_scoreWidget->scrollToTop();
    if (m_useVerovio)
        m_scoreWidget->runWebJavaScript("hideCursor()");
    m_syncTimer->setTime(0);
    m_seekSlider->setValue(0);
    m_timeLabel->setText("0:00");
    m_playPauseAction->setText("Play");
    m_scoreWidget->setPlaying(false);
    m_needsSeekOnPlay = true;

    // Apply new interpretation's settings
    m_interpStart = (index < m_sourceStartTimes.size()) ? m_sourceStartTimes[index] : 0;
    m_interpEnd = (index < m_sourceEndTimes.size()) ? m_sourceEndTimes[index] : 0;

    if (index < m_sourceTunings.size())
        m_playAlongSynth->setPitchOffset(m_sourceTunings[index]);
    if (index < m_sourceInstrumentVols.size() && m_sourceInstrumentVols[index] >= 0 && m_instrumentVolSlider)
        m_instrumentVolSlider->setValue(m_sourceInstrumentVols[index]);

    // Load per-interpretation beat data
    if (index < m_sourceBeatsFiles.size() && !m_sourceBeatsFiles[index].isEmpty()) {
        loadBeatData(m_sourceBeatsFiles[index]);
        m_trackingAction->setEnabled(true);
        m_trackingButton->setEnabled(true);
        m_trackingAction->setChecked(true);
        m_scoreWidget->setCursorVisible(true);
    } else {
        m_syncTimer->setBeatTimes({}, 3);
        m_syncTimer->setBeatTicks({});
        m_trackingAction->setChecked(false);
        m_trackingAction->setEnabled(false);
        m_trackingButton->setEnabled(false);
        m_scoreWidget->setCursorVisible(false);
    }

    // Load new YouTube video (destroys old player, creates fresh)
    if (index < m_sourceYouTubeUrls.size() && !m_sourceYouTubeUrls[index].isEmpty()) {
        // Destroy old player
        if (m_youtubePlayer) {
            m_youtubePlayer->stop();
            auto* view = m_youtubePlayer->videoWidget();
            if (view) { view->setParent(nullptr); delete view; }
            delete m_youtubePlayer;
            m_youtubePlayer = nullptr;
        }
        m_currentYoutubeUrl.clear();
        loadYouTube(m_sourceYouTubeUrls[index]);
    }

    // Update interpretation menu checkmarks
    if (m_interpMenu) {
        auto actions = m_interpMenu->actions();
        for (int j = 0; j < actions.size(); ++j)
            actions[j]->setChecked(j == index);
    }
}

void App::loadLevel(int worldIndex, int sectionIndex, int levelIndex)
{
    if (worldIndex < 0 || worldIndex >= m_worlds.size()) return;
    const auto& world = m_worlds[worldIndex];
    if (sectionIndex < 0 || sectionIndex >= world.sections.size()) return;
    const auto& section = world.sections[sectionIndex];
    if (levelIndex < 0 || levelIndex >= section.levels.size()) return;

    // Kill any existing YouTube player — stop audio first, then destroy
    if (m_youtubePlayer) {
        m_youtubePlayer->stop();
        m_youtubePlayer->pause();
        if (m_expandedVideoContainer && m_expandedVideoContainer->layout()) {
            QLayoutItem* item;
            while ((item = m_expandedVideoContainer->layout()->takeAt(0)) != nullptr)
                delete item;
        }
        auto* view = m_youtubePlayer->videoWidget();
        if (view) {
            view->setParent(nullptr);
            delete view;
        }
        delete m_youtubePlayer;
        m_youtubePlayer = nullptr;
        m_speedButton->setEnabled(false);
        m_speedButton->setText("Speed: 1x");
    }
    m_currentYoutubeUrl.clear();
    m_useYouTube = false;
    // Keep the expanded container in place (black background) if already visible,
    // so the video area doesn't disappear/reappear during interpretation switch.
    if (!m_videoExpanded && m_expandedVideoContainer) {
        m_expandedVideoContainer->hide();
        m_expandedVideoContainer->setParent(nullptr);
    }
    m_videoExpanded = false;
    // Process pending deletions so Chromium releases resources before new player
    // NOTE: Do NOT call QApplication::processEvents() here — it causes re-entrancy.

    if (m_audioPlayer)
        m_audioPlayer->pause();
    if (m_useVerovio)
        m_scoreWidget->runWebJavaScript("hideCursor()");
    m_scoreWidget->setCursorRect(QRectF(), -1);
    m_scoreWidget->setCursorVisible(false);
    m_scoreWidget->scrollToTop();
    m_scoreWidget->setHighlightElement(nullptr);
    m_scoreWidget->setHighlightElement2(nullptr);
    m_syncTimer->setBeatTimes({}, 3);
    m_syncTimer->setBeatTicks({});
    m_syncTimer->setTime(0);
    m_needsSeekOnPlay = true;

    // Reset toolbar
    m_seekSlider->setValue(0);
    m_timeLabel->setText("0:00");
    m_playPauseAction->setText("Play");
    m_scoreWidget->setPlaying(false);

    // Exit current play mode if active
    if (m_playModeActive) {
        m_playModeButton->setChecked(false);
    }

    // Full play-along reset — deterministic clean slate for every level
    m_vrvVoices.clear();
    m_playAlongSynth->clearVoices();
    m_playAlongSynth->resetPosition();
    m_keysHeld = 0;
    m_multiVoice = false;
    m_voiceKeysHeld.clear();
    m_voiceKeyZones.clear();

    // Save window geometry before the heavy load
    QRect savedGeometry = geometry();

    // Show loading overlay on the level browser
    m_levelBrowser->showLoading(true);
    // NOTE: Do NOT call QApplication::processEvents() here — it causes re-entrancy.

    // Defer the heavy work so the loading overlay paints first
    QTimer::singleShot(0, this, [=]() {
        const auto& lvlSection = m_worlds[worldIndex].sections[sectionIndex];
        const auto& level = lvlSection.levels[levelIndex];

        // Load the score
        if (!lvlSection.scorePath.isEmpty()) {
            loadScore(lvlSection.scorePath);
        }

        // Load beat data
        if (!lvlSection.beatsPath.isEmpty()) {
            loadBeatData(lvlSection.beatsPath);
        }

        // Load audio sources
        if (!lvlSection.sourcesPath.isEmpty()) {
            loadSources(lvlSection.sourcesPath);
        }

        // Disable tracking and hide cursor if no beat data available
        if (m_syncTimer->beatTimes().empty()) {
            m_trackingAction->setChecked(false);
            m_trackingAction->setEnabled(false);
            m_trackingButton->setEnabled(false);
            m_scoreWidget->setCursorVisible(false);
            if (m_useVerovio)
                m_scoreWidget->runWebJavaScript("hideCursor()");
        } else {
            m_trackingAction->setEnabled(true);
            m_trackingButton->setEnabled(true);
            m_trackingAction->setChecked(true);
            m_scoreWidget->setCursorVisible(true);
        }

        // Hide the right sidebar by default for levels
        m_sidebarAction->setChecked(false);

        // Video starts expanded in the level — insert container into splitter
        if (m_expandedVideoContainer) {
            // Insert at index 1 (after waveform, before score)
            m_centralSplitter->insertWidget(1, m_expandedVideoContainer);
            m_expandedVideoContainer->show();
            m_expandedVideoContainer->installEventFilter(this);

            int videoH = 394;
            // YouTube ToS: minimum 200x200. Prevent user from shrinking below initial height.
            m_expandedVideoContainer->setMinimumHeight(videoH);
            QList<int> sizes;
            for (int i = 0; i < m_centralSplitter->count(); ++i) {
                if (m_centralSplitter->widget(i) == m_expandedVideoContainer)
                    sizes.append(videoH);
                else if (m_centralSplitter->widget(i) == m_scoreWidget)
                    sizes.append(m_centralSplitter->height() - videoH);
                else
                    sizes.append(m_centralSplitter->widget(i)->height());
            }
            m_centralSplitter->setSizes(sizes);
            m_centralSplitter->setStretchFactor(1, 0);
            m_centralSplitter->setStretchFactor(2, 1);

            // Resize iframe after layout settles
            QTimer::singleShot(50, this, [this]() {
                if (m_expandedVideoContainer && m_youtubePlayer) {
                    QSize sz = m_expandedVideoContainer->size();
                    if (sz.width() > 0 && sz.height() > 0)
                        m_youtubePlayer->resizePlayer(sz.width(), sz.height());
                }
            });
        }
        m_videoExpanded = true;

        // Mark this level as active
        m_activeWorldIndex = worldIndex;
        m_activeSectionIndex = sectionIndex;
        m_activeLevelIndex = levelIndex;
        m_levelBrowser->setCurrentLevel(sectionIndex, levelIndex);

        // Switch to score view
        m_centralStack->setCurrentIndex(1);
        m_toolbar->show();

        // Enter play mode (block partPanel signals so auto-select doesn't
        // override the level's instrument setup below)
        m_partPanel->blockSignals(true);
        m_playModeButton->setChecked(true);
        m_partPanel->blockSignals(false);

#ifdef USE_MUSESCORE
        // Part map used by MuseScore path for voice setup and staff reordering
        std::map<int, Part*> origPartMap;
        if (!m_useVerovio) {
            // MuseScore-only: save part pointers and reorder staves
            if (m_score) {
                const auto& origParts = m_score->parts();
                for (int i = 0; i < static_cast<int>(origParts.size()); ++i) {
                    origPartMap[i + 1] = origParts[i];
                }
            }

            if (level.parts.size() >= 2 && m_score) {
                bool needsReorder = false;
                for (int i = 1; i < level.parts.size(); ++i) {
                    if (level.parts[i] < level.parts[i - 1]) {
                        needsReorder = true;
                        break;
                    }
                }
                if (needsReorder) {
                    size_t nst = m_score->nstaves();
                    std::vector<staff_idx_t> newOrder(nst);
                    std::iota(newOrder.begin(), newOrder.end(), 0);

                    std::vector<staff_idx_t> staffIndices;
                    std::vector<staff_idx_t> sortedSlots;
                    for (int p : level.parts) {
                        auto it = origPartMap.find(p);
                        if (it != origPartMap.end()) {
                            staff_idx_t si = m_score->staffIdx(it->second);
                            staffIndices.push_back(si);
                        }
                    }
                    sortedSlots = staffIndices;
                    std::sort(sortedSlots.begin(), sortedSlots.end());

                    if (sortedSlots.size() == staffIndices.size()) {
                        for (size_t i = 0; i < sortedSlots.size(); ++i) {
                            newOrder[sortedSlots[i]] = staffIndices[i];
                        }
                        m_score->sortStaves(newOrder);
                    }
                }
            }
        }
#endif

        // Show only the relevant parts
        if (!level.parts.isEmpty()) {
            setVisibleParts(level.parts);
        }

        // Verovio play-along: build note tables (matching electron branch logic)
        if (m_useVerovio && m_engine) {
            m_vrvVoices.clear();
            m_playAlongSynth->clearVoices();
            auto* vrvEngine = dynamic_cast<scoretracker::VerovioEngine*>(m_engine.get());
            if (vrvEngine) {
                QList<int> filteredParts = level.parts;

                if (!level.voices.isEmpty()) {
                    // Multi-voice: each voice gets its own note table
                    m_multiVoice = true;
                    m_voiceKeysHeld.clear();
                    m_voiceKeyZones.clear();

                    for (int vi = 0; vi < level.voices.size(); ++vi) {
                        const auto& vc = level.voices[vi];
                        int filteredIdx = filteredParts.indexOf(vc.playPart);
                        int partIdx = filteredIdx >= 0 ? filteredIdx : 0;

                        auto noteInfos = vrvEngine->getNotesForPart(partIdx, vrvEngine->lastRenderedSvgs());
                        std::vector<scoretracker::NoteEvent> events;
                        VrvVoice vv;
                        vv.keyZone = vc.keys;
                        for (auto& ni : noteInfos) {
                            scoretracker::NoteEvent ev{};
                            ev.midiPitch = ni.pitch;
                            ev.durationTicks = 480;
                            ev.tiedBack = ni.tiedBack;
                            events.push_back(ev);
                            vv.elementIds.push_back(ni.elementId);
                        }
                        m_vrvVoices.push_back(std::move(vv));

                        // Load soundfont for this voice
                        int sfId = -1;
                        if (!vc.soundfont.isEmpty()) {
                            QString sfPath = m_soundfontsDir + "/" + vc.soundfont;
                            sfId = m_playAlongSynth->ensureSoundfont(sfPath);
                        }
                        m_playAlongSynth->addVoiceFromNotes(events, vc.gmProgram, sfId);

                        m_voiceKeysHeld.append(0);
                        m_voiceKeyZones.append(vc.keys);

                        qDebug() << "Verovio voice" << vi << ":" << noteInfos.size()
                                 << "notes, part" << vc.playPart << "gm" << vc.gmProgram
                                 << "sfId" << sfId << "sf" << vc.soundfont
                                 << "keys" << vc.keys;
                    }
                } else if (level.playPart > 0) {
                    // Single-voice
                    m_multiVoice = false;
                    int filteredIdx = filteredParts.indexOf(level.playPart);
                    int partIdx = filteredIdx >= 0 ? filteredIdx : 0;

                    auto noteInfos = vrvEngine->getNotesForPart(partIdx, vrvEngine->lastRenderedSvgs());
                    std::vector<scoretracker::NoteEvent> events;
                    VrvVoice vv;
                    vv.keyZone = "all";
                    for (auto& ni : noteInfos) {
                        scoretracker::NoteEvent ev{};
                        ev.midiPitch = ni.pitch;
                        ev.durationTicks = 480;
                        ev.tiedBack = ni.tiedBack;
                        events.push_back(ev);
                        vv.elementIds.push_back(ni.elementId);
                    }
                    m_vrvVoices.push_back(std::move(vv));

                    // Load soundfont
                    if (!level.soundfont.isEmpty()) {
                        QString sfPath = m_soundfontsDir + "/" + level.soundfont;
                        m_playAlongSynth->loadSoundfont(sfPath);
                    }
                    m_playAlongSynth->setVoiceFromNotes(events, level.gmProgram);

                    qDebug() << "Verovio single voice:" << noteInfos.size()
                             << "notes, part" << level.playPart << "gm" << level.gmProgram;
                }

                // Re-apply tuning from active interpretation (loadSources may have set it
                // before voices existed)
                if (m_activeInterpretation < m_sourceTunings.size()) {
                    m_playAlongSynth->setPitchOffset(m_sourceTunings[m_activeInterpretation]);
                }

                // Set up instrument panel for Verovio voices
                int nVoices = static_cast<int>(m_vrvVoices.size());
                setupInstrumentPanelForVoices(nVoices);

                // Populate instrument combos from soundfonts
                if (nVoices > 1) {
                    for (int vi = 0; vi < level.voices.size() && vi < m_voiceSfCombos.size(); ++vi) {
                        const auto& vc = level.voices[vi];
                        auto* sfCombo = m_voiceSfCombos[vi];
                        auto* instrCombo = m_voiceInstrCombos[vi];

                        if (!vc.soundfont.isEmpty()) {
                            sfCombo->blockSignals(true);
                            for (int si = 0; si < m_soundfontPaths.size(); ++si) {
                                if (QFileInfo(m_soundfontPaths[si]).fileName() == vc.soundfont) {
                                    sfCombo->setCurrentIndex(si);
                                    break;
                                }
                            }
                            sfCombo->blockSignals(false);
                        }

                        int sfId = m_playAlongSynth->soundfontIdForVoice(vi);
                        if (sfId >= 0) {
                            instrCombo->blockSignals(true);
                            instrCombo->clear();
                            auto presets = m_playAlongSynth->presetsForSoundfont(sfId);
                            for (const auto& p : presets)
                                instrCombo->addItem(p.second, p.first);
                            int curProg = m_playAlongSynth->gmProgramForVoice(vi);
                            for (int i = 0; i < instrCombo->count(); ++i) {
                                if (instrCombo->itemData(i).toInt() == curProg) {
                                    instrCombo->setCurrentIndex(i);
                                    break;
                                }
                            }
                            instrCombo->blockSignals(false);
                        }
                    }
                } else if (nVoices == 1) {
                    // Single voice: populate the global instrument combo
                    if (m_instrumentCombo) {
                        int sfId = m_playAlongSynth->soundfontIdForVoice(0);
                        if (sfId >= 0) {
                            m_instrumentCombo->blockSignals(true);
                            m_instrumentCombo->clear();
                            auto presets = m_playAlongSynth->presetsForSoundfont(sfId);
                            for (const auto& p : presets)
                                m_instrumentCombo->addItem(p.second, p.first);
                            int curProg = m_playAlongSynth->gmProgramForVoice(0);
                            for (int i = 0; i < m_instrumentCombo->count(); ++i) {
                                if (m_instrumentCombo->itemData(i).toInt() == curProg) {
                                    m_instrumentCombo->setCurrentIndex(i);
                                    break;
                                }
                            }
                            m_instrumentCombo->blockSignals(false);
                        }
                        // Select soundfont combo
                        QString sfName = level.soundfont;
                        if (!sfName.isEmpty() && m_soundfontCombo) {
                            m_soundfontCombo->blockSignals(true);
                            for (int i = 0; i < m_soundfontPaths.size(); ++i) {
                                if (QFileInfo(m_soundfontPaths[i]).fileName() == sfName) {
                                    m_soundfontCombo->setCurrentIndex(i);
                                    break;
                                }
                            }
                            m_soundfontCombo->blockSignals(false);
                        }
                    }
                }

                // Highlight first note of each voice
                // Delay initial highlights until after note positions are fetched (1200ms)
                QTimer::singleShot(1500, this, [this]() {
                    for (int vi = 0; vi < static_cast<int>(m_vrvVoices.size()); ++vi) {
                        auto& vv = m_vrvVoices[vi];
                        if (!vv.elementIds.empty())
                            m_scoreWidget->overlayHighlight(vi, vv.elementIds[0]);
                        else
                            m_scoreWidget->overlayClearHighlight(vi);
                    }
                });
            }
        }

        // Multi-voice or single-voice setup (skip for Verovio — already set up above)
#ifdef USE_MUSESCORE
        if (m_useVerovio) {
            // Verovio play-along was already configured above; skip MuseScore setup
        } else if (!level.voices.isEmpty()) {
            // Multi-voice level
            m_multiVoice = true;
            m_voiceKeysHeld.clear();
            m_voiceKeyZones.clear();
            m_playAlongSynth->clearVoices();
            m_scoreWidget->setHighlightElement(nullptr);
            m_scoreWidget->setHighlightElement2(nullptr);

            for (int vi = 0; vi < level.voices.size(); ++vi) {
                const auto& vc = level.voices[vi];
                m_voiceKeysHeld.append(0);
                m_voiceKeyZones.append(vc.keys);

                // Load soundfont for this voice
                int sfId = -1;
                if (!vc.soundfont.isEmpty()) {
                    QString sfPath = m_soundfontsDir + "/" + vc.soundfont;
                    sfId = m_playAlongSynth->ensureSoundfont(sfPath);
                }

                auto pit = origPartMap.find(vc.playPart);
                if (pit != origPartMap.end()) {
                    m_playAlongSynth->addVoice(pit->second, vc.gmProgram, sfId, m_score);
                }
            }

            // Set highlights for each voice
            if (m_playAlongSynth->voiceCount() > 0)
                m_scoreWidget->setHighlightElement(m_playAlongSynth->nextNoteElementForVoice(0));
            if (m_playAlongSynth->voiceCount() > 1)
                m_scoreWidget->setHighlightElement2(m_playAlongSynth->nextNoteElementForVoice(1));

            // Setup multi-voice instrument panel
            setupInstrumentPanelForVoices(level.voices.size());

            // Preset each voice's soundfont and instrument combos
            for (int vi = 0; vi < level.voices.size(); ++vi) {
                const auto& vc = level.voices[vi];
                if (vi < m_voiceSfCombos.size() && vi < m_voiceInstrCombos.size()) {
                    auto* sfCombo = m_voiceSfCombos[vi];
                    auto* instrCombo = m_voiceInstrCombos[vi];

                    // Select soundfont
                    if (!vc.soundfont.isEmpty()) {
                        sfCombo->blockSignals(true);
                        for (int si = 0; si < m_soundfontPaths.size(); ++si) {
                            if (QFileInfo(m_soundfontPaths[si]).fileName() == vc.soundfont) {
                                sfCombo->setCurrentIndex(si);
                                break;
                            }
                        }
                        sfCombo->blockSignals(false);
                    }

                    // Populate instrument list from the voice's soundfont
                    int sfId = m_playAlongSynth->soundfontIdForVoice(vi);
                    if (sfId >= 0) {
                        instrCombo->blockSignals(true);
                        instrCombo->clear();
                        auto presets = m_playAlongSynth->presetsForSoundfont(sfId);
                        for (const auto& p : presets) {
                            instrCombo->addItem(p.second, p.first);
                        }
                        // Select current program
                        int curProg = m_playAlongSynth->gmProgramForVoice(vi);
                        for (int i = 0; i < instrCombo->count(); ++i) {
                            if (instrCombo->itemData(i).toInt() == curProg) {
                                instrCombo->setCurrentIndex(i);
                                break;
                            }
                        }
                        instrCombo->blockSignals(false);
                    }
                }
            }
        } else {
            // Single-voice level
            m_multiVoice = false;
            m_scoreWidget->setHighlightElement2(nullptr);
            setupInstrumentPanelForVoices(1);

            // Load soundfont
            if (!level.soundfont.isEmpty() && m_soundfontCombo) {
                for (int i = 0; i < m_soundfontPaths.size(); ++i) {
                    if (QFileInfo(m_soundfontPaths[i]).fileName() == level.soundfont) {
                        m_soundfontCombo->blockSignals(true);
                        m_soundfontCombo->setCurrentIndex(i);
                        m_soundfontCombo->blockSignals(false);
                        m_playAlongSynth->loadSoundfont(m_soundfontPaths[i]);
                        m_instrumentCombo->blockSignals(true);
                        m_instrumentCombo->clear();
                        auto presets = m_playAlongSynth->presets();
                        for (const auto& p : presets) {
                            m_instrumentCombo->addItem(p.second, p.first);
                        }
                        m_instrumentCombo->blockSignals(false);
                        break;
                    }
                }
            }

            if (level.playPart > 0 && m_score) {
                const auto& parts = m_score->parts();
                int partIdx = level.playPart - 1;
                if (partIdx >= 0 && partIdx < static_cast<int>(parts.size())) {
                    m_partPanel->activatePlayAlong(partIdx, level.gmProgram);
                    m_playAlongSynth->setVoice(parts[partIdx], level.gmProgram, m_score);
                    m_scoreWidget->setHighlightElement(m_playAlongSynth->nextNoteElement());
                    if (m_instrumentCombo) {
                        m_instrumentCombo->blockSignals(true);
                        for (int i = 0; i < m_instrumentCombo->count(); ++i) {
                            if (m_instrumentCombo->itemData(i).toInt() == level.gmProgram) {
                                m_instrumentCombo->setCurrentIndex(i);
                                break;
                            }
                        }
                        m_instrumentCombo->blockSignals(false);
                    }
                }
            }
        }
#endif // USE_MUSESCORE

        // Populate interpretation menu
        m_interpMenu->clear();
        for (int i = 0; i < m_sourceLabels.size(); ++i) {
            auto* action = m_interpMenu->addAction(m_sourceLabels[i]);
            action->setCheckable(true);
            action->setChecked(i == m_activeInterpretation);
            connect(action, &QAction::triggered, this, [this, i]() {
                if (i == m_activeInterpretation) return;
                switchInterpretation(i);
            });
        }

        m_levelBrowser->showLoading(false);

        // Restore window geometry after all deferred layout work settles
        QTimer::singleShot(0, this, [this, savedGeometry]() {
            move(savedGeometry.topLeft());
        });

        // Video resize is handled by the videoReady handler in loadYouTube
        // and the eventFilter on the expanded container.
    });
}

void App::keyPressEvent(QKeyEvent* event)
{
    // Spacebar in navigation view: no video exists, just consume the event
    if (event->key() == Qt::Key_Space && m_centralStack->currentIndex() == 0) {
        // Toggle play/pause on the level browser's preview player
        auto* preview = m_levelBrowser->previewPlayer();
        if (preview) {
            if (preview->isPlaying())
                preview->pause();
            else
                preview->play();
        }
        event->accept();
        return;
    }

    // Escape returns to world browser from score view
    if (event->key() == Qt::Key_Escape && m_centralStack->currentIndex() == 0) {
        // Already in navigation — ignore
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && m_centralStack->currentIndex() == 1
#ifdef USE_MUSESCORE
        && (!m_syncMode || !m_syncMode->isActive())
#endif
        ) {
        if (m_recordTrackingActive) {
            stopRecordTracking();
        }
        playerPause();
        showWorldBrowser();
        return;
    }

#ifdef USE_MUSESCORE
    if (m_syncMode && m_syncMode->isActive()) {
        // Delete/Backspace: unsync the selected or next-to-tap beat
        if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
            int sel = m_scoreWidget->selectedBeatIndex();
            int target = (sel >= 0) ? sel : m_syncMode->nextUnsyncedBeat();
            if (target >= 0 && m_syncMode->beats()[target].synced) {
                m_syncMode->unsyncBeat(target);
                m_syncMode->setNextUnsyncedFrom(target);
                m_scoreWidget->setSelectedBeat(-1);
                m_scoreWidget->widget()->update();
                if (m_waveformWidget) m_waveformWidget->widget()->update();
            }
            return;
        }
        // Escape: clear selection and move next-to-tap to next unsynced beat
        if (event->key() == Qt::Key_Escape) {
            m_scoreWidget->setSelectedBeat(-1);
            int next = m_syncMode->nextUnsyncedBeat();
            if (next >= 0) {
                const auto& beats = m_syncMode->beats();
                for (int i = next; i < static_cast<int>(beats.size()); ++i) {
                    if (!beats[i].synced) {
                        m_syncMode->setNextUnsyncedFrom(i);
                        break;
                    }
                }
            }
            m_scoreWidget->widget()->update();
            if (m_waveformWidget) m_waveformWidget->widget()->update();
            return;
        }
        if (event->key() == Qt::Key_O && playerIsPlaying()) {
            int next = m_syncMode->nextUnsyncedBeat();
            m_syncMode->recordTap(playerCurrentTime());
            m_scoreWidget->setLastTappedBeat(next);
            return;
        }
    }
#endif

    // Instrument volume: 1/& = down, 2/é = up (always available in play mode)
    if (m_playModeActive && m_instrumentVolSlider) {
        QString t = event->text();
        if (t == "&" || event->key() == Qt::Key_1) {
            m_instrumentVolSlider->setValue(std::max(0, m_instrumentVolSlider->value() - 5));
            return;
        }
        if (t == "\xc3\xa9" || event->key() == Qt::Key_2) { // é in UTF-8
            m_instrumentVolSlider->setValue(std::min(m_instrumentVolSlider->maximum(), m_instrumentVolSlider->value() + 5));
            return;
        }
    }

    // Transpose: 3/" = down, 4/' = up (only when instrument panel is visible)
    if (m_playModeActive && m_instrumentPanel && m_instrumentPanel->isVisible() && m_transposeSpin) {
        QString t = event->text();
        if (t == "\"" || event->key() == Qt::Key_3) {
            m_transposeSpin->setValue(m_transposeSpin->value() - m_transposeSpin->singleStep());
            return;
        }
        if (t == "'" || event->key() == Qt::Key_4) {
            m_transposeSpin->setValue(m_transposeSpin->value() + m_transposeSpin->singleStep());
            return;
        }
    }

    // Tap-to-play: laptop keyboard as MIDI controller (letter keys only)
    // Overlap keys for legato, release all for noteoff
    // Skip when Cmd/Ctrl is held (reserved for shortcuts like Cmd+T)
    if (m_playModeActive && playerIsPlaying() && !(event->modifiers() & Qt::ControlModifier)) {
        int key = event->key();
        bool isLetter = (key >= Qt::Key_A && key <= Qt::Key_Z);
        if (isLetter && !event->isAutoRepeat() && m_useVerovio && !m_vrvVoices.empty()) {
            if (m_multiVoice) {
                // Multi-voice Verovio: route to voices by key zone (matching electron)
                static const QString leftKeys = "ABCDEFGQRSTVWXZ";
                static const QString rightKeys = "HIJKLMNOPUY";
                QChar ch = QChar(key);
                bool isLeft = leftKeys.contains(ch);
                bool isRight = rightKeys.contains(ch);

                for (int vi = 0; vi < m_voiceKeysHeld.size(); ++vi) {
                    const QString& zone = m_voiceKeyZones[vi];
                    bool match = (zone == "all")
                              || (zone == "left" && isLeft)
                              || (zone == "right" && isRight);
                    if (match) {
                        m_voiceKeysHeld[vi]++;
                        m_playAlongSynth->playNextNoteForVoice(vi);
                    }
                }
                // Update highlights via QPainter overlay (synchronous, no IPC)
                for (int vi = 0; vi < static_cast<int>(m_vrvVoices.size()); ++vi) {
                    auto& vv = m_vrvVoices[vi];
                    int idx = m_playAlongSynth->nextNoteIndex(vi);
                    if (idx < static_cast<int>(vv.elementIds.size()))
                        m_scoreWidget->overlayHighlight(vi, vv.elementIds[idx]);
                    else
                        m_scoreWidget->overlayClearHighlight(vi);
                }
                // Auto-scroll still via JS (not latency-critical)
                // auto-scroll triggered by overlayHighlight
            } else {
                // Single-voice Verovio
                m_keysHeld++;
                m_playAlongSynth->playNextNote();
                auto& vv = m_vrvVoices[0];
                int idx = m_playAlongSynth->nextNoteIndex();
                if (idx < static_cast<int>(vv.elementIds.size()))
                    m_scoreWidget->overlayHighlight(0, vv.elementIds[idx]);
                else
                    m_scoreWidget->overlayClearHighlight(0);
                // auto-scroll triggered by overlayHighlight
            }
        } else if (isLetter && !event->isAutoRepeat()) {
            if (m_multiVoice) {
                // Determine key zone: left or right half of keyboard
                static const QString leftKeys = "ABCDEFGQRSTVWXZ";
                static const QString rightKeys = "HIJKLMNOPUY";
                QChar ch = QChar(key);
                bool isLeft = leftKeys.contains(ch);
                bool isRight = rightKeys.contains(ch);
                // Play all matching voices first (minimize latency between notes)
                int matchMask = 0;
                for (int vi = 0; vi < m_voiceKeysHeld.size(); ++vi) {
                    const QString& zone = m_voiceKeyZones[vi];
                    bool match = (zone == "all")
                              || (zone == "left" && isLeft)
                              || (zone == "right" && isRight);
                    if (match) {
                        m_voiceKeysHeld[vi]++;
                        m_playAlongSynth->playNextNoteForVoice(vi);
                        matchMask |= (1 << vi);
                        if (m_recordTrackingActive && vi == 0) {
                            int tick = m_playAlongSynth->lastPlayedTick(0);
                            if (tick >= 0) {
                                m_recordedNotes.push_back({playerCurrentTime(), tick});
                            }
                        }
                    }
                }
                // Defer highlight updates so they don't block the next keypress
                if (matchMask) {
                    QTimer::singleShot(0, this, [this, matchMask]() {
                        if (matchMask & 1)
                            m_scoreWidget->setHighlightElement(m_playAlongSynth->nextNoteElementForVoice(0));
                        if (matchMask & 2)
                            m_scoreWidget->setHighlightElement2(m_playAlongSynth->nextNoteElementForVoice(1));
                    });
                }
            } else {
                m_keysHeld++;
                m_playAlongSynth->playNextNote();
                if (m_recordTrackingActive) {
                    int tick = m_playAlongSynth->lastPlayedTick();
                    if (tick >= 0) {
                        m_recordedNotes.push_back({playerCurrentTime(), tick});
                    }
                }
                QTimer::singleShot(0, this, [this]() {
                    m_scoreWidget->setHighlightElement(m_playAlongSynth->nextNoteElement());
                });
            }
        }
        if (isLetter) return;
    }

    QMainWindow::keyPressEvent(event);
}

void App::keyReleaseEvent(QKeyEvent* event)
{
    int key = event->key();
    bool isLetter = (key >= Qt::Key_A && key <= Qt::Key_Z);
    if (isLetter && !event->isAutoRepeat() && m_playModeActive && playerIsPlaying()) {
        if (m_multiVoice) {
            static const QString leftKeys = "ABCDEFGQRSTVWXZ";
            static const QString rightKeys = "HIJKLMNOPUY";
            QChar ch = QChar(key);
            bool isLeft = leftKeys.contains(ch);
            bool isRight = rightKeys.contains(ch);
            for (int vi = 0; vi < m_voiceKeysHeld.size(); ++vi) {
                const QString& zone = m_voiceKeyZones[vi];
                bool match = (zone == "all")
                          || (zone == "left" && isLeft)
                          || (zone == "right" && isRight);
                if (match) {
                    m_voiceKeysHeld[vi] = std::max(0, m_voiceKeysHeld[vi] - 1);
                    if (m_voiceKeysHeld[vi] == 0) {
                        m_playAlongSynth->stopNoteForVoice(vi);
                    }
                }
            }
        } else {
            m_keysHeld = std::max(0, m_keysHeld - 1);
            if (m_keysHeld == 0) {
                m_playAlongSynth->stopNote();
            }
        }
        return;
    }
    QMainWindow::keyReleaseEvent(event);
}

} // namespace scoretracker
