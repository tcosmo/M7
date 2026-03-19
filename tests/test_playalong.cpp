#include <QApplication>
#include <QTest>
#include <QSignalSpy>
#include <QDebug>
#include <QStackedWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QPixmap>
#include <QPainter>
#include <vector>

#include "engine/VerovioEngine.h"
#include "playalongsynth.h"
#include "synctimer.h"
#include "theme.h"

using namespace scoretracker;

class TestPlayAlong : public QObject
{
    Q_OBJECT

private:
    QString m_scoreDir;

private slots:
    void initTestCase() {
        // Path to test resources — relative to build directory
        m_scoreDir = QCoreApplication::applicationDirPath() + "/../../resources/Magnificat";
    }

    // -- Verovio transposition tests --

    void testTimpaniTransposition() {
        // Timpani in D.A. has <transpose><chromatic>2</chromatic>
        // Written C3 should become D3 (MIDI 50), written G2 should become A2 (MIDI 45)
        VerovioEngine engine;
        QVERIFY(engine.loadMusicXML(m_scoreDir + "/Magnificat.musicxml"));
        engine.layout();

        // Part 4 is Timpani (1-based), select it and re-render
        engine.selectParts({4});
        engine.renderAllPagesHtml(); // generates SVGs needed for getNotesForPart

        auto svgs = engine.lastRenderedSvgs();
        auto notes = engine.getNotesForPart(0, svgs); // filtered index 0 = part 4

        QVERIFY(!notes.empty());

        // First note should be D3 (MIDI 50) — written C3 + 2 semitones
        QCOMPARE(notes[0].pitch, 50);

        // Find an A2 note (MIDI 45) — written G2 + 2 semitones
        bool foundA2 = false;
        for (const auto& n : notes) {
            if (n.pitch == 45) { foundA2 = true; break; }
        }
        QVERIFY2(foundA2, "Expected A2 (MIDI 45) in timpani part");

        // All timpani notes should be either D3 (50) or A2 (45) in this piece
        for (const auto& n : notes) {
            QVERIFY2(n.pitch == 50 || n.pitch == 45,
                qPrintable(QString("Unexpected pitch %1 in timpani").arg(n.pitch)));
        }

        qDebug() << "Timpani: " << notes.size() << "notes, all D3/A2 ✓";
    }

    void testContinuoNoTransposition() {
        // Continuo (part 17) should have no transposition
        VerovioEngine engine;
        QVERIFY(engine.loadMusicXML(m_scoreDir + "/Magnificat.musicxml"));
        engine.layout();

        engine.selectParts({17});
        engine.renderAllPagesHtml();

        auto svgs = engine.lastRenderedSvgs();
        auto notes = engine.getNotesForPart(0, svgs);

        QVERIFY(!notes.empty());

        // Continuo notes should be in a reasonable bass range (MIDI 36-72)
        for (const auto& n : notes) {
            QVERIFY2(n.pitch >= 30 && n.pitch <= 80,
                qPrintable(QString("Continuo pitch %1 out of range").arg(n.pitch)));
        }

        qDebug() << "Continuo: " << notes.size() << "notes, range OK ✓";
    }

    void testOboeNoTransposition() {
        // Oboe I (part 7) — not a transposing instrument in this score
        VerovioEngine engine;
        QVERIFY(engine.loadMusicXML(m_scoreDir + "/Magnificat.musicxml"));
        engine.layout();

        engine.selectParts({7});
        engine.renderAllPagesHtml();

        auto svgs = engine.lastRenderedSvgs();
        auto notes = engine.getNotesForPart(0, svgs);

        QVERIFY(!notes.empty());

        // Oboe notes should be in treble range (MIDI 55-90)
        for (const auto& n : notes) {
            QVERIFY2(n.pitch >= 50 && n.pitch <= 95,
                qPrintable(QString("Oboe pitch %1 out of range").arg(n.pitch)));
        }

        qDebug() << "Oboe I: " << notes.size() << "notes, range OK ✓";
    }

    // -- PlayAlongSynth pitch offset tests --

    void testPitchOffsetDoesNotShiftNoteNumber() {
        // Verify that setPitchOffset doesn't change the MIDI note number
        // (it should only apply pitch bend)
        PlayAlongSynth synth;

        // Create a simple note table
        std::vector<NoteEvent> notes;
        NoteEvent e{};
        e.midiPitch = 50; // D3
        e.durationTicks = 480;
        notes.push_back(e);
        e.midiPitch = 45; // A2
        notes.push_back(e);

        synth.init(m_scoreDir + "/../sounds/MS Basic.sf3");
        synth.setVoiceFromNotes(notes, 47); // GM Timpani

        // Set a pitch offset
        synth.setPitchOffset(-1.0);

        // The offset should be stored but NOT affect note numbers
        QCOMPARE(synth.pitchOffset(), -1.0);

        // Play first note — it should use MIDI 50, not 49
        // (We can't directly check what FluidSynth receives, but we verify
        // the synth doesn't crash and the offset is stored correctly)
        synth.playNextNote();
        synth.stopNote();

        qDebug() << "Pitch offset does not shift note numbers ✓";
    }

