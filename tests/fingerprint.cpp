// The sound's fingerprint: renders fixed scenarios through the CLAP entry, as a host plays them, and prints a 64-bit
// hash of every output sample's bits for each. tests/fingerprint.txt holds the hashes the current sound gives, and
// `make test` fails if one changes: work meant to keep the sound (a speed-up, a clean-up) must leave them all as they
// are. A change meant to change the sound updates the file with `make fingerprint-update`.
//   fingerprint PLUGIN.clap [EXPECTED.txt]
// The scenarios: each factory preset, each cube at three points, random settings changed as it plays, other rates,
// other block sizes (which must hash the same as 128), the song stopped and jumping under a synced LFO. Hashes hold
// for this toolchain and these flags (Melodee's build image, -O3 -mcpu=cortex-a55 -ffp-contract=fast).
#include "signal.h"

#include <clap/clap.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace
{
const clap_plugin_factory* factory = nullptr;
const clap_host host { CLAP_VERSION, nullptr, "fingerprint", "", "", "1", [] (const clap_host*, const char*) -> const void* { return nullptr; },
                       [] (const clap_host*) {}, [] (const clap_host*) {}, [] (const clap_host*) {} };

struct Change
{
    uint64_t frame;
    std::string name;
    double value;
};

struct Scenario
{
    std::string name;
    std::string preset;                                 // a factory preset loaded first ("" none)
    std::vector<std::pair<std::string, double>> values; // then these
    std::vector<Change> changes;
    double rate = 48000.0, seconds = 4.0;
    uint32_t block = 128;
    int signal = 0; // 0 the loop, 1 noise
    // The transport for the block at frame `at` (seconds `t`): {playing, beats}. Default: playing from the top.
    std::function<std::pair<bool, double> (uint64_t, double)> transport;
};

uint64_t fnv (uint64_t h, const void* data, size_t n)
{
    const auto* p = static_cast<const uint8_t*> (data);
    for (size_t i = 0; i < n; ++i)
        h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

uint64_t render (const Scenario& s)
{
    const auto* d = factory->get_plugin_descriptor (factory, 0);
    const clap_plugin* plugin = factory->create_plugin (factory, &host, d->id);
    plugin->init (plugin);
    const auto* params = static_cast<const clap_plugin_params*> (plugin->get_extension (plugin, CLAP_EXT_PARAMS));
    std::map<std::string, clap_id> ids;
    for (uint32_t i = 0; i < params->count (plugin); ++i)
    {
        clap_param_info info {};
        params->get_info (plugin, i, &info);
        ids[info.name] = info.id;
    }
    if (! s.preset.empty())
    {
        const auto* presets = static_cast<const clap_plugin_preset_load*> (plugin->get_extension (plugin, CLAP_EXT_PRESET_LOAD));
        if (presets == nullptr || ! presets->from_location (plugin, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, s.preset.c_str()))
            std::printf ("no preset %s\n", s.preset.c_str());
    }
    plugin->activate (plugin, s.rate, 1, 4096);
    plugin->start_processing (plugin);

    std::vector<Change> changes;
    for (auto& [name, v] : s.values)
        changes.push_back ({ 0, name, v });
    changes.insert (changes.end(), s.changes.begin(), s.changes.end());
    const size_t frames = (size_t) (s.seconds * s.rate);
    const Stereo in = s.signal == 0 ? loop (s.rate, s.seconds) : whiteNoise (frames, 0.7f, 5);
    std::vector<float> l (s.block), r (s.block), allL, allR;
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t at = 0; at < frames; at += s.block)
    {
        const uint32_t n = (uint32_t) std::min<size_t> (s.block, frames - at);
        struct Events
        {
            std::vector<clap_event_param_value> list;
            clap_input_events in { this, [] (const clap_input_events* e) { return (uint32_t) static_cast<const Events*> (e->ctx)->list.size(); },
                                   [] (const clap_input_events* e, uint32_t i) { return &static_cast<const Events*> (e->ctx)->list[i].header; } };
        } events;
        for (const auto& c : changes)
            if (c.frame >= at && c.frame < at + n)
            {
                clap_event_param_value e {};
                e.header = { sizeof (e), (uint32_t) (c.frame - at), CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 };
                e.param_id = ids.at (c.name);
                e.note_id = e.port_index = e.channel = e.key = -1;
                e.value = c.value;
                events.list.push_back (e);
            }
        std::stable_sort (events.list.begin(), events.list.end(), [] (const auto& a, const auto& b) { return a.header.time < b.header.time; });
        const auto [playing, beats] = s.transport ? s.transport (at, (double) at / s.rate) : std::pair<bool, double> { true, (double) at / s.rate * 2.0 };
        clap_event_transport t {};
        t.header = { sizeof (t), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0 };
        t.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | (playing ? CLAP_TRANSPORT_IS_PLAYING : 0);
        t.tempo = 120.0;
        t.song_pos_beats = (clap_beattime) std::llround (beats * (double) CLAP_BEATTIME_FACTOR);
        float* ins[2] = { const_cast<float*> (in.l.data()) + at, const_cast<float*> (in.r.data()) + at };
        float* outs[2] = { l.data(), r.data() };
        clap_audio_buffer input {}, output {};
        input.data32 = ins, input.channel_count = 2;
        output.data32 = outs, output.channel_count = 2;
        clap_output_events out { nullptr, [] (const clap_output_events*, const clap_event_header*) { return true; } };
        clap_process p {};
        p.frames_count = n;
        p.transport = &t;
        p.audio_inputs = &input, p.audio_inputs_count = 1;
        p.audio_outputs = &output, p.audio_outputs_count = 1;
        p.in_events = &events.in;
        p.out_events = &out;
        plugin->process (plugin, &p);
        allL.insert (allL.end(), l.begin(), l.begin() + n);
        allR.insert (allR.end(), r.begin(), r.begin() + n);
    }
    // Left then right, whole: the same bytes whatever the block size.
    h = fnv (h, allL.data(), allL.size() * sizeof (float));
    h = fnv (h, allR.data(), allR.size() * sizeof (float));
    plugin->stop_processing (plugin);
    plugin->deactivate (plugin);
    plugin->destroy (plugin);
    return h;
}

std::vector<Scenario> scenarios()
{
    std::vector<Scenario> list;
    for (const char* preset : { "init", "talking-loop", "phase-drift", "envelope-wah", "harmonic-swirl", "bell-ringer", "acid-envelope",
                                "radio-drift", "wide-space", "chord-glow" })
    {
        Scenario s;
        s.name = std::string ("preset-") + preset;
        s.preset = preset;
        s.seconds = 6.0;
        list.push_back (s);
    }
    const double points[3][3] = { { 0.0, 0.3, 0.0 }, { 0.5, 0.5, 0.5 }, { 1.0, 0.8, 1.0 } };
    for (int c = 0; c < 16; ++c)
        for (int k = 0; k < 3; ++k)
        {
            Scenario s;
            s.name = "cube-" + std::to_string (c) + "-" + std::to_string (k);
            s.values = { { "Cube", c }, { "Morph", points[k][0] }, { "Freq", points[k][1] }, { "Transform", points[k][2] } };
            s.seconds = 1.5;
            s.signal = k == 1 ? 1 : 0;
            list.push_back (s);
        }
    // Random settings, changed as it plays (the parameter list as of 0.1.0: new parameters stay at their defaults
    // here, so the old hashes hold).
    const std::vector<std::pair<std::string, std::pair<double, double>>> order01 = {
        { "Cube", { 0, 15 } }, { "Morph", { 0, 1 } }, { "Freq", { 0, 1 } }, { "Transform", { 0, 1 } }, { "Emphasis", { -1, 1 } }, { "Glide", { 0, 1 } },
        { "Drive", { 0, 24 } }, { "Mix", { 0, 1 } }, { "Output", { -24, 12 } }, { "Spread", { 0, 1 } }, { "LFO Sync", { 0, 13 } }, { "LFO Rate", { 0, 1 } },
        { "LFO Shape", { 0, 6 } }, { "LFO Phase", { 0, 1 } }, { "LFO > Morph", { -1, 1 } }, { "LFO > Freq", { -1, 1 } }, { "LFO > Transform", { -1, 1 } },
        { "Env Gain", { 0, 24 } }, { "Env Attack", { 0, 1 } }, { "Env Release", { 0, 1 } }, { "Env > Morph", { -1, 1 } }, { "Env > Freq", { -1, 1 } },
        { "Env > Transform", { -1, 1 } },
    };
    for (int n = 0; n < 8; ++n)
    {
        std::mt19937 random (100 + n);
        Scenario s;
        s.name = "random-" + std::to_string (n);
        s.seconds = 3.0;
        for (const auto& [name, range] : order01)
        {
            std::uniform_real_distribution<double> any (range.first, range.second);
            double v = any (random);
            if (name == "Cube" || name == "LFO Sync" || name == "LFO Shape")
                v = std::round (v);
            s.values.push_back ({ name, v });
        }
        for (int k = 0; k < 40; ++k)
        {
            const auto& [name, range] = order01[random() % order01.size()];
            std::uniform_real_distribution<double> any (range.first, range.second);
            double v = any (random);
            if (name == "Cube" || name == "LFO Sync" || name == "LFO Shape")
                v = std::round (v);
            s.changes.push_back ({ (uint64_t) (random() % (uint32_t) (s.seconds * s.rate)), name, v });
        }
        list.push_back (s);
    }
    for (double rate : { 44100.0, 96000.0 })
    {
        Scenario s;
        s.name = "rate-" + std::to_string ((int) rate);
        s.preset = "wide-space";
        s.rate = rate;
        list.push_back (s);
    }
    for (uint32_t block : { 1u, 333u, 4096u })
    {
        Scenario s;
        s.name = "block-" + std::to_string (block); // the same hash as preset-talking-loop
        s.preset = "talking-loop";
        s.seconds = 6.0;
        s.block = block;
        list.push_back (s);
    }
    {
        Scenario s;
        s.name = "stop-and-jumps";
        s.preset = "harmonic-swirl";
        s.transport = [] (uint64_t, double t) -> std::pair<bool, double> {
            if (t < 1.0) return { true, t * 2.0 };
            if (t < 1.5) return { false, 2.0 };
            if (t < 2.5) return { true, 16.0 + (t - 1.5) * 2.0 };
            return { true, (t - 2.5) * 2.0 };
        };
        list.push_back (s);
    }
    return list;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: fingerprint PLUGIN.clap [EXPECTED.txt]\n");
        return 2;
    }
    void* library = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    const auto* entry = library ? static_cast<const clap_plugin_entry*> (dlsym (library, "clap_entry")) : nullptr;
    if (entry == nullptr || ! entry->init (argv[1]))
    {
        std::fprintf (stderr, "cannot load %s\n", argv[1]);
        return 1;
    }
    factory = static_cast<const clap_plugin_factory*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
    std::map<std::string, std::string> expected;
    if (argc > 2)
    {
        FILE* f = std::fopen (argv[2], "r");
        char name[128], hash[32];
        while (f != nullptr && std::fscanf (f, "%127s %31s", name, hash) == 2)
            expected[name] = hash;
        if (f != nullptr)
            std::fclose (f);
    }
    int changed = 0;
    std::map<std::string, std::string> got;
    for (const auto& s : scenarios())
    {
        char hash[32];
        std::snprintf (hash, sizeof (hash), "%016llx", (unsigned long long) render (s));
        got[s.name] = hash;
        std::printf ("%s %s\n", s.name.c_str(), hash);
        if (argc > 2 && expected[s.name] != hash)
        {
            std::fprintf (stderr, "fingerprint: %s is %s, was %s\n", s.name.c_str(), hash, expected[s.name].c_str());
            ++changed;
        }
    }
    entry->deinit();
    // Other block sizes must sound as 128 frames do, whatever the file says (an update must not accept a difference).
    for (const char* block : { "block-1", "block-333", "block-4096" })
        if (got[block] != got["preset-talking-loop"])
        {
            std::fprintf (stderr, "fingerprint: %s is %s, but 128-frame blocks give %s\n", block, got[block].c_str(), got["preset-talking-loop"].c_str());
            ++changed;
        }
    if (changed > 0)
        std::fprintf (stderr, "fingerprint: %d scenarios sound different\n", changed);
    return changed == 0 ? 0 : 1;
}
