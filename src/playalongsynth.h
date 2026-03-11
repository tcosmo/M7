#pragma once

#include <vector>
#include <map>
#include <QString>
#include <QList>
#include <QPair>

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
    bool tiedBack = false;   // continuation of a tie (auto-advance, no keypress)
    bool hasTrill = false;
    int trillPitch = 0;      // MIDI pitch of upper trill note
};

struct Voice {
    QString partName;       // long instrument name to match
    int gmProgram;          // GM program number
    int channel;            // MIDI channel
    int sfontId = -1;       // soundfont id for this voice
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
    bool loadSoundfont(const QString& path);

    // Returns list of (program number, preset name) from the loaded soundfont (bank 0 only)
    QList<QPair<int, QString>> presets() const;
    void buildNoteTables(mu::engraving::Score* score);
    void playNextNote();
    void stopNote();
    void resetPosition();
    void stop();

    // Auto-advance past tied notes when cursor reaches them
    // Returns true if any advancement happened (caller should update highlight)
    bool advanceTiedNotes(int currentTick);

    // Set the voice to play along with (clears previous, builds note table)
    void setVoice(mu::engraving::Part* part, int gmProgram, mu::engraving::Score* score);

    // Multi-voice support
    void clearVoices();
    int ensureSoundfont(const QString& path); // load if needed, return sfont_id
    void addVoice(mu::engraving::Part* part, int gmProgram, int sfontId, mu::engraving::Score* score);
    void playNextNoteForVoice(int voiceIdx);
    void stopNoteForVoice(int voiceIdx);
    void* nextNoteElementForVoice(int voiceIdx) const;
    int voiceCount() const;

    // Change GM instrument on the fly
    void setGmProgram(int program);
    int gmProgram() const;

    // Per-voice instrument changes (for multi-voice levels)
    void setGmProgramForVoice(int voiceIdx, int program);
    int gmProgramForVoice(int voiceIdx) const;
    void setSoundfontForVoice(int voiceIdx, const QString& path);
    int soundfontIdForVoice(int voiceIdx) const;
    QList<QPair<int, QString>> presetsForSoundfont(int sfontId) const;

    // Volume control (0.0 – 1.0)
    void setGain(double gain);
    double gain() const;

    // Pitch transposition in semitones (e.g. -1.5 = 1 semitone + 50 cents down)
    void setPitchOffset(double semitones);
    double pitchOffset() const;

    // Returns the element (Note*) for the next note to be played, or nullptr
    void* nextNoteElement() const;

    // Trill support
    bool currentNoteHasTrill() const;
    void trillToggle();

private:
    static void buildNoteTableForPart(mu::engraving::Score* score, Voice& voice);

    void* m_settings = nullptr;
    void* m_synth = nullptr;
    void* m_device = nullptr;
    int m_sfontId = -1;

    double m_gain = 0.6;
    double m_pitchOffset = 0;
    bool m_trillOnUpper = false;
    std::vector<Voice> m_voices;
    std::map<QString, int> m_loadedSfonts; // path → sfont_id
};

} // namespace scoretracker
