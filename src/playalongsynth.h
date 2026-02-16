#pragma once

#include <vector>
#include <QString>

namespace mu::engraving {
class Score;
class Part;
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

class PlayAlongSynth
{
public:
    PlayAlongSynth();
    ~PlayAlongSynth();

    bool init(const QString& sf3Path);
    void buildNoteTables(mu::engraving::Score* score);
    void playNextNote();
    void stopNote();
    void stop();

    // Set the voice to play along with (clears previous, builds note table)
    void setVoice(mu::engraving::Part* part, int gmProgram, mu::engraving::Score* score);

    // Change GM instrument on the fly
    void setGmProgram(int program);
    int gmProgram() const;

    // Volume control (0.0 – 1.0)
    void setGain(double gain);
    double gain() const;

    // Returns the element (Note*) for the next note to be played, or nullptr
    void* nextNoteElement() const;

private:
    static void buildNoteTableForPart(mu::engraving::Score* score, Voice& voice);

    void* m_settings = nullptr;
    void* m_synth = nullptr;
    void* m_device = nullptr;
    int m_sfontId = -1;

    double m_gain = 0.6;
    std::vector<Voice> m_voices;
};

} // namespace scoretracker
