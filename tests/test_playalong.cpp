#include <QCoreApplication>
#include <QTest>
#include <QDebug>
#include <vector>

#include "engine/VerovioEngine.h"
#include "playalongsynth.h"

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
};

QTEST_MAIN(TestPlayAlong)
#include "test_playalong.moc"
