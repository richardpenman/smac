// SDL2 audio backend of the platform layer (platform/ui_platform.h, "audio"): what the original
// gets from DirectSound through soundx.dll. A software mixer of sample voices (linear resampling
// for the pitch, gain, pan, loop, fade-out) and generator streams (music sequencer, movie sound),
// mixed into one stereo float device stream on SDL's audio thread.
#include "platform/ui_platform.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace platform {
namespace {

struct Voice {
    int32_t id = 0;
    std::shared_ptr<const Pcm> pcm;
    VoiceParams p;
    double pos = 0;          // source frame
    float fade = 1.0f;       // fade-out gain
    float fade_step = 0.0f;  // per output frame (0: none)
};
struct Stream {
    int32_t id;
    StreamFn fn;
    void* user;
};

SDL_AudioDeviceID g_dev = 0;
int32_t g_rate = 44100;
int32_t g_next_id = 1;
std::vector<Voice> g_voices;
std::vector<Stream> g_streams;
std::vector<float> g_mix;

void mix_voice(Voice& v, float* out, int32_t frames) {
    const Pcm& s = *v.pcm;
    int32_t ch = s.channels > 1 ? 2 : 1;
    int64_t n = (int64_t)s.samples.size() / s.channels;
    if (n <= 0) {
        v.pcm.reset();
        return;
    }
    double step = (double)s.rate * v.p.pitch / g_rate;
    float gain = v.p.volume;
    float pan = std::max(-1.0f, std::min(1.0f, v.p.pan));
    float gl = gain * (pan > 0 ? 1.0f - pan : 1.0f), gr = gain * (pan < 0 ? 1.0f + pan : 1.0f);
    for (int32_t i = 0; i < frames; i++) {
        if (v.pos >= n) {
            if (!v.p.loop) {
                v.pcm.reset();
                return;
            }
            v.pos = std::fmod(v.pos, (double)n);
        }
        int64_t i0 = (int64_t)v.pos;
        int64_t i1 = i0 + 1 < n ? i0 + 1 : (v.p.loop ? 0 : i0);
        float t = (float)(v.pos - (double)i0);
        float l, r;
        if (ch == 1) {
            l = r = (s.samples[i0] * (1 - t) + s.samples[i1] * t) * (1.0f / 32768);
        } else {
            const int16_t* a = &s.samples[i0 * s.channels];
            const int16_t* b = &s.samples[i1 * s.channels];
            l = (a[0] * (1 - t) + b[0] * t) * (1.0f / 32768);
            r = (a[1] * (1 - t) + b[1] * t) * (1.0f / 32768);
        }
        out[2 * i] += l * gl * v.fade;
        out[2 * i + 1] += r * gr * v.fade;
        if (v.fade_step > 0) {
            v.fade -= v.fade_step;
            if (v.fade <= 0) {
                v.pcm.reset();
                return;
            }
        }
        v.pos += step;
    }
}

void callback(void*, Uint8* stream, int len) {
    int32_t frames = len / (int32_t)(2 * sizeof(float));
    g_mix.assign((size_t)frames * 2, 0.0f);
    for (Voice& v : g_voices)
        if (v.pcm) mix_voice(v, g_mix.data(), frames);
    g_voices.erase(std::remove_if(g_voices.begin(), g_voices.end(), [](const Voice& v) { return !v.pcm; }),
                   g_voices.end());
    for (size_t i = 0; i < g_streams.size();) {
        if (!g_streams[i].fn(g_streams[i].user, g_mix.data(), frames, g_rate)) {
            g_streams.erase(g_streams.begin() + (long)i);
            continue;
        }
        i++;
    }
    float* out = reinterpret_cast<float*>(stream);
    for (int32_t i = 0; i < frames * 2; i++) out[i] = std::max(-1.0f, std::min(1.0f, g_mix[(size_t)i]));
}

Voice* find(int32_t id) {
    for (Voice& v : g_voices)
        if (v.id == id && v.pcm) return &v;
    return nullptr;
}

}  // namespace

bool audio_init(bool enabled) {
    if (!enabled || g_dev) return g_dev != 0;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "SDL audio: %s (no sound)\n", SDL_GetError());
        return false;
    }
    SDL_AudioSpec want{}, have{};
    want.freq = 44100;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = callback;
    g_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!g_dev) {
        std::fprintf(stderr, "SDL audio: %s (no sound)\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    g_rate = have.freq;
    SDL_PauseAudioDevice(g_dev, 0);
    return true;
}

void audio_shutdown() {
    if (!g_dev) return;
    SDL_CloseAudioDevice(g_dev);
    g_dev = 0;
    g_voices.clear();
    g_streams.clear();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

bool audio_enabled() { return g_dev != 0; }

int32_t audio_play(const std::shared_ptr<const Pcm>& pcm, const VoiceParams& params) {
    if (!g_dev || !pcm || pcm->samples.empty()) return 0;
    Voice v;
    v.pcm = pcm;
    v.p = params;
    SDL_LockAudioDevice(g_dev);
    v.id = g_next_id++;
    g_voices.push_back(v);
    SDL_UnlockAudioDevice(g_dev);
    return v.id;
}

void audio_set(int32_t voice, const VoiceParams& params) {
    if (!g_dev) return;
    SDL_LockAudioDevice(g_dev);
    if (Voice* v = find(voice)) v->p = params;
    SDL_UnlockAudioDevice(g_dev);
}

void audio_stop(int32_t voice, int32_t fade_ms) {
    if (!g_dev) return;
    SDL_LockAudioDevice(g_dev);
    if (Voice* v = find(voice)) {
        if (fade_ms <= 0) v->pcm.reset();
        else v->fade_step = 1000.0f / ((float)fade_ms * (float)g_rate);
    }
    SDL_UnlockAudioDevice(g_dev);
}

bool audio_playing(int32_t voice) {
    if (!g_dev || voice <= 0) return false;
    SDL_LockAudioDevice(g_dev);
    bool on = find(voice) != nullptr;
    SDL_UnlockAudioDevice(g_dev);
    return on;
}

int32_t audio_stream_open(StreamFn fn, void* user) {
    if (!g_dev || !fn) return 0;
    SDL_LockAudioDevice(g_dev);
    int32_t id = g_next_id++;
    g_streams.push_back({id, fn, user});
    SDL_UnlockAudioDevice(g_dev);
    return id;
}

void audio_stream_close(int32_t id) {
    if (!g_dev) return;
    SDL_LockAudioDevice(g_dev);
    g_streams.erase(std::remove_if(g_streams.begin(), g_streams.end(), [&](const Stream& s) { return s.id == id; }),
                    g_streams.end());
    SDL_UnlockAudioDevice(g_dev);
}

void audio_lock() {
    if (g_dev) SDL_LockAudioDevice(g_dev);
}
void audio_unlock() {
    if (g_dev) SDL_UnlockAudioDevice(g_dev);
}

}  // namespace platform
