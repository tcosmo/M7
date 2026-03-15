#include "fluidsynth_addon.h"
#include <algorithm>
#include <cmath>

namespace fluidsynth_addon {

// ── Global synth state ───────────────────────────────────────────
static SynthState g_state;

// ── Init ─────────────────────────────────────────────────────────
// arg0: string sampleRate (e.g. "44100")
Napi::Boolean Init(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    // Already initialized — return success (idempotent)
    if (g_state.synth && g_state.driver) {
        return Napi::Boolean::New(env, true);
    }

    // Clean up any partial state
    if (g_state.driver) {
        delete_fluid_audio_driver(g_state.driver);
        g_state.driver = nullptr;
    }
    if (g_state.synth) {
        delete_fluid_synth(g_state.synth);
        g_state.synth = nullptr;
    }
    if (g_state.settings) {
        delete_fluid_settings(g_state.settings);
        g_state.settings = nullptr;
    }
    g_state.defaultSfontId = -1;

    // Create settings
    g_state.settings = new_fluid_settings();
    if (!g_state.settings) {
        return Napi::Boolean::New(env, false);
    }

    // Sample rate from argument or default
    std::string sampleRate = "44100";
    if (info.Length() > 0 && info[0].IsString()) {
        sampleRate = info[0].As<Napi::String>().Utf8Value();
    }
    fluid_settings_setstr(g_state.settings, "audio.driver", "coreaudio");
    fluid_settings_setnum(g_state.settings, "synth.sample-rate", std::stod(sampleRate));
    fluid_settings_setint(g_state.settings, "synth.audio-channels", 1);
    fluid_settings_setnum(g_state.settings, "synth.gain", g_state.gain);
    // Audio buffer: 256 frames x 2 periods = ~11.6ms at 44.1kHz
    fluid_settings_setint(g_state.settings, "audio.period-size", 256);
    fluid_settings_setint(g_state.settings, "audio.periods", 2);

    // Create synth
    g_state.synth = new_fluid_synth(g_state.settings);
    if (!g_state.synth) {
        delete_fluid_settings(g_state.settings);
        g_state.settings = nullptr;
        return Napi::Boolean::New(env, false);
    }

    // Create audio driver (runs on its own real-time thread)
    g_state.driver = new_fluid_audio_driver(g_state.settings, g_state.synth);
    if (!g_state.driver) {
        delete_fluid_synth(g_state.synth);
        g_state.synth = nullptr;
        delete_fluid_settings(g_state.settings);
        g_state.settings = nullptr;
        return Napi::Boolean::New(env, false);
    }

    return Napi::Boolean::New(env, true);
}

// ── LoadSoundfont ────────────────────────────────────────────────
// arg0: string path
// Returns sfont_id (or -1 on failure)
Napi::Number LoadSoundfont(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected string path").ThrowAsJavaScriptException();
        return Napi::Number::New(env, -1);
    }

    if (!g_state.synth) {
        return Napi::Number::New(env, -1);
    }

    std::string path = info[0].As<Napi::String>().Utf8Value();
    int sfontId = fluid_synth_sfload(g_state.synth, path.c_str(), 1);

    if (sfontId >= 0 && g_state.defaultSfontId < 0) {
        g_state.defaultSfontId = sfontId;
    }

    return Napi::Number::New(env, sfontId);
}

// ── NoteOn ───────────────────────────────────────────────────────
// arg0: int channel, arg1: int pitch, arg2: int velocity
void NoteOn(const Napi::CallbackInfo& info) {
    if (!g_state.synth) return;
    if (info.Length() < 3) return;

    int channel = info[0].As<Napi::Number>().Int32Value();
    int pitch = info[1].As<Napi::Number>().Int32Value();
    int velocity = info[2].As<Napi::Number>().Int32Value();

    pitch = std::clamp(pitch, 0, 127);
    velocity = std::clamp(velocity, 0, 127);

    fluid_synth_noteon(g_state.synth, channel, pitch, velocity);
}

// ── NoteOff ──────────────────────────────────────────────────────
// arg0: int channel, arg1: int pitch
void NoteOff(const Napi::CallbackInfo& info) {
    if (!g_state.synth) return;
    if (info.Length() < 2) return;

    int channel = info[0].As<Napi::Number>().Int32Value();
    int pitch = info[1].As<Napi::Number>().Int32Value();

    fluid_synth_noteoff(g_state.synth, channel, pitch);
}

// ── AllNotesOff ──────────────────────────────────────────────────
// arg0: int channel
void AllNotesOff(const Napi::CallbackInfo& info) {
    if (!g_state.synth) return;
    if (info.Length() < 1) return;

    int channel = info[0].As<Napi::Number>().Int32Value();
    fluid_synth_all_notes_off(g_state.synth, channel);
}

// ── ProgramSelect ────────────────────────────────────────────────
// arg0: int channel, arg1: int sfontId, arg2: int bank, arg3: int program
void ProgramSelect(const Napi::CallbackInfo& info) {
    if (!g_state.synth) return;
    if (info.Length() < 4) return;

    int channel = info[0].As<Napi::Number>().Int32Value();
    int sfontId = info[1].As<Napi::Number>().Int32Value();
    int bank = info[2].As<Napi::Number>().Int32Value();
    int program = info[3].As<Napi::Number>().Int32Value();

    fluid_synth_program_select(g_state.synth, channel, sfontId, bank, program);
}