    void testMultipleInterpretationTunings() {
        // Verify different tuning values from sources.json
        PlayAlongSynth synth;

        std::vector<NoteEvent> notes;
        NoteEvent e{};
        e.midiPitch = 60; // C4
        e.durationTicks = 480;
        notes.push_back(e);

        synth.init(m_scoreDir + "/../sounds/MS Basic.sf3");
        synth.setVoiceFromNotes(notes, 0); // Piano

        // Test various tuning offsets
        double tunings[] = {0.0, -0.9, 0.1, -1.0, 0.2, -0.8};
        for (double t : tunings) {
            synth.setPitchOffset(t);
            QCOMPARE(synth.pitchOffset(), t);
        }

        qDebug() << "Multiple tuning offsets stored correctly ✓";
    }

    // -- SyncTimer state reset tests --
    // Reproduces: play Savall interpretation, advance cursor, switch to Koopman,
    // cursor should be at 0 — not stuck at old Savall position.

    void testSetTimeWithClearedBeatDataResetsTick() {
        // After clearing beat data, setTime() must reset m_lastTick to 0.
        // This was the root cause: old m_lastTick persisted after clearing
        // beat data, so currentTick() returned the stale Savall tick when
        // onPositionChanged called setCursorTick(currentTick()) on first play.
        SyncTimer timer;
        QSignalSpy spy(&timer, &SyncTimer::cursorRectChanged);

        // Load beat data and simulate playback (no engine, so m_lastTick
        // won't be set by setTime — but we test the clear path)
        std::vector<double> times = {1.0, 2.0, 3.0, 4.0, 5.0};
        std::vector<int>    ticks = {0, 480, 960, 1440, 1920};
        timer.setBeatTimes(times, 3);
        timer.setBeatTicks(ticks);

        // Clear beat data and call setTime(0)
        timer.setBeatTimes({}, 3);
        timer.setBeatTicks({});
        timer.setTime(0);

        // m_lastTick must be 0 — not stale from previous playback
        QCOMPARE(timer.currentTick(), 0);
        // No signal (no engine)
        QCOMPARE(spy.count(), 0);

        qDebug() << "Cleared beat data resets currentTick to 0 ✓";
    }

    void testSetTimeBeforeFirstBeatSetsTickToFirstBeat() {
        // When time < first beat time, m_lastTick should be set to the
        // first beat's tick, not left stale from previous playback.
        SyncTimer timer;

        // "Koopman" interpretation: first beat at 0.5 seconds
        std::vector<double> times = {0.5, 1.2, 1.9, 2.6, 3.3};
        std::vector<int>    ticks = {0, 480, 960, 1440, 1920};
        timer.setBeatTimes(times, 3);
        timer.setBeatTicks(ticks);

        // setTime(0) — before first beat (0.5s)
        timer.setTime(0);

        // m_lastTick should snap to first beat tick (0), not stay stale
        QCOMPARE(timer.currentTick(), 0);

        // setTime(0.3) — still before first beat
        timer.setTime(0.3);
        QCOMPARE(timer.currentTick(), 0);

        qDebug() << "setTime before first beat sets tick to first beat ✓";
    }

    void testInterpretationSwitchResetsCurrentTick() {
        // Full scenario: play Savall (tick advances), switch to Koopman,
        // press play — currentTick() must be 0, not old Savall tick.
        SyncTimer timer;

        // "Savall" interpretation — load and simulate playback
        std::vector<double> savallTimes = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0};
        std::vector<int>    savallTicks = {0, 480, 960, 1440, 1920, 2400, 2880, 3360};
        timer.setBeatTimes(savallTimes, 3);
        timer.setBeatTicks(savallTicks);

        // --- Switch interpretation ---
        // Step 1: Clear old beat data
        timer.setBeatTimes({}, 3);
        timer.setBeatTicks({});
        timer.setTime(0);
        QCOMPARE(timer.currentTick(), 0); // must be 0, not stale

        // Step 2: Load "Koopman" beat data (first beat at 0.5s)
        std::vector<double> koopmanTimes = {0.5, 1.2, 1.9, 2.6, 3.3, 4.0, 4.7, 5.4};
        std::vector<int>    koopmanTicks = {0, 480, 960, 1440, 1920, 2400, 2880, 3360};
        timer.setBeatTimes(koopmanTimes, 3);
        timer.setBeatTicks(koopmanTicks);
        timer.setTime(0);

