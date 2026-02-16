#pragma once

#include <vector>
#include <QString>

namespace mu::engraving {
class Score;
}

namespace scoretracker {

struct NoteEvent {
    int tick;
    int midiPitch;
    int durationTicks;
    void* element = nullptr; // MuseScore Note* (opaque to avoid includes)
};

struct Voice {
    QString partName;       // long instrument name to match
    int gmProgram;          // GM program number
    int channel;            // MIDI channel
    int lastPlayedNote = -1;
    int nextIndex = 0;             // next note to play
    std::vector<NoteEvent> notes;  // sorted by tick
};

class TrumpetSynth
{
public:
    TrumpetSynth();
    ~TrumpetSynth();

    bool init(const QString& sf3Path);
    void buildNoteTables(mu::engraving::Score* score);
    void playNextNote();
    void stopNote();
    void stop();

    // Returns the element (Note*) for the next note to be played, or nullptr
    void* nextNoteElement() const;

private:
    static void buildNoteTableForPart(mu::engraving::Score* score, Voice& voice);

    void* m_settings = nullptr;
    void* m_synth = nullptr;
    void* m_device = nullptr;
    int m_sfontId = -1;

    std::vector<Voice> m_voices;
};

} // namespace scoretracker