// ── SetGain ──────────────────────────────────────────────────────
// arg0: double gain (0.0 - 5.0)
void SetGain(const Napi::CallbackInfo& info) {
    if (info.Length() < 1) return;

    double gain = info[0].As<Napi::Number>().DoubleValue();
    gain = std::clamp(gain, 0.0, 5.0);
    g_state.gain = gain;

    if (g_state.synth) {
        fluid_synth_set_gain(g_state.synth, static_cast<float>(gain));
    }
}

// ── GetGain ──────────────────────────────────────────────────────
Napi::Number GetGain(const Napi::CallbackInfo& info) {
    return Napi::Number::New(info.Env(), g_state.gain);
}

// ── SetPitchOffset ───────────────────────────────────────────────
// arg0: int channel, arg1: double semitones
// Pitch bend: 8192 = center, default range +/- 2 semitones = +/- 8192
void SetPitchOffset(const Napi::CallbackInfo& info) {
    if (!g_state.synth) return;
    if (info.Length() < 2) return;

    int channel = info[0].As<Napi::Number>().Int32Value();
    double semitones = info[1].As<Napi::Number>().DoubleValue();

    g_state.pitchOffset = semitones;

    // Fractional part as pitch bend (default range is +/- 2 semitones)
    double frac = semitones - static_cast<int>(semitones);
    int bend = 8192 + static_cast<int>(frac * 8192.0 / 2.0);
    bend = std::clamp(bend, 0, 16383);

    fluid_synth_pitch_bend(g_state.synth, channel, bend);
}

// ── GetPresets ────────────────────────────────────────────────────
// arg0: int sfontId
// Returns array of {program: number, name: string}
Napi::Array GetPresets(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::Array result = Napi::Array::New(env);

    if (!g_state.synth) return result;

    int sfontId = g_state.defaultSfontId;
    if (info.Length() > 0 && info[0].IsNumber()) {
        sfontId = info[0].As<Napi::Number>().Int32Value();
    }

    if (sfontId < 0) return result;

    fluid_sfont_t* sfont = fluid_synth_get_sfont_by_id(g_state.synth, sfontId);
    if (!sfont) return result;

    // Collect presets
    struct PresetInfo {
        int key; // bank * 128 + program
        std::string name;
    };
    std::vector<PresetInfo> presets;

    fluid_sfont_iteration_start(sfont);
    fluid_preset_t* preset;
    while ((preset = fluid_sfont_iteration_next(sfont)) != nullptr) {
        int bank = fluid_preset_get_banknum(preset);
        int prog = fluid_preset_get_num(preset);
        const char* name = fluid_preset_get_name(preset);
        int key = bank * 128 + prog;

        std::string nameStr = name ? name : "";
        // Trim trailing whitespace
        while (!nameStr.empty() && nameStr.back() == ' ') {
            nameStr.pop_back();
        }

        presets.push_back({key, nameStr});
    }

    // Sort by encoded key (bank first, then program)
    std::sort(presets.begin(), presets.end(),
              [](const PresetInfo& a, const PresetInfo& b) { return a.key < b.key; });

    // Build JS array
    uint32_t idx = 0;
    for (const auto& p : presets) {
        Napi::Object obj = Napi::Object::New(env);
        obj.Set("program", Napi::Number::New(env, p.key));
        obj.Set("name", Napi::String::New(env, p.name));
        result.Set(idx++, obj);
    }

    return result;
}

// ── Shutdown ─────────────────────────────────────────────────────
void Shutdown(const Napi::CallbackInfo& /*info*/) {
    if (g_state.driver) {
        delete_fluid_audio_driver(g_state.driver);
        g_state.driver = nullptr;
    }
    if (g_state.synth) {
        delete_fluid_synth(g_state.synth);
        g_state.synth = nullptr;
    }
    if (g_state.settings) {
        delete_fluid_settings(g_state.settings);
        g_state.settings = nullptr;
    }
    g_state.defaultSfontId = -1;
}

// ── Module init ──────────────────────────────────────────────────
Napi::Object InitModule(Napi::Env env, Napi::Object exports) {
    exports.Set("init", Napi::Function::New(env, Init));
    exports.Set("loadSoundfont", Napi::Function::New(env, LoadSoundfont));
    exports.Set("noteOn", Napi::Function::New(env, NoteOn));
    exports.Set("noteOff", Napi::Function::New(env, NoteOff));
    exports.Set("allNotesOff", Napi::Function::New(env, AllNotesOff));
    exports.Set("programSelect", Napi::Function::New(env, ProgramSelect));
    exports.Set("setGain", Napi::Function::New(env, SetGain));
    exports.Set("getGain", Napi::Function::New(env, GetGain));
    exports.Set("setPitchOffset", Napi::Function::New(env, SetPitchOffset));
    exports.Set("getPresets", Napi::Function::New(env, GetPresets));
    exports.Set("shutdown", Napi::Function::New(env, Shutdown));
    return exports;
}

NODE_API_MODULE(fluidsynth_addon, InitModule)

} // namespace fluidsynth_addon
