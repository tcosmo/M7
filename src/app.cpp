#include "app.h"
#include "scorewidget.h"
#include "audioplayer.h"
#include "synctimer.h"
#include "partpanel.h"
#include "displaysettings.h"
#include "beatdata.h"

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

#include "importexport/musicxml/internal/import/importmusicxml.h"

#include <QDebug>
#include <QFileInfo>
#include <QPainter>
#include <QStyle>

using namespace mu::engraving;
using namespace mu::engraving::rendering;

namespace scoretracker {

App::App(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("ScoreTracker");
    resize(1250, 850);

    m_audioPlayer = new AudioPlayer(this);
    m_syncTimer = new SyncTimer(this);

    setupUI();
    setupToolbar();

    // Wire audio position to sync timer to score widget
    connect(m_audioPlayer, &AudioPlayer::positionChanged,
            this, &App::onPositionChanged);

    connect(m_syncTimer, &SyncTimer::cursorRectChanged,
            m_scoreWidget, &ScoreWidget::setCursorRect);

    connect(m_partPanel, &PartPanel::partsChanged, [this]() {
        m_scoreWidget->setScore(m_score); // refresh
    });

    connect(m_displaySettings, &DisplaySettings::settingChanged, [this]() {
        if (!m_score || !m_renderer) return;
        m_score->setShowVBox(m_displaySettings->showTitleFrame());
        m_renderer->layoutScore(m_score, Fraction(0, 1), Fraction(-1, 1));
        m_scoreWidget->setScore(m_score); // refresh
    });

    // Set up beat data
    m_syncTimer->setMeasureStarts(MAGNIFICAT_MEASURE_STARTS);
    m_syncTimer->setBeatTimes(MAGNIFICAT_BEAT_TIMES, BEATS_PER_MEASURE);
}

App::~App()
{
    delete m_score;
}

void App::setupUI()
{
    m_scoreWidget = new ScoreWidget(this);
    setCentralWidget(m_scoreWidget);

    m_partPanel = new PartPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, m_partPanel);

    m_displaySettings = new DisplaySettings(this);
    addDockWidget(Qt::RightDockWidgetArea, m_displaySettings);
}

void App::setupToolbar()
{
    m_toolbar = addToolBar("Playback");

    m_playPauseAction = m_toolbar->addAction("Play");
    m_playPauseAction->setShortcut(QKeySequence(Qt::Key_Space));
    connect(m_playPauseAction, &QAction::triggered, this, &App::togglePlayPause);

    m_stopAction = m_toolbar->addAction("Stop");
    connect(m_stopAction, &QAction::triggered, m_audioPlayer, &AudioPlayer::stop);

    m_toolbar->addSeparator();

    m_seekSlider = new QSlider(Qt::Horizontal, this);
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

    m_toolbar->addSeparator();

    m_trackingAction = m_toolbar->addAction("Tracking");
    m_trackingAction->setCheckable(true);
    m_trackingAction->setChecked(true);
    m_trackingAction->setShortcut(QKeySequence(Qt::Key_T));
    connect(m_trackingAction, &QAction::toggled, [this](bool on) {
        if (!on) {
            // Clear cursor but don't move scroll position
            m_scoreWidget->setCursorRect(muse::RectF(), -1);
        }
    });

    m_toolbar->addSeparator();

    auto* zoomOutAction = m_toolbar->addAction("-");
    zoomOutAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));
    connect(zoomOutAction, &QAction::triggered, m_scoreWidget, &ScoreWidget::zoomOut);

    m_zoomLabel = new QLabel("150%", this);
    m_zoomLabel->setMinimumWidth(50);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    m_toolbar->addWidget(m_zoomLabel);

    auto* zoomInAction = m_toolbar->addAction("+");
    zoomInAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal));
    connect(zoomInAction, &QAction::triggered, m_scoreWidget, &ScoreWidget::zoomIn);

    connect(m_scoreWidget, &ScoreWidget::zoomChanged, [this](double zoom) {
        m_zoomLabel->setText(QString("%1%").arg(static_cast<int>(zoom * 100)));
    });

    connect(m_audioPlayer, &AudioPlayer::playbackStarted, [this]() {
        m_playPauseAction->setText("Pause");
    });
    connect(m_audioPlayer, &AudioPlayer::playbackPaused, [this]() {
        m_playPauseAction->setText("Play");
    });
    connect(m_audioPlayer, &AudioPlayer::playbackStopped, [this]() {
        m_playPauseAction->setText("Play");
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
    m_score->setShowVBox(m_displaySettings->showTitleFrame());

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

    setWindowTitle(QString("ScoreTracker - %1").arg(fi.fileName()));

    return true;
}

bool App::loadAudio(const QString& audioPath)
{
    if (!m_audioPlayer->load(audioPath)) {
        return false;
    }

    m_seekSlider->setRange(0, static_cast<int>(m_audioPlayer->duration() * 10));
    m_timeLabel->setText(QString("0:00 / %1").arg(formatTime(m_audioPlayer->duration())));

    return true;
}

void App::togglePlayPause()
{
    qDebug() << "togglePlayPause called, isPlaying:" << m_audioPlayer->isPlaying()
             << "duration:" << m_audioPlayer->duration();
    if (m_audioPlayer->isPlaying()) {
        m_audioPlayer->pause();
    } else {
        m_audioPlayer->play();
    }
}

void App::onSeekSliderMoved(int value)
{
    double duration = m_audioPlayer->duration();
    double seconds = (static_cast<double>(value) / m_seekSlider->maximum()) * duration;
    m_audioPlayer->seekTo(seconds);
    m_syncTimer->setTime(seconds);
}

void App::onPositionChanged(double seconds)
{
    // Update slider position
    if (!m_sliderDragging) {
        double duration = m_audioPlayer->duration();
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
        .arg(formatTime(m_audioPlayer->duration())));

    // Update sync timer -> cursor (only if tracking is on)
    if (m_trackingAction->isChecked()) {
        m_syncTimer->setTime(seconds);
    }
}

QString App::formatTime(double seconds) const
{
    int totalSecs = static_cast<int>(seconds);
    int mins = totalSecs / 60;
    int secs = totalSecs % 60;
    return QString("%1:%2").arg(mins).arg(secs, 2, 10, QChar('0'));
}

} // namespace scoretracker
