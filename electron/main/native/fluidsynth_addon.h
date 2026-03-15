#pragma once

#include <napi.h>
#include <fluidsynth.h>

/**
 * FluidSynth N-API addon for Electron.
 *
 * Wraps FluidSynth with its built-in CoreAudio driver so that
 * audio runs on FluidSynth's own real-time thread — no JS audio
 * callback overhead. All functions are called from the main thread
 * via IPC; noteOn/noteOff are synchronous and fast (~microseconds).
 */

namespace fluidsynth_addon {

// Module state — single global synth instance
struct SynthState {
    fluid_settings_t* settings = nullptr;
    fluid_synth_t* synth = nullptr;
    fluid_audio_driver_t* driver = nullptr;
    int defaultSfontId = -1;
    double gain = 0.6;
    double pitchOffset = 0.0;
};

// N-API exported functions
Napi::Boolean Init(const Napi::CallbackInfo& info);
Napi::Number LoadSoundfont(const Napi::CallbackInfo& info);
void NoteOn(const Napi::CallbackInfo& info);
void NoteOff(const Napi::CallbackInfo& info);
void AllNotesOff(const Napi::CallbackInfo& info);
void ProgramSelect(const Napi::CallbackInfo& info);
void SetGain(const Napi::CallbackInfo& info);
Napi::Number GetGain(const Napi::CallbackInfo& info);
void SetPitchOffset(const Napi::CallbackInfo& info);
Napi::Array GetPresets(const Napi::CallbackInfo& info);
void Shutdown(const Napi::CallbackInfo& info);

Napi::Object InitModule(Napi::Env env, Napi::Object exports);

} // namespace fluidsynth_addon