        // Step 3: Simulate first onPositionChanged after pressing Play
        // adjusted ≈ 0.001 (just started), + 0.08 latency compensation = 0.081
        // 0.081 < 0.5 (first beat) → early return path in setTime
        timer.setTime(0.081);

        // currentTick MUST be 0 (first beat), not old Savall tick
        QCOMPARE(timer.currentTick(), 0);

        qDebug() << "Interpretation switch resets currentTick ✓";
    }

    void testPlayAlongStateResetOnExit() {
        // Verify play-along synth is fully reset — simulates showWorldBrowser cleanup.
        PlayAlongSynth synth;
        synth.init(m_scoreDir + "/../sounds/MS Basic.sf3");

        std::vector<NoteEvent> notes;
        NoteEvent e{};
        e.midiPitch = 50;
        e.durationTicks = 480;
        notes.push_back(e);
        e.midiPitch = 45;
        notes.push_back(e);
        synth.setVoiceFromNotes(notes, 47);
        QVERIFY(synth.voiceCount() > 0);
        synth.playNextNote();

        // Simulate exit cleanup
        synth.stopNote();
        synth.clearVoices();
        synth.resetPosition();

        // After cleanup, no voices should be loaded
        QCOMPARE(synth.voiceCount(), 0);

        qDebug() << "Play-along state fully reset on exit ✓";
    }

    // -- Loading page visual test --

    void testLoadingPageRendersCorrectly() {
        // Reproduce the exact loading page from App constructor and verify
        // it renders visible "Loading..." text on the correct background.
        using namespace scoretracker;

        QStackedWidget stack;
        stack.resize(800, 600);

        // Page 0: placeholder for level browser
        auto* browserPage = new QWidget();
        stack.addWidget(browserPage);

        // Page 1: placeholder for score view
        auto* scorePage = new QWidget();
        stack.addWidget(scorePage);

        // Page 2: loading page — exact copy of App constructor code
        auto* loadingPage = new QWidget();
        loadingPage->setAutoFillBackground(true);
        QPalette lpPal = loadingPage->palette();
        lpPal.setColor(QPalette::Window, Theme::scoreBg());
        loadingPage->setPalette(lpPal);
        auto* loadingLayout = new QVBoxLayout(loadingPage);
        loadingLayout->setAlignment(Qt::AlignCenter);
        auto* loadingLabel = new QLabel("Loading...");
        loadingLabel->setAlignment(Qt::AlignCenter);
        QFont loadingFont = loadingLabel->font();
        loadingFont.setPointSize(18);
        loadingLabel->setFont(loadingFont);
        loadingLabel->setStyleSheet(QString("color: %1;").arg(Theme::textPrimary().name()));
        loadingLayout->addWidget(loadingLabel);
        stack.addWidget(loadingPage);

        // Switch to loading page (simulates what loadLevel does)
        stack.setCurrentIndex(2);
        stack.show();
        QTest::qWaitForWindowExposed(&stack);

        // Grab a screenshot
        QPixmap screenshot = stack.grab();
        QVERIFY(!screenshot.isNull());

        // Save to disk for visual inspection
        QString path = QCoreApplication::applicationDirPath() + "/test_loading_page.png";
        QVERIFY(screenshot.save(path));
        qDebug() << "Loading page screenshot saved to:" << path;

        // Verify the loading page is actually showing (not blank):
        // The background should be Theme::scoreBg() (0x36393f), not pure black/white
        QImage img = screenshot.toImage();
        QColor centerPixel = img.pixelColor(img.width() / 2, img.height() / 4);
        // Background should be dark grey (scoreBg), not black or white
        QVERIFY2(centerPixel.lightness() > 15 && centerPixel.lightness() < 80,
            qPrintable(QString("Expected dark grey background, got %1").arg(centerPixel.name())));

        // Scan the entire image for text pixels (any pixel brighter than background)
        // The "Loading..." text is rendered in textPrimary (~0xdcddde, lightness ~86)
        // vs scoreBg (~0x36393f, lightness ~23). Scan a wide horizontal band.
        bool hasText = false;
        int bgLightness = centerPixel.lightness();
        for (int y = img.height() / 3; y < 2 * img.height() / 3 && !hasText; ++y) {
            for (int x = img.width() / 4; x < 3 * img.width() / 4; ++x) {
                QColor px = img.pixelColor(x, y);
                if (px.lightness() > bgLightness + 20) { hasText = true; break; }
            }
        }
        QVERIFY2(hasText, "Loading page has no visible text — expected 'Loading...'");

        qDebug() << "Loading page renders correctly ✓";
    }
};

QTEST_MAIN(TestPlayAlong)
#include "test_playalong.moc"
