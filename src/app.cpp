#include "app.h"
#include "scorewidget.h"
#include "audioplayer.h"
#include "youtubeplayer.h"
#include "synctimer.h"
#include "syncmode.h"
#include "syncpanel.h"
#include "waveformwidget.h"
#include "partpanel.h"
#include "displaysettings.h"
#include "trackingsettings.h"
#include "collapsiblesection.h"
#include "theme.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMenu>

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

#include <QDebug>
#include <QPushButton>
#include <QScrollBar>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QSplitter>

using namespace mu::engraving;
using namespace mu::engraving::rendering;

namespace scoretracker {

static LayoutMode comboIndexToLayoutMode(int index)
{
    switch (index) {
    case 0: return LayoutMode::PAGE;
    case 1: return LayoutMode::LINE;
    case 2: return LayoutMode::SYSTEM;
    default: return LayoutMode::SYSTEM;
    }
}

App::App(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("ScoreTracker");
    resize(1250, 850);

    m_audioPlayer = new AudioPlayer(this);
    m_syncTimer = new SyncTimer(this);
    m_syncMode = new SyncMode(this);

    setupUI();
    setupToolbar();

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

    // Wire audio position to sync timer to score widget
    connect(m_audioPlayer, &AudioPlayer::positionChanged,
            this, &App::onPositionChanged);

    connect(m_syncTimer, &SyncTimer::cursorRectChanged,
            m_scoreWidget, &ScoreWidget::setCursorRect);

    connect(m_partPanel, &PartPanel::partsChanged, [this]() {
        m_scoreWidget->setScore(m_score); // refresh
        m_syncTimer->refresh();
    });

    // Bidirectional sync: sidebar checkbox <-> toolbar tracking button
    connect(m_trackingSettings, &TrackingSettings::trackingToggled, [this](bool on) {
        m_trackingAction->setChecked(on);
    });
    connect(m_trackingAction, &QAction::toggled, [this](bool on) {
        m_trackingSettings->setTrackingEnabled(on);
        if (!on) {
            // Hide cursor visually
            m_scoreWidget->setCursorVisible(false);
            if (!m_userForcedAutoScroll) {
                // Programmatically untick auto-scroll without saving
                m_trackingSettings->blockSignals(true);
                m_trackingSettings->setAutoScrollEnabled(false);
                m_trackingSettings->blockSignals(false);
                m_scoreWidget->setAutoScrollEnabled(false);
                m_scoreWidget->setCursorRect(muse::RectF(), -1);
            }
        } else {
            m_scoreWidget->setCursorVisible(true);
        }
    });

    connect(m_trackingSettings, &TrackingSettings::settingChanged, [this]() {
        bool autoScroll = m_trackingSettings->autoScrollEnabled();
        bool tracking = m_trackingSettings->trackingEnabled();

        // Track user forcing auto-scroll while tracking is off
        if (!tracking && autoScroll) {
            m_userForcedAutoScroll = true;
        }
        if (!autoScroll) {
            m_userForcedAutoScroll = false;
        }

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
            // Feed current position to sync timer so cursor position is computed
            if (playerDuration() > 0) {
                m_syncTimer->setTime(playerCurrentTime());
            }
        } else if (!tracking && !autoScroll) {
            m_scoreWidget->setCursorRect(muse::RectF(), -1);
        }
    });

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

    // Set initial overlay width and position sidebar
    int scrollbarW = m_scoreWidget->verticalScrollBar()->sizeHint().width();
    m_scoreWidget->setOverlayWidth(m_sidebarWidth + scrollbarW);
    QTimer::singleShot(0, this, &App::repositionSidebar);
}

App::~App()
{
    delete m_score;
}

