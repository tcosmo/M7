#include "playalongsynth.h"

#include <fluidsynth.h>
#include <miniaudio/miniaudio.h>

#include "engraving/dom/score.h"
#include "engraving/dom/part.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/note.h"
#include "engraving/dom/spanner.h"
#include "engraving/dom/spannermap.h"
#include "engraving/types/types.h"

#include <QDebug>
#include <algorithm>

using namespace mu::engraving;

namespace scoretracker {

// Compute the MIDI pitch one diatonic step above in the given key
static int trillUpperPitch(int midiPitch, Key key)
{
    static const int scaleIntervals[7] = {0, 2, 4, 5, 7, 9, 11};
    int tonic = ((static_cast<int>(key) * 7) % 12 + 12) % 12;
    int pc = midiPitch % 12;

    for (int i = 0; i < 7; ++i) {
        int scalePc = (tonic + scaleIntervals[i]) % 12;
        if (scalePc == pc) {
            int nextPc = (tonic + scaleIntervals[(i + 1) % 7]) % 12;
            int semitones = (nextPc - pc + 12) % 12;
            return midiPitch + semitones;
        }
    }
    // Not in scale — default to whole step up
    return midiPitch + 2;
}

// Mark notes that fall under trill spanners
static void annotateTrills(Score* score, const Part* part, std::vector<NoteEvent>& notes)
{
    if (notes.empty()) return;
    int firstTick = notes.front().tick;
    int lastTick = notes.back().tick + notes.back().durationTicks;
    const auto& overlapping = score->spannerMap().findOverlapping(firstTick, lastTick);

    for (const auto& iv : overlapping) {
        Spanner* sp = iv.value;
        if (!sp->isTrill()) continue;
        int trillStart = sp->tick().ticks();
        int trillEnd = sp->tick2().ticks();

        // Get key at trill start from the part's first staff
        Key key = part->staff(0)->key(Fraction::fromTicks(trillStart));

        for (auto& ne : notes) {
            if (ne.tick >= trillStart && ne.tick < trillEnd) {
                ne.hasTrill = true;
                ne.trillPitch = trillUpperPitch(ne.midiPitch, key);
            }
        }
    }
}

// Accessor helpers for void* members
static fluid_synth_t* fs(void* p) { return static_cast<fluid_synth_t*>(p); }
static fluid_settings_t* fsettings(void* p) { return static_cast<fluid_settings_t*>(p); }
static ma_device* madev(void* p) { return static_cast<ma_device*>(p); }

static void audioCallback(ma_device* dev, void* output, const void* /*input*/, ma_uint32 frameCount)
{
    auto* s = static_cast<fluid_synth_t*>(dev->pUserData);
    fluid_synth_write_float(s, static_cast<int>(frameCount),
                            output, 0, 2,
                            output, 1, 2);
}


PlayAlongSynth::PlayAlongSynth()
{
    // No hardcoded voice — starts empty until setVoice() is called
}

PlayAlongSynth::~PlayAlongSynth()
{
    stop();
}

bool PlayAlongSynth::init(const QString& sf3Path)
{
    m_settings = new_fluid_settings();
    if (!m_settings) return false;

    fluid_settings_setnum(fsettings(m_settings), "synth.sample-rate", 44100.0);
    fluid_settings_setint(fsettings(m_settings), "synth.audio-channels", 1);
    fluid_settings_setnum(fsettings(m_settings), "synth.gain", m_gain);

    m_synth = new_fluid_synth(fsettings(m_settings));
    if (!m_synth) return false;

    m_sfontId = fluid_synth_sfload(fs(m_synth), sf3Path.toUtf8().constData(), 1);
    if (m_sfontId < 0) {
        qWarning() << "PlayAlongSynth: failed to load SF3:" << sf3Path;
        return false;
    }

    // Set up GM programs for any existing voices
    for (const auto& v : m_voices) {
        fluid_synth_program_change(fs(m_synth), v.channel, v.gmProgram);
    }

    // Set up miniaudio device
    m_device = new ma_device;
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = 44100;
    config.dataCallback = audioCallback;
    config.pUserData = m_synth;

    if (ma_device_init(nullptr, &config, madev(m_device)) != MA_SUCCESS) {
        qWarning() << "PlayAlongSynth: failed to init audio device";
        delete madev(m_device);
        m_device = nullptr;
        return false;
    }

    if (ma_device_start(madev(m_device)) != MA_SUCCESS) {
        qWarning() << "PlayAlongSynth: failed to start audio device";
        ma_device_uninit(madev(m_device));
        delete madev(m_device);
        m_device = nullptr;
        return false;
    }

    qDebug() << "PlayAlongSynth: initialized with" << m_voices.size() << "voices";
    return true;
}

bool PlayAlongSynth::loadSoundfont(const QString& path)
{
    if (!m_synth) return false;
    stopNote();

    // Unload previous soundfont
    if (m_sfontId >= 0) {
        fluid_synth_sfunload(fs(m_synth), m_sfontId, 1);
        m_sfontId = -1;
    }

    m_sfontId = fluid_synth_sfload(fs(m_synth), path.toUtf8().constData(), 1);
    if (m_sfontId < 0) {
        qWarning() << "PlayAlongSynth: failed to load soundfont:" << path;
        return false;
    }

    // Re-apply GM programs for existing voices
    for (const auto& v : m_voices) {
        fluid_synth_program_change(fs(m_synth), v.channel, v.gmProgram);
    }
    // Re-apply pitch bend
    setPitchOffset(m_pitchOffset);

    qDebug() << "PlayAlongSynth: loaded soundfont:" << path;
    return true;
}

void PlayAlongSynth::clearVoices()
{
    stopNote();
    m_voices.clear();
}

int PlayAlongSynth::ensureSoundfont(const QString& path)
{
    if (!m_synth) return -1;

    auto it = m_loadedSfonts.find(path);
    if (it != m_loadedSfonts.end()) return it->second;

    int id = fluid_synth_sfload(fs(m_synth), path.toUtf8().constData(), 0); // 0 = don't reset
    if (id < 0) {
        qWarning() << "PlayAlongSynth: failed to load soundfont:" << path;
        return -1;
    }
    m_loadedSfonts[path] = id;
    qDebug() << "PlayAlongSynth: ensureSoundfont loaded:" << path << "id:" << id;
    return id;
}

void PlayAlongSynth::addVoice(Part* part, int gmProg, int sfontId, Score* score)
{
    if (!part || !score) return;

    int channel = static_cast<int>(m_voices.size()); // each voice on its own channel
    QString name = part->partName().toQString();
    Voice voice{name, gmProg, channel, -1, 0, {}};

    staff_idx_t startStaff = part->startTrack() / VOICES;
    track_idx_t startTrack = startStaff * VOICES;
    track_idx_t endTrack = startTrack + VOICES;

    for (Segment* seg = score->firstSegment(SegmentType::ChordRest);
         seg; seg = seg->next1(SegmentType::ChordRest)) {
        for (track_idx_t track = startTrack; track < endTrack; ++track) {
            EngravingItem* e = seg->element(track);
            if (!e || !e->isChord()) continue;
            Chord* chord = static_cast<Chord*>(e);
            int tick = seg->tick().ticks();
            int durTicks = chord->actualTicks().ticks();
            const auto& notes = chord->notes();
            if (!notes.empty()) {
                Note* note = notes.front();
                bool tied = note->tieBack() != nullptr;
                voice.notes.push_back({tick, note->pitch(), durTicks, note, tied});
            }
        }
    }

    std::sort(voice.notes.begin(), voice.notes.end(),
              [](const NoteEvent& a, const NoteEvent& b) { return a.tick < b.tick; });

    m_voices.push_back(std::move(voice));

    // Select the right soundfont + program on this channel
    if (m_synth) {
        int bank = gmProg / 128;
        int prog = gmProg % 128;
        if (sfontId >= 0) {
            fluid_synth_program_select(fs(m_synth), channel, sfontId, bank, prog);
        } else {
            fluid_synth_bank_select(fs(m_synth), channel, bank);
            fluid_synth_program_change(fs(m_synth), channel, prog);
        }
        // Apply pitch bend
        double frac = m_pitchOffset - static_cast<int>(m_pitchOffset);
        int bend = 8192 + static_cast<int>(frac * 8192.0 / 2.0);
        bend = std::clamp(bend, 0, 16383);
        fluid_synth_pitch_bend(fs(m_synth), channel, bend);
    }

    qDebug() << "PlayAlongSynth: addVoice" << name << "channel" << channel
             << "program" << gmProg << "notes:" << m_voices.back().notes.size();
}

void PlayAlongSynth::playNextNoteForVoice(int voiceIdx)
{
    if (!m_synth || voiceIdx < 0 || voiceIdx >= static_cast<int>(m_voices.size())) return;

    auto* s = fs(m_synth);
    auto& v = m_voices[voiceIdx];

    if (v.nextIndex >= static_cast<int>(v.notes.size())) return;

    int pitch = v.notes[v.nextIndex].midiPitch + static_cast<int>(m_pitchOffset);
    pitch = std::clamp(pitch, 0, 127);

    if (v.lastPlayedNote >= 0) {
        fluid_synth_noteoff(s, v.channel, v.lastPlayedNote);
    }

    fluid_synth_noteon(s, v.channel, pitch, 100);
    v.lastPlayedNote = pitch;
    v.nextIndex++;

    // Skip tied notes
    while (v.nextIndex < static_cast<int>(v.notes.size()) && v.notes[v.nextIndex].tiedBack) {
        v.nextIndex++;
    }
}

void PlayAlongSynth::stopNoteForVoice(int voiceIdx)
{
    if (!m_synth || voiceIdx < 0 || voiceIdx >= static_cast<int>(m_voices.size())) return;

    auto& v = m_voices[voiceIdx];
    if (v.lastPlayedNote >= 0) {
        fluid_synth_noteoff(fs(m_synth), v.channel, v.lastPlayedNote);
        v.lastPlayedNote = -1;
    }
}

void* PlayAlongSynth::nextNoteElementForVoice(int voiceIdx) const
{
    if (voiceIdx < 0 || voiceIdx >= static_cast<int>(m_voices.size())) return nullptr;
    const auto& v = m_voices[voiceIdx];
    if (v.nextIndex < static_cast<int>(v.notes.size())) {
        return v.notes[v.nextIndex].element;
    }
    return nullptr;
}

int PlayAlongSynth::voiceCount() const
{
    return static_cast<int>(m_voices.size());
}

QList<QPair<int, QString>> PlayAlongSynth::presets() const
{
    QList<QPair<int, QString>> result;
    if (!m_synth || m_sfontId < 0) return result;

    fluid_sfont_t* sfont = fluid_synth_get_sfont_by_id(fs(const_cast<void*>(m_synth)), m_sfontId);
    if (!sfont) return result;

    fluid_sfont_iteration_start(sfont);
    fluid_preset_t* preset;
    while ((preset = fluid_sfont_iteration_next(sfont)) != nullptr) {
        int bank = fluid_preset_get_banknum(preset);
        int prog = fluid_preset_get_num(preset);
        QString name = QString::fromUtf8(fluid_preset_get_name(preset)).trimmed();
        // Encode bank and program: bank * 128 + program
        int key = bank * 128 + prog;
        result.append({key, name});
    }

    // Sort by encoded key (bank first, then program)
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    return result;
}

void PlayAlongSynth::setVoice(Part* part, int gmProg, Score* score)
{
    if (!part || !score) return;

    // Stop any currently playing note
    stopNote();

    // Clear existing voices
    m_voices.clear();

    // Build the new voice from the part's track range
    QString name = part->partName().toQString();
    Voice voice{name, gmProg, 0, -1, 0, {}};

    // Build note table using the part's track range directly
    voice.notes.clear();
    staff_idx_t startStaff = part->startTrack() / VOICES;
    track_idx_t startTrack = startStaff * VOICES;
    track_idx_t endTrack = startTrack + VOICES;

    for (Segment* seg = score->firstSegment(SegmentType::ChordRest);
         seg; seg = seg->next1(SegmentType::ChordRest)) {
        for (track_idx_t track = startTrack; track < endTrack; ++track) {
            EngravingItem* e = seg->element(track);
            if (!e || !e->isChord()) continue;
            Chord* chord = static_cast<Chord*>(e);
            int tick = seg->tick().ticks();
            int durTicks = chord->actualTicks().ticks();
            const auto& notes = chord->notes();
            if (!notes.empty()) {
                Note* note = notes.front();
                bool tied = note->tieBack() != nullptr;
                voice.notes.push_back({tick, note->pitch(), durTicks, note, tied});
            }
        }
    }

    std::sort(voice.notes.begin(), voice.notes.end(),
              [](const NoteEvent& a, const NoteEvent& b) { return a.tick < b.tick; });

    annotateTrills(score, part, voice.notes);

    m_voices.push_back(std::move(voice));

    // Set GM program if synth is already initialized
    if (m_synth) {
        fluid_synth_program_change(fs(m_synth), 0, gmProg);
    }

    qDebug() << "PlayAlongSynth: setVoice" << name << "program" << gmProg
             << "notes:" << m_voices[0].notes.size();
}

void PlayAlongSynth::setGmProgram(int program)
{
    // program may be encoded as bank*128 + preset
    int bank = program / 128;
    int prog = program % 128;
    if (!m_voices.empty()) {
        m_voices[0].gmProgram = prog;
    }
    if (m_synth) {
        fluid_synth_bank_select(fs(m_synth), 0, bank);
        fluid_synth_program_change(fs(m_synth), 0, prog);
    }
}

int PlayAlongSynth::gmProgram() const
{
    return m_voices.empty() ? -1 : m_voices[0].gmProgram;
}

void PlayAlongSynth::setGain(double gain)
{
    m_gain = gain;
    if (m_synth) {
        fluid_synth_set_gain(fs(m_synth), static_cast<float>(gain));
    }
}

double PlayAlongSynth::gain() const
{
    return m_gain;
}

void PlayAlongSynth::setPitchOffset(double semitones)
{
    m_pitchOffset = semitones;
    // Apply fractional part as pitch bend (default range ±2 semitones)
    if (m_synth) {
        double frac = semitones - static_cast<int>(semitones);
        // Pitch bend: 8192 = center, range is ±2 semitones = ±8192
        int bend = 8192 + static_cast<int>(frac * 8192.0 / 2.0);
        bend = std::clamp(bend, 0, 16383);
        for (const auto& v : m_voices) {
            fluid_synth_pitch_bend(fs(m_synth), v.channel, bend);
        }
    }
}

double PlayAlongSynth::pitchOffset() const
{
    return m_pitchOffset;
}

void PlayAlongSynth::buildNoteTables(Score* score)
{
    if (!score) return;
    for (auto& v : m_voices) {
        buildNoteTableForPart(score, v);
    }
}

void PlayAlongSynth::buildNoteTableForPart(Score* score, Voice& voice)
{
    voice.notes.clear();

    // Find matching part by long instrument name
    const Part* matchedPart = nullptr;
    for (const Part* part : score->parts()) {
        const Instrument* inst = part->instrument();
        if (!inst->longNames().empty()) {
            QString longName = inst->longNames().front().name().toQString();
            if (longName == voice.partName) {
                matchedPart = part;
                break;
            }
        }
    }

    if (!matchedPart) {
        qWarning() << "PlayAlongSynth: part not found:" << voice.partName;
        return;
    }

    staff_idx_t startStaff = matchedPart->startTrack() / VOICES;
    track_idx_t startTrack = startStaff * VOICES;
    track_idx_t endTrack = startTrack + VOICES;

    for (Segment* seg = score->firstSegment(SegmentType::ChordRest);
         seg; seg = seg->next1(SegmentType::ChordRest)) {
        for (track_idx_t track = startTrack; track < endTrack; ++track) {
            EngravingItem* e = seg->element(track);
            if (!e || !e->isChord()) continue;
            Chord* chord = static_cast<Chord*>(e);
            int tick = seg->tick().ticks();
            int durTicks = chord->actualTicks().ticks();
            const auto& notes = chord->notes();
            if (!notes.empty()) {
                Note* note = notes.front();
                bool tied = note->tieBack() != nullptr;
                voice.notes.push_back({tick, note->pitch(), durTicks, note, tied});
            }
        }
    }

    std::sort(voice.notes.begin(), voice.notes.end(),
              [](const NoteEvent& a, const NoteEvent& b) { return a.tick < b.tick; });

    annotateTrills(score, matchedPart, voice.notes);

    qDebug() << "PlayAlongSynth: built" << voice.notes.size() << "notes for" << voice.partName;
}

void PlayAlongSynth::playNextNote()
{
    if (!m_synth) return;

    auto* s = fs(m_synth);

    for (auto& v : m_voices) {
        if (v.nextIndex >= static_cast<int>(v.notes.size())) continue;

        int pitch = v.notes[v.nextIndex].midiPitch + static_cast<int>(m_pitchOffset);
        pitch = std::clamp(pitch, 0, 127);

        // Turn off previous note on this channel
        if (v.lastPlayedNote >= 0) {
            fluid_synth_noteoff(s, v.channel, v.lastPlayedNote);
        }

        fluid_synth_noteon(s, v.channel, pitch, 100);
        v.lastPlayedNote = pitch;
        v.nextIndex++;

        // Skip any tied continuation notes (same pitch sustained)
        while (v.nextIndex < static_cast<int>(v.notes.size()) && v.notes[v.nextIndex].tiedBack) {
            v.nextIndex++;
        }
    }
    m_trillOnUpper = false;
}

void PlayAlongSynth::stopNote()
{
    if (!m_synth) return;

    auto* s = fs(m_synth);
    for (auto& v : m_voices) {
        if (v.lastPlayedNote >= 0) {
            fluid_synth_noteoff(s, v.channel, v.lastPlayedNote);
            v.lastPlayedNote = -1;
        }
    }
}

void PlayAlongSynth::resetPosition()
{
    stopNote();
    for (auto& v : m_voices) {
        v.nextIndex = 0;
    }
    m_trillOnUpper = false;
}

void PlayAlongSynth::stop()
{
    stopNote();

    if (m_device) {
        ma_device_uninit(madev(m_device));
        delete madev(m_device);
        m_device = nullptr;
    }

    if (m_synth) {
        delete_fluid_synth(fs(m_synth));
        m_synth = nullptr;
    }

    if (m_settings) {
        delete_fluid_settings(fsettings(m_settings));
        m_settings = nullptr;
    }
}

bool PlayAlongSynth::advanceTiedNotes(int currentTick)
{
    bool advanced = false;
    for (auto& v : m_voices) {
        while (v.nextIndex < static_cast<int>(v.notes.size())) {
            const auto& next = v.notes[v.nextIndex];
            if (!next.tiedBack) break;          // not a tie — wait for keypress
            if (next.tick > currentTick) break;  // cursor hasn't reached it yet
            // Auto-advance: tied note reached by cursor
            // No noteoff/noteon needed — same pitch sustains
            v.nextIndex++;
            advanced = true;
        }
    }
    return advanced;
}

void* PlayAlongSynth::nextNoteElement() const
{
    // Return the element from the first voice that still has notes
    for (const auto& v : m_voices) {
        if (v.nextIndex < static_cast<int>(v.notes.size())) {
            return v.notes[v.nextIndex].element;
        }
    }
    return nullptr;
}

bool PlayAlongSynth::currentNoteHasTrill() const
{
    for (const auto& v : m_voices) {
        int idx = v.nextIndex - 1;
        if (idx >= 0 && idx < static_cast<int>(v.notes.size())) {
            return v.notes[idx].hasTrill;
        }
    }
    return false;
}

void PlayAlongSynth::trillToggle()
{
    if (!m_synth) return;
    auto* s = fs(m_synth);

    for (auto& v : m_voices) {
        int idx = v.nextIndex - 1;
        if (idx < 0 || idx >= static_cast<int>(v.notes.size())) continue;
        const auto& ne = v.notes[idx];
        if (!ne.hasTrill) continue;

        int intOffset = static_cast<int>(m_pitchOffset);
        int oldPitch = std::clamp((m_trillOnUpper ? ne.trillPitch : ne.midiPitch) + intOffset, 0, 127);
        int newPitch = std::clamp((m_trillOnUpper ? ne.midiPitch : ne.trillPitch) + intOffset, 0, 127);

        fluid_synth_noteoff(s, v.channel, oldPitch);
        fluid_synth_noteon(s, v.channel, newPitch, 100);
        v.lastPlayedNote = newPitch;
    }
    m_trillOnUpper = !m_trillOnUpper;
}

} // namespace scoretracker