void App::setupUI()
{
    // Central splitter: waveform (top) + score (bottom)
    m_centralSplitter = new QSplitter(Qt::Vertical, this);
    m_centralSplitter->setChildrenCollapsible(false);
    m_centralSplitter->setHandleWidth(5);
    m_centralSplitter->setStyleSheet(
        "QSplitter::handle:vertical { background: #555; border-top: 1px solid #666; border-bottom: 1px solid #333; }");

    m_waveformWidget = new WaveformWidget(m_centralSplitter);
    m_waveformWidget->hide();
    m_centralSplitter->addWidget(m_waveformWidget);

    connect(m_waveformWidget, &WaveformWidget::seekRequested, this, [this](double t) {
        playerSeekTo(t);
        m_syncTimer->setTime(t);
        onPositionChanged(t);
        m_scoreWidget->clearLastTappedBeat();
    });

    m_scoreWidget = new ScoreWidget(m_centralSplitter);
    m_centralSplitter->addWidget(m_scoreWidget);

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

    // Score gets most of the space by default
    m_centralSplitter->setStretchFactor(0, 0); // waveform: don't stretch
    m_centralSplitter->setStretchFactor(1, 1); // score: stretch

    setCentralWidget(m_centralSplitter);

    // Sidebar overlays the score on the right edge
    m_sidebarWidget = new QWidget(this);
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

    // Drag handle on the left edge of the sidebar
    m_sidebarHandle = new QWidget(this);
    m_sidebarHandle->setFixedWidth(5);
    m_sidebarHandle->setCursor(Qt::SplitHCursor);
    m_sidebarHandle->installEventFilter(this);
}

void App::repositionSidebar()
{
    if (!m_sidebarWidget) return;
    QRect cr = centralWidget()->geometry();
    int scrollbarW = m_scoreWidget->verticalScrollBar()->sizeHint().width();
    if (m_sidebarWidget->isVisible()) {
        int x = cr.right() - m_sidebarWidth - scrollbarW + 1;
        m_sidebarWidget->setGeometry(x, cr.top(), m_sidebarWidth, cr.height());
        m_sidebarHandle->setGeometry(x - 5, cr.top(), 5, cr.height());
        m_sidebarHandle->show();
        m_sidebarWidget->raise();
        m_sidebarHandle->raise();
    } else if (!m_sidebarDragging) {
        m_sidebarHandle->hide();
    }
}

void App::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    repositionSidebar();
    repositionSyncSidebar();
}

bool App::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_sidebarHandle) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            m_sidebarDragging = true;
            m_dragStartX = me->globalPosition().toPoint().x();
            m_dragStartWidth = m_sidebarWidth;
            return true;
        }
        if (event->type() == QEvent::MouseMove && m_sidebarDragging) {
            auto* me = static_cast<QMouseEvent*>(event);
            int dx = me->globalPosition().toPoint().x() - m_dragStartX;
            int newWidth = m_dragStartWidth - dx;
            static const int MIN_WIDTH = 280;
            static const int MAX_WIDTH = 500;
            static const int COLLAPSE_THRESHOLD = 120;
            int scrollbarW = m_scoreWidget->verticalScrollBar()->sizeHint().width();
            if (newWidth < COLLAPSE_THRESHOLD) {
                // Collapse but keep dragging
                if (m_sidebarWidget->isVisible()) {
                    m_sidebarWidget->setVisible(false);
                    m_scoreWidget->setOverlayWidth(0);
                    m_sidebarHandle->raise();
                    m_sidebarAction->blockSignals(true);
                    m_sidebarAction->setChecked(false);
                    m_sidebarAction->blockSignals(false);
                }
            } else {
                // Uncollapse if needed
                if (!m_sidebarWidget->isVisible()) {
                    m_sidebarWidget->setVisible(true);
                    m_scoreWidget->setOverlayWidth(m_sidebarWidth + scrollbarW);
                    m_sidebarAction->blockSignals(true);
                    m_sidebarAction->setChecked(true);
                    m_sidebarAction->blockSignals(false);
                }
                m_sidebarWidth = std::clamp(newWidth, MIN_WIDTH, MAX_WIDTH);
                m_scoreWidget->setOverlayWidth(m_sidebarWidth + scrollbarW);
                repositionSidebar();
            }
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            m_sidebarDragging = false;
            // Only refit when sidebar was collapsed by dragging
            if (!m_sidebarWidget->isVisible()) {
                m_scoreWidget->zoomToFit();
            }
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void App::setSidebarVisible(bool visible)
{
    int scrollbarW = m_scoreWidget->verticalScrollBar()->sizeHint().width();
    m_sidebarWidget->setVisible(visible);
    m_scoreWidget->setOverlayWidth(visible ? m_sidebarWidth + scrollbarW : 0);
    repositionSidebar();
    if (!visible) {
        m_scoreWidget->zoomToFit();
    }
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

    m_stopAction = m_toolbar->addAction("Stop");
    connect(m_stopAction, &QAction::triggered, this, [this]() { playerStop(); });
    m_toolbar->widgetForAction(m_stopAction)->setCursor(Qt::PointingHandCursor);

    m_toolbar->addSeparator();

    m_seekSlider = new QSlider(Qt::Horizontal, this);
    m_seekSlider->setCursor(Qt::PointingHandCursor);
    m_seekSlider->setRange(0, 1000);
    m_seekSlider->setMinimumWidth(300);
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

    // Source button (hidden until loadSources is called)
    m_sourceButton = new QPushButton("Source", this);
    m_sourceButton->setFlat(true);
    m_sourceButton->setCursor(Qt::PointingHandCursor);
    m_sourceButton->setFocusPolicy(Qt::NoFocus);
    m_sourceButton->setStyleSheet(
        "QPushButton { padding: 2px 4px; }"
        "QPushButton:pressed { background: transparent; padding: 2px 4px; }");
    m_sourceButton->setMenu(new QMenu(m_sourceButton));
    m_sourceButtonAction = m_toolbar->addWidget(m_sourceButton);
    m_sourceButtonAction->setVisible(false);

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
    m_trackingButton->setFixedSize(m_trackingButton->sizeHint());

    m_trackingButton->setShortcut(QKeySequence(Qt::Key_T));
    m_toolbar->addWidget(m_trackingButton);

    connect(m_trackingButton, &QPushButton::clicked, [this]() {
        m_trackingAction->toggle();
    });
    connect(m_trackingAction, &QAction::toggled, [this](bool on) {
        updateTrackingIcon();
        if (!on) {
            m_scoreWidget->setCursorRect(muse::RectF(), -1);
        }
    });

    m_toolbar->addSeparator();

    // Sync mode button
    m_syncModeButton = new QPushButton("Sync Mode", this);
    m_syncModeButton->setFlat(true);
    m_syncModeButton->setCursor(Qt::PointingHandCursor);
    m_syncModeButton->setFocusPolicy(Qt::NoFocus);
    m_syncModeButton->setCheckable(true);
    m_syncModeButton->setStyleSheet(
        "QPushButton { padding: 2px 8px; font-size: 12px; border: none; background: transparent; }"
        "QPushButton:checked { color: #1e78ff; font-weight: bold; }");
    // Size to bold metrics so toggling doesn't crop
    QFont boldFont = m_syncModeButton->font();
    boldFont.setPointSize(12);
    boldFont.setBold(true);
    int boldWidth = QFontMetrics(boldFont).horizontalAdvance("Sync Mode") + 20;
    m_syncModeButton->setMinimumWidth(boldWidth);
    m_toolbar->addWidget(m_syncModeButton);
    connect(m_syncModeButton, &QPushButton::toggled, this, [this](bool on) {
        if (on) enterSyncMode();
        else exitSyncMode();
    });

    m_toolbar->addSeparator();

    auto* zoomOutAction = m_toolbar->addAction("-");
    zoomOutAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));
    connect(zoomOutAction, &QAction::triggered, m_scoreWidget, &ScoreWidget::zoomOut);
    m_toolbar->widgetForAction(zoomOutAction)->setCursor(Qt::PointingHandCursor);

    m_zoomLabel = new QLabel("150%", this);
    m_zoomLabel->setMinimumWidth(50);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    m_toolbar->addWidget(m_zoomLabel);

    auto* zoomInAction = m_toolbar->addAction("+");
    zoomInAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal));
    connect(zoomInAction, &QAction::triggered, m_scoreWidget, &ScoreWidget::zoomIn);
    m_toolbar->widgetForAction(zoomInAction)->setCursor(Qt::PointingHandCursor);

    auto* fitButton = new QPushButton("Fit", this);
    fitButton->setFlat(true);
    fitButton->setCursor(Qt::PointingHandCursor);
    fitButton->setStyleSheet("QPushButton:pressed { background-color: rgba(255,255,255,0.1); }");
    fitButton->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    m_toolbar->addWidget(fitButton);
    connect(fitButton, &QPushButton::clicked, m_scoreWidget, &ScoreWidget::zoomToFit);

    connect(m_scoreWidget, &ScoreWidget::zoomChanged, [this](double zoom) {
        m_zoomLabel->setText(QString("%1%").arg(static_cast<int>(zoom * 100)));
    });

    m_toolbar->addSeparator();

    m_sidebarAction = m_toolbar->addAction("Sidebar");
    m_sidebarAction->setCheckable(true);
    m_sidebarAction->setChecked(true);
    m_sidebarAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    auto* sidebarWidget = m_toolbar->widgetForAction(m_sidebarAction);
    sidebarWidget->setCursor(Qt::PointingHandCursor);
    sidebarWidget->setStyleSheet("font-size: 12px;");
    connect(m_sidebarAction, &QAction::toggled, [this](bool on) {
        setSidebarVisible(on);
    });

    auto* sidebarSpacer = new QWidget();
    sidebarSpacer->setFixedWidth(2);
    m_toolbar->addWidget(sidebarSpacer);

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

    // Resolve the renderer from IoC
    m_renderer = muse::modularity::globalIoc()->resolve<IScoreRenderer>("scoretracker");
    if (!m_renderer) {
        qWarning() << "Failed to resolve IScoreRenderer";
        return false;
    }

    // Create a score with default style
    auto iocCtx = muse::modularity::globalCtx();
    m_score = compat::ScoreAccess::createMasterScoreWithDefaultStyle(iocCtx);
    m_score->setFileInfoProvider(std::make_shared<LocalFileInfoProvider>(musicXmlPath.toStdString()));

    // Import MusicXML
    muse::String path = muse::String::fromQString(musicXmlPath);
    Err err = mu::iex::musicxml::importMusicXml(m_score, path, false);
    if (err != Err::NoError) {
        qWarning() << "Failed to import MusicXML, error:" << static_cast<int>(err);
        delete m_score;
        m_score = nullptr;
        return false;
    }

    qDebug() << "Score loaded:" << musicXmlPath;
    qDebug() << "Parts:" << m_score->parts().size();
    qDebug() << "Measures:" << m_score->nmeasures();

    // Page layout: Letter, matching MuseScore defaults
    m_score->style().set(Sid::pageWidth, 8.5);
    m_score->style().set(Sid::pageHeight, 11.0);
    m_score->style().set(Sid::spatium, 0.046 * 1200.0); // 0.046in in internal units
    m_score->style().set(Sid::pageOddTopMargin, 0.39);
    m_score->style().set(Sid::pageOddBottomMargin, 0.79);
    m_score->style().set(Sid::pageOddLeftMargin, 0.39);
    m_score->style().set(Sid::pageEvenTopMargin, 0.39);
    m_score->style().set(Sid::pageEvenBottomMargin, 0.79);
    m_score->style().set(Sid::pageEvenLeftMargin, 0.39);
    m_score->style().set(Sid::pagePrintableWidth, 8.5 - 0.39 - 0.39);

    // Ensure instrument names show on all systems
    m_score->style().set(Sid::firstSystemInstNameVisibility,
        mu::engraving::PropertyValue(int(InstrumentLabelVisibility::LONG)));
    m_score->style().set(Sid::subsSystemInstNameVisibility,
        mu::engraving::PropertyValue(int(InstrumentLabelVisibility::SHORT)));
    m_score->style().set(Sid::hideInstrumentNameIfOneInstrument, false);

    // Generate short instrument names from long names if missing
    // Full-string overrides for specific instruments
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
    m_score->setLayoutMode(comboIndexToLayoutMode(m_displaySettings->layoutMode()));
    bool showTitle = m_displaySettings->showTitleFrame();
    m_score->setShowVBox(showTitle);
    {
        double topMargin = showTitle ? 0.39 : 0.10;
        m_score->style().set(Sid::pageOddTopMargin, topMargin);
        m_score->style().set(Sid::pageEvenTopMargin, topMargin);
    }

    // Layout the score
    m_renderer->layoutScore(m_score, Fraction(0, 1), Fraction(-1, 1));

    qDebug() << "Score layout complete, pages:" << m_score->pages().size();

    // Debug: page layout
    qDebug() << "pageWidth:" << m_score->style().styleD(Sid::pageWidth) << "in"
             << "pageHeight:" << m_score->style().styleD(Sid::pageHeight) << "in";
    qDebug() << "spatium:" << m_score->style().spatium() / 1200.0 << "in";
    qDebug() << "margins - top:" << m_score->style().styleD(Sid::pageOddTopMargin)
             << "bottom:" << m_score->style().styleD(Sid::pageOddBottomMargin)
             << "left:" << m_score->style().styleD(Sid::pageOddLeftMargin)
             << "right:" << m_score->style().styleD(Sid::pageEvenLeftMargin);
    for (const auto* part : m_score->parts()) {
        const auto& instr = *part->instrument();
        qDebug() << "Part:" << part->partName()
                 << "longName:" << (instr.longNames().empty() ? "EMPTY" : instr.longNames().front().name().toQString())
                 << "shortName:" << (instr.shortNames().empty() ? "EMPTY" : instr.shortNames().front().name().toQString());
    }

    // Set up widgets
    m_scoreWidget->setRenderer(m_renderer.get());
    m_scoreWidget->setScore(m_score);

    m_syncTimer->setScore(m_score);

    m_partPanel->setScore(m_score);
    m_partPanel->setRenderer(m_renderer.get());
    m_partPanel->setScoreFileName(fi.fileName());

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

    setWindowTitle(QString("ScoreTracker - %1").arg(fi.fileName()));

    // Fit score to viewport after layout settles
    QTimer::singleShot(0, m_scoreWidget, &ScoreWidget::zoomToFit);

    return true;
}

void App::setVisibleParts(const QList<int>& partNumbers)
{
    m_partPanel->showOnlyParts(partNumbers);
}

void App::startSyncMode()
{
    m_syncModeButton->setChecked(true);
    // When launched via --sync, start with tracking off
    m_trackingAction->setChecked(false);
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

    if (obj.contains("measures")) {
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

    // Compute tick position for each beat (each beat = quarter note = 480 ticks)
    std::vector<int> beatTicks;
    beatTicks.reserve(beatTimes.size());
    for (size_t i = 0; i < beatTimes.size(); ++i) {
        beatTicks.push_back(static_cast<int>(i) * 480);
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
    m_scoreWidget->clearLastTappedBeat();
}

void App::onPositionChanged(double seconds)
{
    // Update slider position
    if (!m_sliderDragging) {
        double duration = playerDuration();
        if (duration > 0) {
            int sliderVal = static_cast<int>((seconds / duration) * m_seekSlider->maximum());
            m_seekSlider->blockSignals(true);
            m_seekSlider->setValue(sliderVal);
            m_seekSlider->blockSignals(false);
        }
    }

    // Update time label
    m_timeLabel->setText(QString("%1 / %2")
        .arg(formatTime(seconds))
        .arg(formatTime(playerDuration())));

    // Update sync timer -> cursor if tracking is on, or auto-scroll without tracking
    if (m_trackingAction->isChecked() || m_trackingSettings->autoScrollEnabled()) {
        m_syncTimer->setTime(seconds);
    }

    // Update sync mode widgets
    if (m_syncMode->isActive()) {
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
    bool on = m_trackingAction->isChecked();
    int sz = 16; // render at 2x for retina
    QPixmap px(sz + 7, sz + 3);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    if (on) {
        p.setBrush(QColor("#4CAF50"));
        p.setPen(QPen(QColor("#4CAF50"), 1.5));
    } else {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Theme::textHint(), 1.5));
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
    if (m_useYouTube && m_youtubePlayer)
        m_youtubePlayer->seekTo(seconds);
    else
        m_audioPlayer->seekTo(seconds);
}

double App::playerCurrentTime() const
{
    if (m_useYouTube && m_youtubePlayer)
        return m_youtubePlayer->currentTime();
    return m_audioPlayer->currentTime();
}

double App::playerDuration() const
{
    if (m_useYouTube && m_youtubePlayer)
        return m_youtubePlayer->duration();
    return m_audioPlayer->duration();
}

bool App::playerIsPlaying() const
{
    if (m_useYouTube && m_youtubePlayer)
        return m_youtubePlayer->isPlaying();
    return m_audioPlayer->isPlaying();
}

void App::loadSources(const QString& jsonPath)
{
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
    QMenu* menu = m_sourceButton->menu();
    menu->clear();

    QDir sourceDir = QFileInfo(jsonPath).absoluteDir();
    QString youtubeUrl;
    QString audioFile;

    if (obj.contains("file")) {
        audioFile = sourceDir.absoluteFilePath(obj.value("file").toString());
        auto* action = menu->addAction("File");
        connect(action, &QAction::triggered, this, [this, audioFile]() {
            double pos = playerCurrentTime();
            if (m_useYouTube && m_youtubePlayer) {
                m_youtubePlayer->pause();
                m_videoDock->hide();
                m_speedButton->setEnabled(false);
                m_speedButton->setText("Speed: 1x");
            }
            m_useYouTube = false;
            loadAudio(audioFile);
            m_audioPlayer->seekTo(pos);
            m_audioPlayer->play();
        });
    }

    if (obj.contains("youtube")) {
        youtubeUrl = obj.value("youtube").toString();
        auto* action = menu->addAction(QIcon(":/src/icons/youtube.png"), "YouTube");
        connect(action, &QAction::triggered, this, [this, youtubeUrl]() {
            double pos = playerCurrentTime();
            if (!m_useYouTube) {
                m_audioPlayer->pause();
            }
            loadYouTube(youtubeUrl);
            // Seek once video is ready (seekTo requires m_ready)
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = connect(m_youtubePlayer, &YouTubePlayer::videoReady, this, [this, pos, conn]() {
                m_youtubePlayer->seekTo(pos);
                disconnect(*conn);
            });
            m_playPauseAction->setText("Play");
        });
    }

    if (!menu->actions().isEmpty()) {
        m_sourceButtonAction->setVisible(true);
    }

    // Auto-load: YouTube if present, otherwise file
    if (!youtubeUrl.isEmpty()) {
        loadYouTube(youtubeUrl);
    } else if (!audioFile.isEmpty()) {
        loadAudio(audioFile);
    }
}

void App::loadYouTube(const QString& url)
{
    m_useYouTube = true;

    // If player already exists, just show dock and reload
    if (m_youtubePlayer) {
        m_videoDock->show();
        m_youtubePlayer->load(url);
        return;
    }

    m_youtubePlayer = new YouTubePlayer(this);

    // Connect position updates
    connect(m_youtubePlayer, &YouTubePlayer::positionChanged,
            this, &App::onPositionChanged);

    // Connect playback state to toolbar
    connect(m_youtubePlayer, &YouTubePlayer::playbackStarted, [this]() {
        m_playPauseAction->setText("Pause");
        m_scoreWidget->setPlaying(true);
    });
    connect(m_youtubePlayer, &YouTubePlayer::playbackPaused, [this]() {
        m_playPauseAction->setText("Play");
        m_scoreWidget->setPlaying(false);
    });
    connect(m_youtubePlayer, &YouTubePlayer::playbackStopped, [this]() {
        m_playPauseAction->setText("Play");
        m_scoreWidget->setPlaying(false);
    });

    // When video is ready, set up seek slider
    connect(m_youtubePlayer, &YouTubePlayer::videoReady, [this](double duration) {
        m_seekSlider->setRange(0, static_cast<int>(duration * 10));
        m_timeLabel->setText(QString("0:00 / %1").arg(formatTime(duration)));
    });

    // Fixed left-side dock for video — 200x200 minimum per YouTube API TOS
    auto* videoWidget = m_youtubePlayer->videoWidget();
    videoWidget->setFixedSize(200, 200);

    QPalette videoPal = videoWidget->palette();
    videoPal.setColor(QPalette::Window, Qt::black);
    videoWidget->setPalette(videoPal);
    videoWidget->setAutoFillBackground(true);

    m_videoDock = new QDockWidget("Video", this);
    m_videoDock->setWidget(videoWidget);
    m_videoDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    m_videoDock->setFixedWidth(200);
    addDockWidget(Qt::LeftDockWidgetArea, m_videoDock);

    // Enable speed button and wire it to the YouTube player
    connect(m_youtubePlayer, &YouTubePlayer::videoReady, this, [this]() {
        m_speedButton->setEnabled(m_useYouTube);
    });
    connect(m_speedButton->menu(), &QMenu::triggered, this, [this](QAction* action) {
        m_youtubePlayer->setPlaybackRate(action->data().toDouble());
    });
    connect(m_youtubePlayer, &YouTubePlayer::playbackRateChanged,
            this, [this](double rate) {
        m_speedButton->setText(QString("Speed: %1x").arg(rate));
    });

    m_youtubePlayer->load(url);
}

void App::enterSyncMode()
{
    if (!m_score || !m_renderer) return;

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
    if (m_useYouTube && m_sourceButton && m_sourceButton->menu()) {
        for (auto* action : m_sourceButton->menu()->actions()) {
            if (action->text() == "File") {
                action->trigger();
                playerPause();
                break;
            }
        }
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

void App::keyPressEvent(QKeyEvent* event)
{
    if (m_syncMode->isActive()) {
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
    QMainWindow::keyPressEvent(event);
}

} // namespace scoretracker
