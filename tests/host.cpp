// Loads CortexFilter.clap as a host does and checks it: description and ports, parameters and their texts, state, presets,
// the filter's measured response against the response its screen draws (several cubes, points, Emphasis and rates),
// known filters measured with no help from the plug-in's maths, the same output whatever the block size, Mix 0 giving
// the input back to the bit, mono and missing inputs, in-place buffers, bounded output under abuse, and the page on
// Melodee's screen. Writes demo renders (OUT_FOLDER/demo-*.wav) and the page's pictures (OUT_FOLDER/screen-*.png).
//   host CortexFilter.clap OUT_FOLDER
#include "png.h"
#include "signal.h"
#include "wav.h"
#include "../src/melodee_screen.h"
#include "../src/filters.h"
#include "../src/morph.h"
#include "../src/presets.h"

#include <clap/clap.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace
{
int failures = 0, checks = 0;
#define CHECK(c) do { ++checks; if (! (c)) { ++failures; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define NEAR(a, b, tol) do { ++checks; const double _a = (a), _b = (b); if (! (std::fabs (_a - _b) <= (tol))) { ++failures; std::printf ("FAIL %s:%d  %s = %g, %s = %g (tolerance %g)\n", __FILE__, __LINE__, #a, _a, #b, _b, (double) (tol)); } } while (0)

const clap_plugin_entry* entry = nullptr;
const clap_plugin_factory* factory = nullptr;

int screenRuns = 0;
int rescans = 0, flushes = 0, paints = 0, presetsLoaded = 0, presetErrors = 0, processes = 0;
const void* hostExtension (const clap_host*, const char* id)
{
    static const clap_host_log log { [] (const clap_host*, clap_log_severity s, const char* m) { std::printf ("  [plug-in log %d] %s\n", (int) s, m); } };
    static const clap_host_params params { [] (const clap_host*, clap_param_rescan_flags f) { rescans += (f & CLAP_PARAM_RESCAN_VALUES) != 0; },
                                           [] (const clap_host*, clap_id, clap_param_clear_flags) {}, [] (const clap_host*) { ++flushes; } };
    static const melodee_screen_host_t screen { [] (const void*, uint32_t flags) {
        ++paints;
        if (flags & MELODEE_SCREEN_RUN)
            ++processes, ++screenRuns;
    } };
    static const clap_host_preset_load presetLoad {
        [] (const clap_host*, uint32_t, const char*, const char*, int32_t, const char*) { ++presetErrors; },
        [] (const clap_host*, uint32_t, const char*, const char*) { ++presetsLoaded; },
    };
    if (std::strcmp (id, MELODEE_SCREEN_CLAP_HOST) == 0) return &screen;
    if (std::strcmp (id, CLAP_EXT_LOG) == 0) return &log;
    if (std::strcmp (id, CLAP_EXT_PARAMS) == 0) return &params;
    if (std::strcmp (id, CLAP_EXT_PRESET_LOAD) == 0) return &presetLoad;
    return nullptr;
}

clap_host host {
    CLAP_VERSION, nullptr, "Cortex Filter test host", "tests", "", "1",
    hostExtension, [] (const clap_host*) {}, [] (const clap_host*) { ++processes; }, [] (const clap_host*) {},
};

// A parameter change at a frame of the render.
struct Change
{
    uint64_t frame;
    clap_id id;
    double value;
};

struct Events
{
    std::vector<clap_event_param_value> list;
    clap_input_events in { this, [] (const clap_input_events* e) { return (uint32_t) static_cast<const Events*> (e->ctx)->list.size(); },
                           [] (const clap_input_events* e, uint32_t i) { return &static_cast<const Events*> (e->ctx)->list[i].header; } };
    void param (uint32_t time, clap_id id, double value)
    {
        clap_event_param_value e {};
        e.header = { sizeof (e), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 };
        e.param_id = id;
        e.note_id = e.port_index = e.channel = e.key = -1;
        e.value = value;
        list.push_back (e);
    }
};

struct OutEvents
{
    std::vector<std::pair<clap_id, double>> values;
    clap_output_events out { this, [] (const clap_output_events* o, const clap_event_header* h) {
        if (h->space_id == CLAP_CORE_EVENT_SPACE_ID && h->type == CLAP_EVENT_PARAM_VALUE)
        {
            const auto* v = reinterpret_cast<const clap_event_param_value*> (h);
            static_cast<OutEvents*> (o->ctx)->values.push_back ({ v->param_id, v->value });
        }
        return true;
    } };
};

struct Instance
{
    const clap_plugin* plugin = nullptr;
    const clap_plugin_params* params = nullptr;
    const clap_plugin_state* state = nullptr;
    const clap_plugin_preset_load* presets = nullptr;
    std::map<std::string, clap_id> ids;
    double rate = 48000.0;
    uint64_t played = 0; // frames rendered, for the transport

    explicit Instance (double sampleRate = 48000.0, uint32_t maxFrames = 4096, const clap_host* with = &host) : rate (sampleRate)
    {
        const auto* d = factory->get_plugin_descriptor (factory, 0);
        plugin = factory->create_plugin (factory, with, d->id);
        CHECK (plugin != nullptr && plugin->init (plugin));
        params = static_cast<const clap_plugin_params*> (plugin->get_extension (plugin, CLAP_EXT_PARAMS));
        state = static_cast<const clap_plugin_state*> (plugin->get_extension (plugin, CLAP_EXT_STATE));
        presets = static_cast<const clap_plugin_preset_load*> (plugin->get_extension (plugin, CLAP_EXT_PRESET_LOAD));
        for (uint32_t i = 0; params && i < params->count (plugin); ++i)
        {
            clap_param_info info {};
            params->get_info (plugin, i, &info);
            ids[info.name] = info.id;
        }
        CHECK (plugin->activate (plugin, rate, 1, maxFrames));
        CHECK (plugin->start_processing (plugin));
    }
    ~Instance()
    {
        plugin->stop_processing (plugin);
        plugin->deactivate (plugin);
        plugin->destroy (plugin);
    }
    clap_id id (const std::string& name)
    {
        const auto it = ids.find (name);
        if (it == ids.end())
        {
            std::printf ("FAIL no parameter named \"%s\"\n", name.c_str());
            ++failures;
            return CLAP_INVALID_ID;
        }
        return it->second;
    }
    double value (const std::string& name)
    {
        double v = 0.0;
        params->get_value (plugin, id (name), &v);
        return v;
    }
    bool loadPreset (const char* key) { return presets != nullptr && presets->from_location (plugin, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, key); }

    // `in` through the plug-in in blocks of `block`, with `changes` at their frames, the song playing at 120 bpm from
    // where the last render ended. `inChannels` 0: no input port's buffers at all; 1: a mono input.
    Stereo run (const Stereo& in, uint32_t block, const std::vector<Change>& changes = {}, int inChannels = 2, OutEvents* out = nullptr, bool playing = true)
    {
        const size_t frames = in.l.size();
        Stereo o { std::vector<float> (frames, 0.0f), std::vector<float> (frames, 0.0f) };
        OutEvents ignored;
        for (size_t at = 0; at < frames; at += block)
        {
            const uint32_t n = (uint32_t) std::min<size_t> (block, frames - at);
            clap_event_transport tr {};
            tr.header = { sizeof (tr), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0 };
            tr.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | (playing ? CLAP_TRANSPORT_IS_PLAYING : 0);
            tr.tempo = 120.0;
            tr.song_pos_beats = (clap_beattime) std::llround ((double) (played + at) * 2.0 / rate * (double) CLAP_BEATTIME_FACTOR);
            Events e;
            for (const auto& c : changes)
                if (c.frame >= at && c.frame < at + n)
                    e.param ((uint32_t) (c.frame - at), c.id, c.value);
            std::stable_sort (e.list.begin(), e.list.end(), [] (const auto& a, const auto& b) { return a.header.time < b.header.time; });
            float* ins[2] = { const_cast<float*> (in.l.data()) + at, const_cast<float*> (in.r.data()) + at };
            float* outs[2] = { o.l.data() + at, o.r.data() + at };
            clap_audio_buffer input {}, output {};
            input.data32 = ins, input.channel_count = (uint32_t) inChannels;
            output.data32 = outs, output.channel_count = 2;
            clap_process p {};
            p.steady_time = (int64_t) (played + at);
            p.frames_count = n;
            p.transport = &tr;
            p.audio_inputs = inChannels > 0 ? &input : nullptr;
            p.audio_inputs_count = inChannels > 0 ? 1 : 0;
            p.audio_outputs = &output;
            p.audio_outputs_count = 1;
            p.in_events = &e.in;
            p.out_events = out ? &out->out : &ignored.out;
            CHECK (plugin->process (plugin, &p) != CLAP_PROCESS_ERROR);
        }
        played += frames;
        return o;
    }
    // Settings, then a short silence for every glide to land.
    void setup (const std::vector<std::pair<std::string, double>>& values, double settleSeconds = 0.3)
    {
        std::vector<Change> changes;
        for (auto& [name, v] : values)
            changes.push_back ({ 0, id (name), v });
        run (Stereo { std::vector<float> ((size_t) (settleSeconds * rate), 0.0f), std::vector<float> ((size_t) (settleSeconds * rate), 0.0f) }, 128, changes);
    }
};

// The spectrum of `x` at `hz`, in dB.
double spectrumDb (const std::vector<float>& x, double hz, double rate)
{
    const double step = 2.0 * M_PI * hz / rate;
    const double cr = std::cos (step), ci = -std::sin (step);
    double wr = 1.0, wi = 0.0, re = 0.0, im = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        re += x[i] * wr, im += x[i] * wi;
        const double nr = wr * cr - wi * ci;
        wi = wr * ci + wi * cr, wr = nr;
        if ((i & 4095) == 4095) // keep the rotation on the circle
        {
            const double m = 1.0 / std::sqrt (wr * wr + wi * wi);
            wr *= m, wi *= m;
        }
    }
    return 10.0 * std::log10 (re * re + im * im + 1e-30);
}

// The plug-in's impulse response at a setting (left channel; the impulse at 1/1000 so nothing nears the limit).
std::vector<float> impulseResponse (Instance& a, const std::vector<std::pair<std::string, double>>& values, size_t frames)
{
    a.setup (values);
    const Stereo out = a.run (impulse (frames, 1e-3f), 128);
    std::vector<float> h (out.l);
    for (float& v : h)
        v *= 1000.0f;
    return h;
}

void testDescription()
{
    CHECK (factory->get_plugin_count (factory) == 1);
    const auto* d = factory->get_plugin_descriptor (factory, 0);
    CHECK (std::string (d->id) == "com.hdslave.cortex-filter" && std::string (d->name) == "hdslave Cortex Filter" && std::string (d->vendor) == "hdslave");
    bool effect = false, filter = false, stereo = false, instrument = false;
    for (const char* const* f = d->features; *f != nullptr; ++f)
        effect |= std::string (*f) == CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, filter |= std::string (*f) == CLAP_PLUGIN_FEATURE_FILTER,
            stereo |= std::string (*f) == CLAP_PLUGIN_FEATURE_STEREO, instrument |= std::string (*f) == CLAP_PLUGIN_FEATURE_INSTRUMENT;
    CHECK (effect && filter && stereo && ! instrument);
    Instance a;
    const auto* ports = static_cast<const clap_plugin_audio_ports*> (a.plugin->get_extension (a.plugin, CLAP_EXT_AUDIO_PORTS));
    CHECK (ports != nullptr && ports->count (a.plugin, true) == 1 && ports->count (a.plugin, false) == 1);
    for (bool input : { true, false })
    {
        clap_audio_port_info info {};
        CHECK (ports->get (a.plugin, 0, input, &info) && info.channel_count == 2 && (info.flags & CLAP_AUDIO_PORT_IS_MAIN)
               && std::string (info.port_type) == CLAP_PORT_STEREO);
        CHECK (! ports->get (a.plugin, 1, input, &info));
    }
    CHECK (a.plugin->get_extension (a.plugin, CLAP_EXT_NOTE_PORTS) == nullptr);
    CHECK (factory->create_plugin (factory, &host, "com.example.other") == nullptr);
}

void testParameters()
{
    Instance a;
    CHECK (a.params->count (a.plugin) == cortex::paramCount);
    std::map<clap_id, int> seen;
    for (uint32_t i = 0; i < a.params->count (a.plugin); ++i)
    {
        clap_param_info info {};
        CHECK (a.params->get_info (a.plugin, i, &info));
        CHECK (seen[info.id]++ == 0 && info.id < cortex::ParamId::count);
        CHECK (info.min_value < info.max_value && info.default_value >= info.min_value && info.default_value <= info.max_value);
        double v = -1.0;
        static const cortex::Store factory;
        const double expect = cortex::isEditField (info.id) ? factory.field (1, (int) (info.id - cortex::ParamId::editCube)) : info.default_value;
        CHECK (a.params->get_value (a.plugin, info.id, &v) && v == expect); // the edit filter's fields show F01
        // Text there and back for a spread of values: the same text again, and a value within one text's step.
        for (int k = 0; k <= 8; ++k)
        {
            double x = info.min_value + (info.max_value - info.min_value) * k / 8.0;
            if (info.flags & CLAP_PARAM_IS_STEPPED)
                x = std::round (x);
            char text[64], again[64];
            CHECK (a.params->value_to_text (a.plugin, info.id, x, text, sizeof (text)));
            double back = 0.0;
            CHECK (a.params->text_to_value (a.plugin, info.id, text, &back));
            CHECK (a.params->value_to_text (a.plugin, info.id, back, again, sizeof (again)));
            if (std::string (text) != again)
                std::printf ("  %s: \"%s\" read back as \"%s\"\n", info.name, text, again);
            CHECK (std::string (text) == again);
        }
    }
    CHECK (! a.params->get_info (a.plugin, cortex::paramCount, nullptr));
    double v = 0.0;
    CHECK (! a.params->get_value (a.plugin, 999, &v));
    CHECK (a.params->text_to_value (a.plugin, a.id ("Cube"), "Bell Tower", &v) && v == 8.0); // the edit filter's: Null first
    CHECK (a.params->text_to_value (a.plugin, a.id ("Filter"), "F17 Deep Acid", &v) && v == 17.0);
    CHECK (a.params->text_to_value (a.plugin, a.id ("Filter"), "f5", &v) && v == 5.0);
    CHECK (! a.params->text_to_value (a.plugin, a.id ("Filter"), "five", &v));
    char text[64];
    CHECK (a.params->value_to_text (a.plugin, a.id ("Filter"), 8.0, text, sizeof (text)) && std::string (text) == "F08 Bell Tower");
    CHECK (a.params->value_to_text (a.plugin, a.id ("Filter"), 40.0, text, sizeof (text)) && std::string (text) == "F40 --empty--");
    CHECK (a.params->text_to_value (a.plugin, a.id ("Freq Gain"), "-1.5x", &v) && v == -15.0);
    CHECK (a.params->text_to_value (a.plugin, a.id ("Exit Fade"), "250 ms", &v) && std::fabs (cortex::fadeSeconds ((float) v) - 0.25f) < 1e-5f);
    CHECK (a.params->text_to_value (a.plugin, a.id ("LFO Sync"), "1/8T", &v) && v == 10.0);
    CHECK (a.params->text_to_value (a.plugin, a.id ("LFO Sync"), " 1/8 ", &v) && v == 9.0);
    CHECK (a.params->text_to_value (a.plugin, a.id ("LFO Sync"), "4 bars", &v) && v == 2.0);
    CHECK (a.params->text_to_value (a.plugin, a.id ("LFO Sync"), "3", &v) && v == 3.0);
    CHECK (! a.params->text_to_value (a.plugin, a.id ("LFO Sync"), "4 Bar", &v));
    CHECK (a.params->text_to_value (a.plugin, a.id ("Glide"), "100 ms", &v) && std::fabs (cortex::glideSeconds ((float) v) - 0.1f) < 1e-5f);
    CHECK (a.params->text_to_value (a.plugin, a.id ("LFO Rate"), "0", &v) && v == 0.0);
    CHECK (! a.params->text_to_value (a.plugin, a.id ("Mix"), "lots", &v));
    // A value out of range from the host is held to the range.
    a.setup ({ { "Mix", 7.0 }, { "Filter", -3.0 } }, 0.01);
    CHECK (a.value ("Mix") == 1.0 && a.value ("Filter") == 1.0);
}

struct Memory
{
    std::vector<uint8_t> bytes;
    size_t read = 0;
    clap_ostream out { this, [] (const clap_ostream* s, const void* data, uint64_t size) -> int64_t {
        auto& m = *static_cast<Memory*> (s->ctx);
        m.bytes.insert (m.bytes.end(), (const uint8_t*) data, (const uint8_t*) data + size);
        return (int64_t) size;
    } };
    clap_istream in { this, [] (const clap_istream* s, void* data, uint64_t size) -> int64_t {
        auto& m = *static_cast<Memory*> (s->ctx);
        const size_t n = std::min<size_t> (size, m.bytes.size() - m.read);
        std::memcpy (data, m.bytes.data() + m.read, n);
        m.read += n;
        return (int64_t) n;
    } };
};

void testState()
{
    Memory m;
    {
        Instance a;
        a.setup ({ { "Filter", 10 }, { "Morph", 0.33 }, { "LFO Sync", 5 }, { "Env > Freq", -0.4 }, { "Output", -6.5 } }, 0.01);
        CHECK (a.state->save (a.plugin, &m.out));
    }
    Instance b;
    CHECK (b.state->load (b.plugin, &m.in));
    CHECK (b.value ("Filter") == 10 && std::fabs (b.value ("Morph") - 0.33) < 1e-6 && b.value ("LFO Sync") == 5
           && std::fabs (b.value ("Env > Freq") + 0.4) < 1e-6 && std::fabs (b.value ("Output") + 6.5) < 1e-6);
    // Another plug-in's state, or a cut-off one, is refused; one with fewer values (an older version) keeps the
    // defaults for the rest; an unknown id is passed over.
    Memory bad;
    bad.bytes = { 'A', 'N', 'V', 'L', 1, 0, 0, 0, 0, 0, 0, 0 };
    CHECK (! b.state->load (b.plugin, &bad.in));
    Memory cut;
    cut.bytes.assign (m.bytes.begin(), m.bytes.begin() + 20);
    CHECK (! b.state->load (b.plugin, &cut.in));
    Memory older;
    const uint32_t header[2] = { 1, 2 };
    const struct { uint32_t id; float value; } pairs[2] = { { cortex::ParamId::spread, 0.5f }, { 4000u, 1.0f } };
    older.bytes.insert (older.bytes.end(), { 'C', 'R', 'T', 'X' });
    older.bytes.insert (older.bytes.end(), (const uint8_t*) header, (const uint8_t*) header + sizeof (header));
    older.bytes.insert (older.bytes.end(), (const uint8_t*) pairs, (const uint8_t*) pairs + sizeof (pairs));
    CHECK (b.state->load (b.plugin, &older.in));
    CHECK (b.value ("Spread") == 0.5 && b.value ("Filter") == 1 && b.value ("Morph") == 0.0);

    // 0.1.0's state: its Cube (id 0) is the factory's filter for that cube.
    {
        Memory v1;
        const uint32_t head[2] = { 1, 2 };
        const struct { uint32_t id; float value; } old[2] = { { 0u, 9.0f }, { cortex::ParamId::morph, 0.25f } };
        v1.bytes.insert (v1.bytes.end(), { 'C', 'R', 'T', 'X' });
        v1.bytes.insert (v1.bytes.end(), (const uint8_t*) head, (const uint8_t*) head + sizeof (head));
        v1.bytes.insert (v1.bytes.end(), (const uint8_t*) old, (const uint8_t*) old + sizeof (old));
        Instance c;
        CHECK (c.state->load (c.plugin, &v1.in));
        CHECK (c.value ("Filter") == 10 && c.value ("Morph") == 0.25 && c.value ("Cube") == 10); // F10 Acid Twins
    }
    // The bank and the sequences go with the state: an edited filter, a copied one, a sequence programmed; a state
    // cut off inside them is refused and changes nothing.
    {
        Memory bank;
        {
            Instance a;
            a.setup ({ { "Filter", 40 }, { "Cube", 13 }, { "Freq Offset", 25 }, { "Right Dist", 12 }, { "Filter", 9 }, { "Sequence", 12 } }, 0.01);
            const auto* screen = static_cast<const melodee_screen_t*> (a.plugin->get_extension (a.plugin, MELODEE_SCREEN_CLAP));
            auto in = [&] (uint32_t kind, int value) { melodee_screen_input_t e { kind, value, false }; return screen->input (a.plugin, &e); };
            // SEQ (two groups on), Sequence 12 empty: Add Step twice (the filter Filter picks, F09), then the first
            // step made a Halt: its choices are the 33 filters in use (F01-F32, F40), then Pause, Halt.
            CHECK (in (MELODEE_SCREEN_STEP, 1) && in (MELODEE_SCREEN_STEP, 1));
            CHECK (in (MELODEE_SCREEN_PUSH, 0) && in (MELODEE_SCREEN_PUSH, 0));
            CHECK (in (MELODEE_SCREEN_WHEEL, -5) && in (MELODEE_SCREEN_PUSH, 0)); // editing step 1, on F09 (choice 8)
            CHECK (in (MELODEE_SCREEN_WHEEL, 33 + 1 - 8) && in (MELODEE_SCREEN_PUSH, 0));
            CHECK (a.state->save (a.plugin, &bank.out));
        }
        // Sequence 12 as saved: two steps, Halt then F09.
        const auto seqs = std::search (bank.bytes.begin(), bank.bytes.end(), std::begin ("SEQS"), std::begin ("SEQS") + 4);
        CHECK (seqs != bank.bytes.end());
        if (seqs != bank.bytes.end())
        {
            const size_t at = (size_t) (seqs - bank.bytes.begin()) + 12 + 11 * (1 + cortex::maxSteps);
            CHECK (bank.bytes[at] == 2 && bank.bytes[at + 1] == cortex::stepHalt && bank.bytes[at + 2] == 9);
        }
        Instance b;
        CHECK (b.state->load (b.plugin, &bank.in));
        Memory again;
        CHECK (b.state->save (b.plugin, &again.out) && again.bytes == bank.bytes); // all of it, back as it was
        b.setup ({ { "Filter", 40 } }, 0.01);
        CHECK (b.value ("Cube") == 13 && b.value ("Freq Offset") == 25 && b.value ("Right Dist") == 12);
        char text[64];
        CHECK (b.params->value_to_text (b.plugin, b.id ("Filter"), 40.0, text, sizeof (text)) && std::string (text) == "F40 Metal Plate");
        Memory cut;
        cut.bytes.assign (bank.bytes.begin(), bank.bytes.end() - 40);
        Instance c;
        c.setup ({ { "Filter", 40 } }, 0.01);
        CHECK (! c.state->load (c.plugin, &cut.in));
        CHECK (c.value ("Cube") == 0 && c.params->value_to_text (c.plugin, c.id ("Filter"), 40.0, text, sizeof (text)) && std::string (text) == "F40 --empty--");
    }

    // A state loaded into a running plug-in (as Melodee opens a project: activate, then load) sounds at once as it was
    // saved, the same to the bit as one loaded before activation, not gliding in from the defaults.
    Memory saved;
    {
        Instance a;
        a.setup ({ { "Glide", 1.0 }, { "Morph", 1.0 }, { "Freq", 0.0 }, { "Transform", 1.0 }, { "Filter", 8 }, { "Output", -24.0 }, { "Mix", 0.5 },
                   { "Spread", 0.5 }, { "LFO > Freq", 0.3 } }, 0.01);
        CHECK (a.state->save (a.plugin, &saved.out));
    }
    const Stereo x = loop (48000.0, 1.0);
    Instance running;
    Memory first; // a stream of its own (a copied Memory's streams would still read the original)
    first.bytes = saved.bytes;
    CHECK (running.state->load (running.plugin, &first.in));
    const Stereo heard = running.run (x, 128);
    Instance before;
    before.plugin->stop_processing (before.plugin);
    before.plugin->deactivate (before.plugin);
    Memory second;
    second.bytes = saved.bytes;
    CHECK (before.state->load (before.plugin, &second.in));
    CHECK (before.plugin->activate (before.plugin, 48000.0, 1, 4096) && before.plugin->start_processing (before.plugin));
    const Stereo expected = before.run (x, 128);
    CHECK (heard.l == expected.l && heard.r == expected.r);
}

struct Listed { std::string name, key; bool ours = false; uint32_t flags = 0; };
std::vector<Listed> listPresets()
{
    std::vector<Listed> out;
    const auto* f = static_cast<const clap_preset_discovery_factory*> (entry->get_factory (CLAP_PRESET_DISCOVERY_FACTORY_ID));
    CHECK (f != nullptr && entry->get_factory (CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT) == (const void*) f);
    if (f == nullptr || f->count (f) != 1)
        return out;
    const auto* d = f->get_descriptor (f, 0);
    struct Index
    {
        std::vector<clap_preset_discovery_location> found;
        clap_preset_discovery_indexer indexer {};
    } index;
    index.indexer = { CLAP_VERSION, "tests", "", "", "1", &index,
                      [] (const clap_preset_discovery_indexer*, const clap_preset_discovery_filetype*) { return true; },
                      [] (const clap_preset_discovery_indexer* i, const clap_preset_discovery_location* l) {
                          static_cast<Index*> (i->indexer_data)->found.push_back (*l);
                          return true;
                      },
                      [] (const clap_preset_discovery_indexer*, const clap_preset_discovery_soundpack*) { return true; },
                      [] (const clap_preset_discovery_indexer*, const char*) -> const void* { return nullptr; } };
    const auto* provider = f->create (f, &index.indexer, d->id);
    CHECK (provider != nullptr && provider->init (provider));
    if (provider == nullptr)
        return out;
    CHECK (index.found.size() == 1 && index.found[0].kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN);
    clap_preset_discovery_metadata_receiver r {};
    r.receiver_data = &out;
    auto list = [] (const clap_preset_discovery_metadata_receiver* r) -> std::vector<Listed>& { return *static_cast<std::vector<Listed>*> (r->receiver_data); };
    static std::vector<Listed>& (*of) (const clap_preset_discovery_metadata_receiver*) = list;
    r.on_error = [] (const clap_preset_discovery_metadata_receiver*, int32_t, const char*) { CHECK (false); };
    r.begin_preset = [] (const clap_preset_discovery_metadata_receiver* r, const char* name, const char* key) {
        of (r).push_back ({ name ? name : "", key ? key : "" });
        return true;
    };
    r.add_plugin_id = [] (const clap_preset_discovery_metadata_receiver* r, const clap_universal_plugin_id* id) {
        of (r).back().ours = std::string (id->abi) == "clap" && std::string (id->id) == "com.hdslave.cortex-filter";
    };
    r.set_soundpack_id = [] (const clap_preset_discovery_metadata_receiver*, const char*) {};
    r.set_flags = [] (const clap_preset_discovery_metadata_receiver* r, uint32_t flags) { of (r).back().flags = flags; };
    r.add_creator = [] (const clap_preset_discovery_metadata_receiver*, const char*) {};
    r.set_description = [] (const clap_preset_discovery_metadata_receiver*, const char*) {};
    r.set_timestamps = [] (const clap_preset_discovery_metadata_receiver*, clap_timestamp, clap_timestamp) {};
    r.add_feature = [] (const clap_preset_discovery_metadata_receiver*, const char*) {};
    r.add_extra_info = [] (const clap_preset_discovery_metadata_receiver*, const char*, const char*) {};
    CHECK (provider->get_metadata (provider, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, &r));
    provider->destroy (provider);
    return out;
}

void testPresets()
{
    const auto listed = listPresets();
    CHECK (listed.size() == cortex::presetCount);
    Instance a;
    for (size_t i = 0; i < listed.size(); ++i)
    {
        CHECK (listed[i].ours && (listed[i].flags & CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT));
        CHECK (listed[i].key == cortex::presetTable[i].key && listed[i].name == cortex::presetTable[i].name);
        const int before = presetsLoaded;
        CHECK (a.loadPreset (listed[i].key.c_str()) && presetsLoaded == before + 1);
        float expect[cortex::paramCount];
        cortex::presetValuesOf (cortex::presetTable[i], expect);
        static const cortex::Store factory;
        const int slot = (int) expect[cortex::paramIndex (cortex::ParamId::filter)];
        for (int f = 0; f < cortex::fieldCount; ++f) // the edit filter's fields: the filter the preset picks
            expect[cortex::paramIndex (cortex::fieldParam (f))] = factory.field (slot, f);
        bool same = true;
        for (uint32_t p = 0; p < cortex::paramCount; ++p)
        {
            double v = 0.0;
            a.params->get_value (a.plugin, cortex::paramTable[p].id, &v);
            same &= (float) v == expect[p];
        }
        CHECK (same);
    }
    const int errors = presetErrors;
    CHECK (! a.loadPreset ("no-such-preset") && presetErrors == errors + 1);
}

// The sound's response against the screen's: the impulse's spectrum at 40 frequencies is the response morph.h works
// out for the same cube, point, Emphasis and rate, within 0.05 dB (where the response is above -50 dB).
void testResponse()
{
    struct Case { int cube; cortex::Position p; float emphasis; double rate; };
    const Case cases[] = {
        { 0, { 0.0f, 0.5556f, 0.0f }, 0.0f, 48000.0 }, { 0, { 0.7f, 0.3f, 0.8f }, 0.5f, 48000.0 }, { 1, { 0.2f, 0.6f, 0.5f }, 0.0f, 44100.0 },
        { 2, { 0.3f, 0.5f, 0.6f }, 0.0f, 48000.0 }, { 3, { 0.8f, 0.4f, 0.2f }, -0.5f, 96000.0 }, { 4, { 0.5f, 0.5f, 0.5f }, 0.0f, 48000.0 },
        { 5, { 0.4f, 0.5f, 0.3f }, 0.0f, 48000.0 }, { 6, { 0.6f, 0.4f, 0.7f }, 0.3f, 44100.0 }, { 7, { 0.5f, 0.7f, 0.2f }, 0.0f, 48000.0 },
        { 8, { 0.9f, 0.5f, 0.6f }, 0.0f, 48000.0 }, { 9, { 0.3f, 0.45f, 0.9f }, 0.2f, 48000.0 }, { 10, { 0.5f, 0.5f, 0.5f }, 0.0f, 96000.0 },
        { 11, { 0.35f, 0.6f, 0.4f }, 0.0f, 48000.0 }, { 12, { 0.5f, 0.6f, 0.3f }, 0.0f, 48000.0 }, { 13, { 0.7f, 0.5f, 0.5f }, 0.0f, 48000.0 },
        { 14, { 0.4f, 0.5f, 0.8f }, 0.0f, 48000.0 }, { 15, { 0.5f, 0.6f, 0.4f }, 0.0f, 48000.0 },
    };
    for (const Case& c : cases)
    {
        Instance a (c.rate);
        const auto h = impulseResponse (a, { { "Filter", c.cube + 1 }, { "Morph", c.p.x }, { "Freq", c.p.y }, { "Transform", c.p.z }, { "Emphasis", c.emphasis },
                                             { "Glide", 0.0 } },
                                        (size_t) (c.rate * 5.5));
        cortex::Design d;
        cortex::design (cortex::cubeTable (c.cube), c.p, c.emphasis, (float) c.rate, d);
        double worst = 0.0;
        for (int i = 0; i < 40; ++i)
        {
            const double hz = 25.0 * std::pow (16000.0 / 25.0, i / 39.0);
            const double expect = cortex::responseDb (d, (float) hz, (float) c.rate);
            if (expect < -50.0)
                continue;
            worst = std::max (worst, std::fabs (spectrumDb (h, hz, c.rate) - expect));
        }
        if (worst > 0.05)
            std::printf ("  cube %d at (%g, %g, %g), %g Hz: %g dB off the screen's response\n", c.cube, c.p.x, c.p.y, c.p.z, c.rate, worst);
        CHECK (worst <= 0.05);
    }
}

// Known filters, measured with no help from morph.h: Low To Band at its corner with Freq for 1 kHz is a 2-pole
// Butterworth low pass, High To Band the high pass (the textbook responses through the bilinear transform, prewarped
// to 1 kHz); Vowels A E O U at A peaks at the vowel's first two formants.
double butterworthDb (double hz, double cutoff, double rate, bool high)
{
    const double w = std::tan (M_PI * hz / rate) / std::tan (M_PI * cutoff / rate), w2 = w * w;
    const double denominator = (1.0 - w2) * (1.0 - w2) + 2.0 * w2;
    return 10.0 * std::log10 ((high ? w2 * w2 : 1.0) / denominator);
}

void testKnownFilters()
{
    const double y1k = 5.0 / 9.0; // Freq for a ratio of 1 between 1/32 and 16
    for (int cube : { 0, 1 })
    {
        Instance a;
        const auto h = impulseResponse (a, { { "Filter", cube + 1 }, { "Morph", 0 }, { "Freq", y1k }, { "Transform", 0 }, { "Glide", 0.0 } }, 48000);
        NEAR (spectrumDb (h, 1000.0, 48000.0), -3.01, 0.02);
        for (double hz : { 30.0, 100.0, 300.0, 2000.0, 5000.0, 10000.0, 15000.0 })
            NEAR (spectrumDb (h, hz, 48000.0), butterworthDb (hz, 1000.0, 48000.0, cube == 1), 0.02);
    }
    {
        Instance a;
        const auto h = impulseResponse (a, { { "Filter", 3 }, { "Morph", 0 }, { "Freq", 0.5 }, { "Transform", 0 }, { "Glide", 0.0 } }, 48000);
        auto peakNear = [&] (double hz) {
            double best = -1e9, at = 0.0;
            for (double f = hz * 0.8; f <= hz * 1.2; f *= 1.005)
            {
                const double v = spectrumDb (h, f, 48000.0);
                if (v > best)
                    best = v, at = f;
            }
            return at;
        };
        NEAR (peakNear (650.0), 650.0, 650.0 * 0.06);
        NEAR (peakNear (1080.0), 1080.0, 1080.0 * 0.06);
        CHECK (spectrumDb (h, 650.0, 48000.0) > spectrumDb (h, 850.0, 48000.0) + 6.0); // a peak, not a slope
    }
}

// The same input and changes give the same output, to the bit, in any block size.
void testBlockSizes()
{
    const Stereo x = loop (48000.0, 1.75);
    std::vector<Change> changes;
    {
        Instance probe;
        changes = { { 0, probe.id ("Filter"), 7 }, { 0, probe.id ("LFO > Morph"), 0.8 }, { 0, probe.id ("LFO Sync"), 7 }, { 0, probe.id ("Env > Freq"), 0.6 },
                    { 0, probe.id ("Spread"), 0.4 }, { 0, probe.id ("Drive"), 6 }, { 5003, probe.id ("Filter"), 12 }, { 9001, probe.id ("Mix"), 0.5 },
                    { 20000, probe.id ("Freq"), 0.2 }, { 33333, probe.id ("Emphasis"), 0.7 }, { 47999, probe.id ("Drive"), 0 },
                    // The sequencer: Acid Steps on sixteenths with a bar's Reset, the envelope on Dec, a gate, Deep Acid
                    // distorted harder and faded slower, a Fade step put in.
                    { 50000, probe.id ("Filter"), 17 }, { 50000, probe.id ("Left Dist"), 24 }, { 50000, probe.id ("Exit Fade"), 0.3 },
                    { 50001, probe.id ("Sequence"), 5 }, { 50001, probe.id ("Inc Clock"), 11 }, { 50001, probe.id ("Reset Clock"), 1 },
                    { 51111, probe.id ("Env Trigger"), 2 }, { 51111, probe.id ("Trig Level"), -24 }, { 60000, probe.id ("Inc"), 1 },
                    { 60100, probe.id ("Inc"), 0 }, { 61000, probe.id ("Xform > Dist"), 1 } };
    }
    Stereo reference;
    for (uint32_t block : { 128u, 1u, 3u, 8u, 64u, 333u, 1024u, 4096u })
    {
        Instance a (48000.0, 4096);
        const Stereo o = a.run (x, block, changes);
        if (block == 128)
            reference = o;
        else
        {
            const bool same = std::memcmp (o.l.data(), reference.l.data(), o.l.size() * 4) == 0 && std::memcmp (o.r.data(), reference.r.data(), o.r.size() * 4) == 0;
            if (! same)
                std::printf ("  block %u differs\n", block);
            CHECK (same);
        }
    }
}

// Mix 0 is the input to the bit; a mono input is heard on both sides; no input at all is silence; the input and
// output may be the same buffers.
void testRouting()
{
    const Stereo x = loop (48000.0, 1.0);
    {
        Instance a;
        a.setup ({ { "Mix", 0.0 }, { "Filter", 10 }, { "Drive", 12 } }, 1.0);
        const Stereo o = a.run (x, 128);
        CHECK (o.l == x.l && o.r == x.r);
    }
    {
        Instance a;
        a.setup ({ { "Filter", 3 }, { "Morph", 0.3 } });
        const Stereo o = a.run (x, 128, {}, 1);
        CHECK (o.l == o.r);
        CHECK (std::any_of (o.l.begin(), o.l.end(), [] (float v) { return std::fabs (v) > 1e-3f; }));
    }
    {
        Instance a;
        const Stereo o = a.run (x, 128, {}, 0);
        CHECK (std::all_of (o.l.begin(), o.l.end(), [] (float v) { return v == 0.0f; }) && std::all_of (o.r.begin(), o.r.end(), [] (float v) { return v == 0.0f; }));
    }
    {
        Instance a, b;
        a.setup ({ { "Filter", 12 }, { "Spread", 0.7 }, { "LFO > Freq", 0.5 } });
        b.setup ({ { "Filter", 12 }, { "Spread", 0.7 }, { "LFO > Freq", 0.5 } });
        const Stereo apart = a.run (x, 128);
        Stereo buffer = x;
        for (size_t at = 0; at < buffer.l.size(); at += 128) // the same buffers in and out
        {
            const uint32_t n = (uint32_t) std::min<size_t> (128, buffer.l.size() - at);
            float* chans[2] = { buffer.l.data() + at, buffer.r.data() + at };
            clap_audio_buffer io {};
            io.data32 = chans, io.channel_count = 2;
            clap_event_transport tr {};
            tr.header = { sizeof (tr), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0 };
            tr.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_IS_PLAYING;
            tr.tempo = 120.0;
            tr.song_pos_beats = (clap_beattime) std::llround ((double) (b.played + at) * 2.0 / 48000.0 * (double) CLAP_BEATTIME_FACTOR);
            Events none;
            OutEvents ignored;
            clap_process p {};
            p.frames_count = n, p.transport = &tr, p.audio_inputs = &io, p.audio_inputs_count = 1, p.audio_outputs = &io, p.audio_outputs_count = 1;
            p.in_events = &none.in, p.out_events = &ignored.out;
            CHECK (b.plugin->process (b.plugin, &p) == CLAP_PROCESS_CONTINUE);
        }
        CHECK (buffer.l == apart.l && buffer.r == apart.r);
        CHECK (apart.l != apart.r); // Spread: two filters
    }
}

// Abuse: random settings changed every 64 frames, loud noise, full Drive, Emphasis and Output, the fastest LFO at
// full depth: the output stays finite and under 2.
void testExtremes()
{
    std::mt19937 random (11);
    for (int run = 0; run < 6; ++run)
    {
        Instance a;
        std::vector<Change> changes;
        const Stereo x = whiteNoise (48000 * 2, run < 3 ? 1.0f : 4.0f, (uint32_t) run);
        for (uint64_t at = 0; at < x.l.size(); at += 64)
            for (int k = 0; k < 3; ++k)
            {
                const auto& p = cortex::paramTable[random() % cortex::paramCount];
                std::uniform_real_distribution<double> any (p.min, p.max);
                changes.push_back ({ at, p.id, run % 2 == 0 ? any (random) : (random() % 2 ? p.max : p.min) });
            }
        changes.push_back ({ 0, a.id ("LFO Rate"), 1.0 });
        changes.push_back ({ 0, a.id ("Emphasis"), 1.0 });
        const Stereo o = a.run (x, 128, changes);
        bool ok = true;
        for (size_t i = 0; i < o.l.size(); ++i)
            ok &= std::isfinite (o.l[i]) && std::isfinite (o.r[i]) && std::fabs (o.l[i]) <= 2.0f && std::fabs (o.r[i]) <= 2.0f;
        CHECK (ok);
        // Then silence: it rings down to nothing.
        const Stereo quiet = a.run (Stereo { std::vector<float> (48000 * 25, 0.0f), std::vector<float> (48000 * 25, 0.0f) }, 128);
        CHECK (std::fabs (quiet.l.back()) < 1e-5f && std::fabs (quiet.r.back()) < 1e-5f);
    }
}

// The largest difference between two renders.
float maxDifference (const Stereo& a, const Stereo& b, size_t from = 0)
{
    float m = 0.0f;
    for (size_t i = from; i < a.l.size() && i < b.l.size(); ++i)
        m = std::max ({ m, std::fabs (a.l[i] - b.l[i]), std::fabs (a.r[i] - b.r[i]) });
    return m;
}

Stereo slice (const Stereo& x, size_t from, size_t frames)
{
    Stereo s;
    s.l.assign (x.l.begin() + (long) from, x.l.begin() + (long) (from + frames));
    s.r.assign (x.r.begin() + (long) from, x.r.begin() + (long) (from + frames));
    return s;
}

// The sequencer through a gap in the process calls while the song plays on, as Melodee's idle gate makes one (it lets
// an effect sleep after half a second of silence): Vowel Walk on a phrase with a bar's rest across its Reset sounds
// after the rest as it does with the rest played through. A loop one Reset long (the song back at the bar's start
// each time round) starts the sequence again each time: every pass sounds as the one before.
void testSequencerGaps()
{
    const Stereo phrase = loop (48000.0, 9.0);
    {
        Instance through, gapped;
        for (Instance* a : { &through, &gapped })
        {
            CHECK (a->loadPreset ("vowel-walk"));
            a->run (slice (phrase, 0, 144000), 128); // beats 0-6
        }
        through.run (Stereo { std::vector<float> (96000, 0.0f), std::vector<float> (96000, 0.0f) }, 128); // the rest, beats 6-10
        gapped.played += 96000; // the rest asleep: no process calls
        const Stereo a = through.run (slice (phrase, 240000, 192000), 128), b = gapped.run (slice (phrase, 240000, 192000), 128);
        const float apart = maxDifference (a, b, 48000); // from beat 11, past the first change after the rest
        if (apart >= 1e-3f)
            std::printf ("  after a rest asleep: %g from the rest played through\n", (double) apart);
        CHECK (apart < 1e-3f);
    }
    {
        Instance a;
        a.setup ({ { "Sequence", 1 }, { "Inc Clock", 7 }, { "Reset Clock", 1 } }, 0.3);
        const Stereo bar = slice (phrase, 0, 96000);
        std::vector<Stereo> passes;
        for (int pass = 0; pass < 4; ++pass)
        {
            a.played = 0;
            passes.push_back (a.run (bar, 128));
        }
        for (int pass = 2; pass < 4; ++pass) // (the first came in from the silence before it, with no change of filter)
        {
            const float apart = maxDifference (passes[pass], passes[pass - 1]);
            if (apart >= 1e-3f)
                std::printf ("  a loop one Reset long: pass %d is %g from the one before\n", pass + 1, (double) apart);
            CHECK (apart < 1e-3f);
        }
    }
}

// hdslaveOS's idle gate for an effect (src/melodee/plugin/idle_gate.h, as plugin_rack.cpp and clap_instance.cpp run
// it): before each block the plug-in is skipped once its input and output have been under -100 dB for half a second,
// or for the tail it reports when that is longer, at most 10 s (this one's 11 s tail: 10 s), unless the block's input
// is louder or the plug-in asked to be run (request_process: the host clears the wish as a block starts, so one asked
// for during a block runs the next). Skipped, it is not called at all: it hears no transport and gives silence. The
// song moves on regardless. What woke it counts it quiet again from that block. hdslaveOS from 0.8.3-hdslave.4, as the
// model has it unless told otherwise:
// - `transportWakes`: a sleeping plug-in is also run for the one block in which the transport starts, stops or jumps
//   (a locate, the loop going round), its place off by more than a 64th note from where the block before led;
// - `countsSleep`: steady_time counts the frames it slept, and those of `bypassed` blocks (a bypassed slot, a frozen
//   track: not run at all, nor woken, their input passed by); in every build before, it stood still;
// - the song's loop, on Song with its loop braced, told while it plays (CLAP_TRANSPORT_IS_LOOP_ACTIVE).
struct GatedHost
{
    clap_host host;
    bool asked = false, transportWakes = true, countsSleep = true;
    std::function<bool (uint64_t)> bypassed; // the blocks (by their first frame) the plug-in is bypassed for
    GatedHost()
        : host { CLAP_VERSION, this, "Cortex Filter gated test host", "tests", "", "1", hostExtension, [] (const clap_host*) {},
                 [] (const clap_host* h) { static_cast<GatedHost*> (h->host_data)->asked = true; }, [] (const clap_host*) {} }
    {
    }
};
struct SongAt
{
    bool playing;
    double beats, bpm = 120.0;
    bool looping = false; // the loop told, from loopStart to loopEnd (beats)
    double loopStart = 0.0, loopEnd = 0.0;
};
// `in` through `a` in 128-frame blocks, the song as `song` tells it at each block's first frame; with `gate` (the
// host `a` was made with), through the idle gate, `skipped` counting the blocks it slept.
Stereo hosted (Instance& a, const Stereo& in, const std::function<SongAt (uint64_t)>& song, GatedHost* gate, int* skipped = nullptr)
{
    const size_t frames = in.l.size();
    Stereo o { std::vector<float> (frames, 0.0f), std::vector<float> (frames, 0.0f) };
    const auto* tail = static_cast<const clap_plugin_tail*> (a.plugin->get_extension (a.plugin, CLAP_EXT_TAIL));
    const double span = std::max (0.5, std::min (tail != nullptr ? (double) tail->get (a.plugin) / a.rate : 0.0, 10.0)); // IdleGate::set_tail
    const uint32_t needed = (uint32_t) std::lround (span * a.rate);
    uint32_t quiet = 0;
    uint64_t steady = 0;
    auto loud = [] (const float* l, const float* r, size_t n) {
        for (size_t i = 0; i < n; ++i)
            if (std::fabs (l[i]) > 1e-5f || std::fabs (r[i]) > 1e-5f)
                return true;
        return false;
    };
    OutEvents ignored;
    SongAt before { false, 0.0 };
    for (size_t at = 0; at < frames; at += 128)
    {
        const uint32_t n = (uint32_t) std::min<size_t> (128, frames - at);
        float* ins[2] = { const_cast<float*> (in.l.data()) + at, const_cast<float*> (in.r.data()) + at };
        float* outs[2] = { o.l.data() + at, o.r.data() + at };
        const SongAt now = song (at);
        // Started, stopped, or more than a 64th note from where the last block's frames took the song.
        const double expected = before.playing ? before.beats + 128.0 * before.bpm / 60.0 / a.rate : before.beats;
        const bool changed = at > 0 && (now.playing != before.playing || (now.playing && std::fabs (now.beats - expected) > 1.0 / 16.0));
        before = now;
        if (gate != nullptr && gate->bypassed && gate->bypassed (at))
        {
            std::copy (ins[0], ins[0] + n, outs[0]), std::copy (ins[1], ins[1] + n, outs[1]);
            if (gate->countsSleep)
                steady += n;
            continue;
        }
        if (gate != nullptr)
        {
            const bool waking = gate->asked || loud (ins[0], ins[1], n) || (changed && gate->transportWakes);
            if (waking)
                quiet = 0;
            if (! waking && quiet >= needed)
            {
                if (skipped != nullptr)
                    ++*skipped;
                if (gate->countsSleep)
                    steady += n;
                continue;
            }
            gate->asked = false;
        }
        clap_event_transport tr {};
        tr.header = { sizeof (tr), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0 };
        tr.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | (now.playing ? CLAP_TRANSPORT_IS_PLAYING : 0);
        tr.tempo = now.bpm;
        tr.song_pos_beats = (clap_beattime) std::llround (now.beats * (double) CLAP_BEATTIME_FACTOR);
        if (now.playing && now.looping)
        {
            tr.flags |= CLAP_TRANSPORT_IS_LOOP_ACTIVE;
            tr.loop_start_beats = (clap_beattime) std::llround (now.loopStart * (double) CLAP_BEATTIME_FACTOR);
            tr.loop_end_beats = (clap_beattime) std::llround (now.loopEnd * (double) CLAP_BEATTIME_FACTOR);
        }
        Events e;
        clap_audio_buffer input {}, output {};
        input.data32 = ins, input.channel_count = 2;
        output.data32 = outs, output.channel_count = 2;
        clap_process p {};
        p.steady_time = (int64_t) steady;
        p.frames_count = n;
        p.transport = &tr;
        p.audio_inputs = &input;
        p.audio_inputs_count = 1;
        p.audio_outputs = &output;
        p.audio_outputs_count = 1;
        p.in_events = &e.in;
        p.out_events = &ignored.out;
        CHECK (a.plugin->process (a.plugin, &p) != CLAP_PROCESS_ERROR);
        steady += n;
        if (gate != nullptr)
            quiet = loud (outs[0], outs[1], n) ? 0 : std::min<uint32_t> (quiet + n, needed);
    }
    return o;
}

// Short noise bursts (30 ms, then exact silence) every `every` frames from the first.
Stereo bursts (size_t frames, size_t every, float level)
{
    Stereo s = whiteNoise (frames, level, 7);
    for (size_t i = 0; i < frames; ++i)
        if (i % every >= 1440)
            s.l[i] = s.r[i] = 0.0f;
    return s;
}

// The effect as hdslaveOS runs it, sleeping through silence, against the same effect run every block. It sleeps once
// it has been quiet for the gate's 10 s, so the rests here are 14 s long (2 s to 16 s), asleep from about 12 s, and
// what happens in them happens from 13.5 s.
// - A sequence lets it sleep through a rest: woken by the sound coming back, or for a block by a transport change, it
//   moves on by the frames it slept (steady_time) to where the song had got to, counting the clocks the song passed,
//   then hears the block's own transport; so a second after the sound is back it is where it would have been awake
//   (within 1e-3, as testSequencerGaps: the fades between filters differ until then). In a rest: a Stop and a Play
//   from the top, a Play, the tempo changed, a locate forward, a loop going round onto its Reset; and with Reset Clock
//   Off (nothing lining the sequence up again), the song playing on, the loop going round, a locate back and a Stop,
//   and the loop going round or a Stop after the tempo changed (the loop's ends and the place a Stop holds telling
//   where the song had got to).
// - Bypassed (no block for the transport, steady_time counting on; the bypass begun while it sounds, so not a sleep):
//   the song playing on, and a Stop in the bypass with the bypass ended stopped, land; a locate back in it is not
//   counted (nothing bears it out), and the sequence is behind, as 0.3.3 would be.
// - The host as it was (no block for the transport, steady_time standing still asleep): a Stop and a Play in a rest
//   are missed and the sequence is off. With steady_time standing still but the block for the transport, the sound
//   is 0.3.3's: with Reset Clock Off, a loop going round, a locate back or a Stop in a rest leave the sequence behind.
// - The one limit left in a sleep, held here so a change to it shows: the tempo changed and then a locate back, in one
//   rest: the clocks are counted at the tempo it fell asleep at (nothing tells when the tempo changed).
// - The LFO runs on through a rest: free, and synced with the song stopped (where it runs on at the tempo).
// - The envelope follower keeps it awake while it falls and something reads it: Env Trigger re-arms through rests
//   (each of four hits steps the sequence, not only the first) and Env > Morph starts each hit from where the follower
//   fell to, to the bit; once the follower is under -100 dB it sleeps again.
// - With neither in use (a plain filter) it sleeps as before.
void testIdleGate()
{
    const double rate = 48000.0;
    const size_t second = 48000;
    auto silenceFrom = [] (Stereo x, size_t from, size_t to) {
        for (size_t i = from; i < to && i < x.l.size(); ++i)
            x.l[i] = x.r[i] = 0.0f;
        return x;
    };
    // How far the gated render is from the one run every block, from frame `from` (reported when `what` names it and
    // they differ by more than `allowed`, or always with CORTEX_DEBUG set).
    auto compare = [&] (const char* what, const std::function<void (Instance&)>& patch, const Stereo& in,
                        const std::function<SongAt (uint64_t)>& song, int& skipped, size_t from = 0, float allowed = 0.0f,
                        bool transportWakes = true, bool countsSleep = true, const std::function<bool (uint64_t)>& bypassed = {}) {
        Instance through;
        GatedHost gate;
        gate.transportWakes = transportWakes, gate.countsSleep = countsSleep, gate.bypassed = bypassed;
        Instance gated (rate, 4096, &gate.host);
        patch (through), patch (gated);
        const Stereo a = hosted (through, in, song, nullptr), b = hosted (gated, in, song, &gate, &skipped);
        const float apart = maxDifference (a, b, from);
        if ((apart > allowed || std::getenv ("CORTEX_DEBUG") != nullptr) && what != nullptr)
            std::printf ("  %s: %g apart from it run every block (%d blocks asleep)\n", what, (double) apart, skipped);
        return apart;
    };
    auto beatsFrom = [&] (double beats, size_t since, uint64_t at, double bpm = 120.0) { return beats + (double) (at - since) * bpm / 60.0 / rate; };
    auto s = [&] (double seconds) { return (uint64_t) (seconds * (double) second); };

    // Vowel Walk (a sequence on 1/8 clocks, reset every 2 bars) on a phrase with a rest from 2 s to 16 s, compared from
    // 17 s.
    const Stereo phrase = silenceFrom (loop (rate, 20.0), 2 * second, 16 * second);
    const size_t after = 17 * second;
    auto vowelWalk = [] (Instance& a) { CHECK (a.loadPreset ("vowel-walk")); };
    auto stopPlay = [&] (uint64_t at) -> SongAt {
        if (at < s (14.0)) return { true, beatsFrom (0.0, 0, at) }; // from the top: beat 28 at 14 s
        if (at < s (15.0)) return { false, 28.0 };                   // stopped a second
        return { true, beatsFrom (0.0, s (15.0), at) };             // Play from the top again
    };
    {
        int skipped = 0;
        CHECK (compare ("a Stop and a Play from the top in a rest", vowelWalk, phrase, stopPlay, skipped, after, 1e-3f) < 1e-3f);
        CHECK (skipped > 0);
        skipped = 0; // the host as it was: no block for the transport, steady_time standing still asleep
        CHECK (compare ("the same without the host's wake", vowelWalk, phrase, stopPlay, skipped, after, 1.0f, false, false) > 1e-2f);
    }
    {
        int skipped = 0;
        const Stereo in = silenceFrom (loop (rate, 19.0), second, 15 * second);
        auto song = [&] (uint64_t at) -> SongAt {
            if (at < s (13.0)) return { false, 0.0 };       // stopped at the top, the phrase heard
            return { true, beatsFrom (0.0, s (13.0), at) }; // Play at 13 s, in the rest
        };
        CHECK (compare ("a Play in a rest", vowelWalk, in, song, skipped, 16 * second, 1e-3f) < 1e-3f);
        CHECK (skipped > 0);
    }
    {
        int skipped = 0;
        auto song = [&] (uint64_t at) -> SongAt {
            if (at < s (13.5)) return { true, beatsFrom (0.0, 0, at) };
            return { true, beatsFrom (27.0, s (13.5), at, 90.0), 90.0 }; // 90 bpm from 13.5 s (beat 27), no jump
        };
        CHECK (compare ("the tempo changed in a rest", vowelWalk, phrase, song, skipped, after, 1e-3f) < 1e-3f);
        CHECK (skipped > 0);
    }
    {
        int skipped = 0;
        auto song = [&] (uint64_t at) -> SongAt {
            if (at < s (14.5)) return { true, beatsFrom (0.0, 0, at) };
            return { true, beatsFrom (40.0, s (14.5), at) }; // a locate to beat 40 at 14.5 s
        };
        CHECK (compare ("a locate forward in a rest", vowelWalk, phrase, song, skipped, after, 1e-3f) < 1e-3f);
        CHECK (skipped > 0);
    }
    // A loop of the song's first 30 beats, round at 15 s (onto beat 0: a Reset).
    auto loopRound = [&] (uint64_t at) -> SongAt { return { true, std::fmod (beatsFrom (0.0, 0, at), 30.0), 120.0, true, 0.0, 30.0 }; };
    {
        int skipped = 0;
        CHECK (compare ("a loop round onto its Reset in a rest", vowelWalk, phrase, loopRound, skipped, after, 1e-3f) < 1e-3f);
        CHECK (skipped > 0);
    }
    // Reset Clock Off, so only the Inc clocks place the sequence: the song playing on through the rest (patterns: the
    // place counts up), the loop going round at 15 s, a locate back to beat 1 at 14.5 s, and a Stop at 14.5 s (the sound
    // coming back stopped). Then the tempo to 90 bpm at 13.5 s (beat 27), and after it the loop going round (at 15.5 s)
    // or a Stop (at 15 s, beat 29.25); and with no loop told, the same turn is a locate back after a tempo change.
    auto noReset = [&] (Instance& a) { vowelWalk (a), a.setup ({ { "Reset Clock", 0 } }, 0.01); };
    auto playOn = [&] (uint64_t at) -> SongAt { return { true, beatsFrom (0.0, 0, at) }; };
    {
        auto locateBack = [&] (uint64_t at) -> SongAt {
            if (at < s (14.5)) return { true, beatsFrom (0.0, 0, at) };
            return { true, beatsFrom (1.0, s (14.5), at) };
        };
        auto stopIn = [&] (uint64_t at) -> SongAt {
            if (at < s (14.5)) return { true, beatsFrom (0.0, 0, at) };
            return { false, 29.0 };
        };
        auto slower = [&] (uint64_t at, bool told) -> SongAt {
            if (at < s (13.5)) return { true, beatsFrom (0.0, 0, at), 120.0, told, 0.0, 30.0 };
            return { true, std::fmod (beatsFrom (27.0, s (13.5), at, 90.0), 30.0), 90.0, told, 0.0, 30.0 }; // round at 15.5 s
        };
        auto slowerStop = [&] (uint64_t at) -> SongAt {
            if (at < s (13.5)) return { true, beatsFrom (0.0, 0, at) };
            if (at < s (15.0)) return { true, beatsFrom (27.0, s (13.5), at, 90.0), 90.0 };
            return { false, 29.25, 90.0 };
        };
        int skipped = 0;
        CHECK (compare ("the song playing on in a rest, Reset Clock Off", noReset, phrase, playOn, skipped, after, 1e-3f) < 1e-3f);
        CHECK (skipped > 0);
        CHECK (compare ("a loop round in a rest, Reset Clock Off", noReset, phrase, loopRound, skipped, after, 1e-3f) < 1e-3f);
        CHECK (compare ("a locate back in a rest, Reset Clock Off", noReset, phrase, locateBack, skipped, after, 1e-3f) < 1e-3f);
        CHECK (compare ("a Stop in a rest, Reset Clock Off", noReset, phrase, stopIn, skipped, after, 1e-3f) < 1e-3f);
        CHECK (compare ("the tempo changed, then the loop round, Reset Clock Off", noReset, phrase,
                        [&] (uint64_t at) { return slower (at, true); }, skipped, after, 1e-3f) < 1e-3f);
        CHECK (compare ("the tempo changed, then a Stop, Reset Clock Off", noReset, phrase, slowerStop, skipped, after, 1e-3f) < 1e-3f);
        // The limit left: the same turn with no loop told is a locate back after a tempo change.
        CHECK (compare ("the tempo changed, then a locate back (the limit)", noReset, phrase,
                        [&] (uint64_t at) { return slower (at, false); }, skipped, after, 1.0f) > 1e-2f);
        // A host whose steady_time stands still asleep (but runs the block for the transport): 0.3.3's sound, the
        // sequence behind after a loop round, a locate back or a Stop, as it was; the song playing on still lands.
        const bool still = false;
        CHECK (compare ("the song playing on, steady_time still", noReset, phrase, playOn, skipped, after, 1e-3f, true, still) < 1e-3f);
        CHECK (compare ("a loop round, steady_time still (0.3.3)", noReset, phrase, loopRound, skipped, after, 1.0f, true, still) > 1e-2f);
        CHECK (compare ("a locate back, steady_time still (0.3.3)", noReset, phrase, locateBack, skipped, after, 1.0f, true, still) > 1e-2f);
        CHECK (compare ("a Stop, steady_time still (0.3.3)", noReset, phrase, stopIn, skipped, after, 1.0f, true, still) > 1e-2f);
    }
    // Bypassed from 1 s to 8 s, begun while the phrase sounds: the song playing on; a Stop at 3 s (beat 6), the bypass
    // ended stopped; a locate back to beat 1 at 3 s (not counted: the limit, held so a change shows).
    {
        auto bypass = [&] (uint64_t at) { return at >= s (1.0) && at < s (8.0); };
        auto stopAt3 = [&] (uint64_t at) -> SongAt { return at < s (3.0) ? SongAt { true, beatsFrom (0.0, 0, at) } : SongAt { false, 6.0 }; };
        auto backAt3 = [&] (uint64_t at) -> SongAt {
            return at < s (3.0) ? SongAt { true, beatsFrom (0.0, 0, at) } : SongAt { true, beatsFrom (1.0, s (3.0), at) };
        };
        int skipped = 0;
        CHECK (compare ("bypassed, the song playing on", noReset, phrase, playOn, skipped, after, 1e-3f, true, true, bypass) < 1e-3f);
        CHECK (compare ("bypassed, a Stop in it", noReset, phrase, stopAt3, skipped, after, 1e-3f, true, true, bypass) < 1e-3f);
        CHECK (compare ("bypassed, a locate back in it (the limit)", noReset, phrase, backAt3, skipped, after, 1.0f, true, true, bypass) > 1e-2f);
    }
    // The LFO through the same rest, the song stopped: free (0.63 Hz) on Freq, and synced to 1/4T, which runs on at the
    // tempo while the song is stopped.
    {
        auto stoppedAt = [] (uint64_t) { return SongAt { false, 0.0 }; };
        auto freeLfo = [] (Instance& a) { a.setup ({ { "Filter", 2 }, { "LFO Rate", 0.5 }, { "LFO > Freq", 1.0 } }, 0.01); };
        auto synced = [] (Instance& a) { a.setup ({ { "Filter", 2 }, { "LFO Sync", 8 }, { "LFO > Freq", 1.0 } }, 0.01); };
        int skipped = 0;
        CHECK (compare ("a free LFO through a rest", freeLfo, phrase, stoppedAt, skipped, after, 1e-3f) < 1e-3f);
        CHECK (skipped > 0);
        skipped = 0;
        CHECK (compare ("a synced LFO through a rest, stopped", synced, phrase, stoppedAt, skipped, after, 1e-3f) < 1e-3f);
        CHECK (skipped > 0);
    }
    // Four hits 16 s apart, the song stopped: Env Trigger stepping the Lowpasses with the longest release (it falls
    // 6 dB under Trig Level about 4.6 s after a hit, and under -100 dB about 22 s after), and Env > Morph alone, no
    // sequence: the gate's 10 s pass in each rest with the follower still falling.
    const Stereo hits = bursts (64 * second, 16 * second, 0.5f);
    auto stopped = [] (uint64_t) { return SongAt { false, 0.0 }; };
    {
        int skipped = 0;
        auto trig = [] (Instance& a) {
            a.setup ({ { "Sequence", 1 }, { "Inc Clock", 0 }, { "Env Trigger", 1 }, { "Trig Level", -20.0 }, { "Env Attack", 0.1 },
                       { "Env Release", 1.0 } }, 0.01);
        };
        CHECK (compare ("Env Trigger through rests", trig, hits, stopped, skipped) == 0.0f && skipped == 0);
    }
    {
        int skipped = 0;
        auto morph = [] (Instance& a) {
            a.setup ({ { "Filter", 2 }, { "Env > Morph", 1.0 }, { "Env Attack", 0.1 }, { "Env Release", 1.0 } }, 0.01);
        };
        CHECK (compare ("Env > Morph through rests", morph, hits, stopped, skipped) == 0.0f && skipped == 0);
        // Then 40 s of silence: the follower falls under -100 dB (about 22 s at this release), and 10 s later it sleeps.
        Stereo tail = hits;
        tail.l.resize (104 * second, 0.0f), tail.r.resize (104 * second, 0.0f);
        skipped = 0;
        compare (nullptr, morph, tail, stopped, skipped);
        CHECK (skipped > 0);
    }
    // Neither in use (a filter, no sequence, no envelope depth): it sleeps in the rests as before.
    {
        int skipped = 0;
        compare (nullptr, [] (Instance& a) { a.setup ({ { "Filter", 8 } }, 0.01); }, hits, stopped, skipped);
        CHECK (skipped > 0);
    }
}

// The page's presses fire at the next block only while fresh: one the sound did not take within half a second (it
// was not running) is dropped, and stopping, resetting or deactivating the plug-in drops those not yet taken.
void testPagePresses()
{
    const Stereo x = loop (48000.0, 1.0);
    // Sequence 1 and no clocks: only a press moves it. `before` runs between the press (if any) and the render, which
    // has the song stopped (so a reset's or an activation's restart of the song does not drop a press itself).
    auto render = [&] (bool pressed, const std::function<void (Instance&)>& before) {
        Instance a;
        a.setup ({ { "Sequence", 1 }, { "Inc Clock", 0 } }, 0.3);
        const auto* screen = static_cast<const melodee_screen_t*> (a.plugin->get_extension (a.plugin, MELODEE_SCREEN_CLAP));
        auto in = [&] (uint32_t kind, int value) { melodee_screen_input_t e { kind, value, false }; return screen->input (a.plugin, &e); };
        CHECK (in (MELODEE_SCREEN_STEP, 1) && in (MELODEE_SCREEN_STEP, 1) && in (MELODEE_SCREEN_STEP, 1) && in (MELODEE_SCREEN_WHEEL, 6)); // TRIG, INC
        if (pressed)
            CHECK (in (MELODEE_SCREEN_PUSH, 0));
        if (before)
            before (a);
        return a.run (x, 128, {}, 2, nullptr, false);
    };
    auto restart = [] (Instance& a) { a.plugin->stop_processing (a.plugin), a.plugin->start_processing (a.plugin); };
    auto reset = [] (Instance& a) { a.plugin->reset (a.plugin); };
    auto reactivate = [] (Instance& a) {
        a.plugin->stop_processing (a.plugin), a.plugin->deactivate (a.plugin);
        CHECK (a.plugin->activate (a.plugin, 48000.0, 1, 4096) && a.plugin->start_processing (a.plugin));
    };
    const Stereo quiet = render (false, nullptr);
    CHECK (maxDifference (render (true, nullptr), quiet) > 0.01f); // fresh: the sequence steps on
    // Freshness is counted in frames rendered, not wall time: a press made while no block ran for 600 ms (the host
    // had let the sound sleep; the press itself asks for a block) still fires, once. Stopped, reset or reactivated
    // in between, it does not (below): those clear what waits.
    CHECK (maxDifference (render (true, [] (Instance&) { std::this_thread::sleep_for (std::chrono::milliseconds (600)); }), quiet) > 0.01f);
    CHECK (maxDifference (render (true, restart), render (false, restart)) == 0.0f);
    CHECK (maxDifference (render (true, reset), render (false, reset)) == 0.0f);
    CHECK (maxDifference (render (true, reactivate), render (false, reactivate)) == 0.0f);
}

// The presets through the loop, to listen to: demo-dry.wav is the loop itself.
void demos (const std::string& folder)
{
    const Stereo x = loop (48000.0, 8.0);
    writeWav ((folder + "/demo-dry.wav").c_str(), x.l, x.r, 48000);
    for (const auto& p : cortex::presetTable)
    {
        Instance a;
        CHECK (a.loadPreset (p.key));
        a.setup ({}, 0.2);
        a.played = 0;
        const Stereo o = a.run (x, 128);
        float peak = 0.0f;
        for (size_t i = 0; i < o.l.size(); ++i)
            peak = std::max ({ peak, std::fabs (o.l[i]), std::fabs (o.r[i]) });
        CHECK (peak > 0.05f && peak <= 2.0f);
        std::printf ("  demo %-15s peak %5.2f\n", p.key, peak);
        writeWav ((folder + "/demo-" + p.key + ".wav").c_str(), o.l, o.r, 48000);
    }
    // Every cube, a sweep of Freq with the loop: one file, two seconds a cube.
    Stereo all;
    for (int c = 0; c < cortex::cubeCount; ++c)
    {
        Instance a;
        a.setup ({ { "Filter", c + 1 }, { "Morph", 0.0 }, { "Freq", 0.1 }, { "Transform", 0.3 }, { "Glide", 1.0 } }, 0.05);
        a.played = 0;
        const Stereo x2 = loop (48000.0, 2.0);
        const Stereo o = a.run (x2, 128, { { 0, a.id ("Freq"), 0.9 }, { 0, a.id ("Morph"), 1.0 } });
        all.l.insert (all.l.end(), o.l.begin(), o.l.end());
        all.r.insert (all.r.end(), o.r.begin(), o.r.end());
    }
    writeWav ((folder + "/demo-every-cube.wav").c_str(), all.l, all.r, 48000);
}

// Melodee's screen: the page draws, follows the sound while it plays and the knobs while it does not, moves its title
// for 4 s after it opens, an input or the sound stopping and then rests it, asking for no paints, answers the wheel
// (filters), Push (views) and Minus / Plus (groups), edits, copies and
// restores filters, programs a sequence, fires the sequencer's triggers, names K1-K3, and tells the host what it
// changed. Its pictures go to OUT_FOLDER/screen-*.png.
void testScreen (const std::string& folder)
{
    Instance a;
    const auto* screen = static_cast<const melodee_screen_t*> (a.plugin->get_extension (a.plugin, MELODEE_SCREEN_CLAP));
    CHECK (screen != nullptr);
    if (screen == nullptr)
        return;
    std::vector<uint32_t> pixels (MELODEE_SCREEN_WIDTH * MELODEE_SCREEN_PAGE_HEIGHT, 0);
    melodee_screen_frame_t canvas {};
    canvas.pixels = pixels.data();
    canvas.width = MELODEE_SCREEN_WIDTH, canvas.height = MELODEE_SCREEN_PAGE_HEIGHT, canvas.stride = MELODEE_SCREEN_WIDTH;
    canvas.colour = 0x003ee0ff;
    for (int k = 0; k < 3; ++k)
        canvas.pots[k] = -1, canvas.pots_live[k] = 1;
    uint64_t now = 960;
    auto paint = [&] (bool whole = false) { canvas.redraw_all = whole, canvas.time_ms = now; return screen->draw (a.plugin, &canvas); };
    auto outsideTitleMotion = [&] {
        auto copy = pixels;
        for (int y = 1; y < 26; ++y)
        {
            const int end = y < 11 ? 164 : 143;
            std::fill (copy.begin() + y * canvas.stride + 1, copy.begin() + y * canvas.stride + end, 0);
        }
        return copy;
    };
    auto shot = [&] (const char* name) {
        now += 40;
        paint (true);
        CHECK (writePng ((folder + "/screen-" + name + ".png").c_str(), pixels.data(), 320, 218, 320, 2));
    };
    auto in = [&] (uint32_t kind, int value, bool shift = false) { melodee_screen_input_t e { kind, value, shift }; return screen->input (a.plugin, &e); };
    auto knobs = [&] () { std::vector<clap_id> ids (3); screen->controls (a.plugin, ids.data()); return ids; };
    // Sound for `ms`, a frame painted every 40 ms of it.
    const Stereo music = loop (48000.0, 8.0);
    size_t musicAt = 0;
    int drawn = 0; // frames the page drew while sound played
    auto play = [&] (int ms, OutEvents* out = nullptr) {
        drawn = 0;
        for (int t = 0; t < ms; t += 40)
        {
            Stereo piece;
            piece.l.assign (music.l.begin() + (long) musicAt, music.l.begin() + (long) musicAt + 1920);
            piece.r.assign (music.r.begin() + (long) musicAt, music.r.begin() + (long) musicAt + 1920);
            musicAt = (musicAt + 1920) % (music.l.size() - 1920);
            a.run (piece, 128, {}, 2, out);
            now += 40;
            drawn += paint() ? 1 : 0;
        }
    };

    // For 4 s after the page opens the title moves while the effect sleeps, each paint asking for the next: both the
    // planet and the lettering, the lower word still, and the loop closes. Nothing asks the sound to run.
    CHECK (paint (true));
    const auto first = pixels, still = outsideTitleMotion();
    const int asleepRequests = processes;
    int requests = paints;
    now += 20;
    CHECK (! paint() && paints > requests);
    now += 40;
    CHECK (paint() && outsideTitleMotion() == still);
    auto titleChanged = [&] (int left, int right, int top, int bottom) {
        for (int y = top; y < bottom; ++y)
        {
            const int row = y * canvas.stride;
            if (! std::equal (first.begin() + row + left, first.begin() + row + right, pixels.begin() + row + left))
                return true;
        }
        return false;
    };
    CHECK (titleChanged (36, 43, 11, 16) && titleChanged (54, 128, 3, 22));
    CHECK (! titleChanged (140, 184, 12, 22) && ! titleChanged (164, 184, 0, 26));
    now += 3780;
    CHECK (paint() && pixels == first && processes == asleepRequests);
    // Then it rests on its rest frame, and no paint is asked for until something wakes it: an input (even one with
    // nothing to do, Minus on the first group), a whole paint (the page back in view), the host's Shift.
    now += 200;
    requests = paints;
    CHECK (paint() && paints == requests && outsideTitleMotion() == still);
    const auto resting = pixels;
    now += 1000;
    CHECK (! paint() && paints == requests && pixels == resting);
    CHECK (! in (MELODEE_SCREEN_STEP, -1) && paints > requests);
    now += 60;
    CHECK (paint() && pixels != resting && outsideTitleMotion() == still);
    now += 4100;
    CHECK (paint() && pixels == resting);
    requests = paints;
    now += 60;
    CHECK (! paint() && paints == requests);
    CHECK (paint (true) && paints > requests && pixels != resting);
    now += 60;
    CHECK (paint() && pixels != resting && processes == asleepRequests);
    canvas.shift = true;
    CHECK (paint() && outsideTitleMotion() != still);
    canvas.shift = false;
    CHECK (paint() && outsideTitleMotion() == still);
    // CUBE: K1-K3 are Morph, Freq, Transform; the wheel loads the next filter in use (and tells the host, with the
    // edit filter's fields that now show it).
    CHECK ((knobs() == std::vector<clap_id> { a.id ("Morph"), a.id ("Freq"), a.id ("Transform") }));
    const int flushesBefore = flushes;
    CHECK (in (MELODEE_SCREEN_WHEEL, 2));
    CHECK (a.value ("Filter") == 3 && a.value ("Cube") == 3 && flushes > flushesBefore);
    OutEvents told;
    a.run (Stereo { std::vector<float> (128, 0.0f), std::vector<float> (128, 0.0f) }, 128, {}, 2, &told);
    CHECK (std::any_of (told.values.begin(), told.values.end(), [&] (auto& v) { return v.first == a.id ("Filter") && v.second == 3.0; }));
    CHECK (std::any_of (told.values.begin(), told.values.end(), [&] (auto& v) { return v.first == a.id ("Cube") && v.second == 3.0; }));
    CHECK (in (MELODEE_SCREEN_WHEEL, -5) && a.value ("Filter") == 1);
    CHECK (in (MELODEE_SCREEN_WHEEL, 99) && a.value ("Filter") == cortex::factoryFilterCount); // the last in use
    // A knob moved while no sound runs moves the point on the page.
    a.setup ({ { "Filter", 3 }, { "Morph", 0.2 }, { "Freq", 0.5 }, { "Transform", 0.7 } }, 0.01);
    now += 1000;
    CHECK (paint());
    shot ("cube-still");
    // Sound with the LFO moving the point: the page animates, asking for paints, and draws a new frame each time.
    a.setup ({ { "LFO Sync", 4 }, { "LFO > Morph", 0.8 }, { "LFO > Transform", 0.7 }, { "LFO Phase", 0.25 }, { "Morph", 0.5 }, { "Transform", 0.5 } }, 0.01);
    const int paintsBefore = paints;
    play (1200);
    CHECK (paints > paintsBefore + 10 && drawn >= 28); // a new frame each 40 ms of the 30
    shot ("cube-time");
    CHECK (in (MELODEE_SCREEN_PUSH, 0)); // the MORPH view
    play (400);
    shot ("cube-morph");
    CHECK (in (MELODEE_SCREEN_PUSH, 0));
    // Spread: two points, two responses.
    a.setup ({ { "Spread", 0.8 }, { "Filter", 6 }, { "LFO > Freq", 0.4 } }, 0.01);
    play (1200);
    shot ("cube-spread");
    // Other cubes for a look (the factory's filter for each: the edit filter's Cube choice is its number).
    for (const char* name : { "Phase Drift", "Bell Tower", "Swirl", "Low To Band" })
    {
        double v = 0.0;
        a.params->text_to_value (a.plugin, a.id ("Cube"), name, &v);
        a.setup ({ { "Filter", v }, { "Spread", 0.0 } }, 0.01);
        play (900);
        std::string file = std::string ("cube-") + name;
        std::replace (file.begin(), file.end(), ' ', '-');
        std::transform (file.begin(), file.end(), file.begin(), [] (char c) { return (char) std::tolower ((unsigned char) c); });
        shot (file.c_str());
    }
    // Transform > Dist (F19 Vowel Crush, Transform up: the D bar and the distortion meters), then a sequence
    // running (its place in the header).
    a.setup ({ { "Filter", 19 }, { "Transform", 0.95 }, { "LFO > Morph", 0.0 }, { "LFO > Transform", 0.0 } }, 0.01);
    play (900);
    shot ("cube-distortion");
    CHECK (a.loadPreset ("acid-sequence"));
    play (1300);
    shot ("cube-sequence");
    a.setup ({ { "Sequence", 0 }, { "Filter", 1 } }, 0.01);
    // No sound for a while: the graphs settle. The next request is for the title.
    a.run (Stereo { std::vector<float> (48000, 0.0f), std::vector<float> (48000, 0.0f) }, 128);
    now += 2000;
    paint();
    now += 500; // no block since: the sound sleeps
    paint();
    const int quietPaints = paints;
    const auto quiet = outsideTitleMotion();
    now += 60;
    CHECK (paint() && paints > quietPaints && outsideTitleMotion() == quiet);
    // 4 s after the sound stopped the title rests, and the page asks for no more paints.
    now += 4000;
    paint();
    const int restingPaints = paints;
    now += 60;
    CHECK (! paint() && paints == restingPaints && outsideTitleMotion() == quiet);

    // FILTER: Plus moves to the next group; K1-K3 follow the cursor; Push edits the filter Filter picks (F01 here).
    CHECK (in (MELODEE_SCREEN_STEP, 1));
    CHECK ((knobs() == std::vector<clap_id> { a.id ("Filter"), a.id ("Cube"), a.id ("Morph Offset") }));
    CHECK (in (MELODEE_SCREEN_WHEEL, 3) && in (MELODEE_SCREEN_PUSH, 0) && in (MELODEE_SCREEN_WHEEL, 20) && in (MELODEE_SCREEN_PUSH, 0));
    CHECK (a.value ("Freq Offset") == 20);
    canvas.pots[1] = 90, canvas.pots_live[1] = 0; // K2 has yet to meet its value: the card shows where to move it
    shot ("filter");
    canvas.pots[1] = -1, canvas.pots_live[1] = 1;
    a.setup ({ { "Filter", 2 } }, 0.01);
    CHECK (a.value ("Freq Offset") == 0);
    a.setup ({ { "Filter", 1 } }, 0.01);
    CHECK (a.value ("Freq Offset") == 20); // kept in F01
    // Copy: into the first empty filter, which Filter then picks; Restore: two pushes, back to the factory's (empty).
    CHECK (in (MELODEE_SCREEN_WHEEL, 12) && in (MELODEE_SCREEN_PUSH, 0));
    CHECK (a.value ("Filter") == cortex::factoryFilterCount + 1 && a.value ("Freq Offset") == 20 && a.value ("Cube") == 1);
    CHECK (in (MELODEE_SCREEN_WHEEL, 1) && in (MELODEE_SCREEN_PUSH, 0) && a.value ("Cube") == 1);
    shot ("filter-restore");
    CHECK (in (MELODEE_SCREEN_PUSH, 0) && a.value ("Cube") == 0 && a.value ("Freq Offset") == 0);
    a.setup ({ { "Filter", 1 } }, 0.01);

    // SEQ: off, it says so; K1 picks the sequence. With one, its steps; Push edits a step; while editing, Minus and
    // Plus jump through the choices rather than leave; the step changes (the state saved shows it).
    CHECK (in (MELODEE_SCREEN_STEP, 1));
    shot ("seq-off");
    CHECK ((knobs() == std::vector<clap_id> { a.id ("Sequence"), a.id ("Inc Clock"), a.id ("Reset Clock") }));
    CHECK (a.loadPreset ("acid-sequence"));
    play (1300);
    shot ("seq");
    Memory beforeEdit, afterEdit;
    CHECK (a.state->save (a.plugin, &beforeEdit.out));
    CHECK (in (MELODEE_SCREEN_WHEEL, 2) && in (MELODEE_SCREEN_PUSH, 0) && in (MELODEE_SCREEN_STEP, 1) && in (MELODEE_SCREEN_WHEEL, 1));
    shot ("seq-edit");
    CHECK (in (MELODEE_SCREEN_PUSH, 0));
    CHECK (a.state->save (a.plugin, &afterEdit.out) && afterEdit.bytes != beforeEdit.bytes);
    // TRIG: Inc pressed asks the sound to run (even asleep) and steps the sequence.
    CHECK (in (MELODEE_SCREEN_STEP, 1));
    CHECK ((knobs() == std::vector<clap_id> { a.id ("Sequence"), a.id ("Inc Clock"), a.id ("Dec Clock") }));
    const int processesBefore = processes, runsBefore = screenRuns;
    CHECK (in (MELODEE_SCREEN_WHEEL, 6) && in (MELODEE_SCREEN_PUSH, 0) && processes == processesBefore + 1);
    CHECK (screenRuns == runsBefore + 1);
    play (200);
    shot ("trig");
    // KNOBS: the wheel changes Morph.
    CHECK (in (MELODEE_SCREEN_STEP, 1));
    a.setup ({ { "Sequence", 0 }, { "Morph", 0.5 } }, 0.01);
    CHECK ((knobs() == std::vector<clap_id> { a.id ("Morph"), a.id ("Freq"), a.id ("Transform") }));
    CHECK (in (MELODEE_SCREEN_PUSH, 0) && in (MELODEE_SCREEN_WHEEL, 16));
    CHECK (std::fabs (a.value ("Morph") - (0.5 + 16.0 / 128.0)) < 1e-6);
    CHECK (in (MELODEE_SCREEN_PUSH, 0));
    shot ("knobs");
    CHECK (in (MELODEE_SCREEN_STEP, 1));
    shot ("lfo");
    CHECK (in (MELODEE_SCREEN_STEP, 1));
    shot ("env");
    CHECK (in (MELODEE_SCREEN_STEP, 1));
    shot ("out");
    CHECK (! in (MELODEE_SCREEN_STEP, 1)); // the last group
    for (int i = 0; i < 7; ++i)
        in (MELODEE_SCREEN_STEP, -1);
    canvas.shift = true;
    shot ("cube-shift");
    canvas.shift = false;
    // Freq changed on KNOBS while asleep: the MORPH landscape catches up over four paints, then only the title moves.
    CHECK (in (MELODEE_SCREEN_PUSH, 0)); // MORPH
    now += 2000;
    paint();
    for (int i = 0; i < 4; ++i)
        CHECK (in (MELODEE_SCREEN_STEP, 1));
    CHECK (in (MELODEE_SCREEN_WHEEL, 1) && in (MELODEE_SCREEN_PUSH, 0) && in (MELODEE_SCREEN_WHEEL, -40) && in (MELODEE_SCREEN_PUSH, 0)
           && in (MELODEE_SCREEN_WHEEL, -1));
    for (int i = 0; i < 4; ++i)
        CHECK (in (MELODEE_SCREEN_STEP, -1));
    now += 40;
    paint();
    const auto catchingUp = outsideTitleMotion();
    for (int i = 0; i < 4; ++i)
        now += 60, paint();
    const auto caughtUp = outsideTitleMotion();
    CHECK (caughtUp != catchingUp);
    for (int i = 0; i < 6; ++i)
    {
        now += 60;
        CHECK (paint() && outsideTitleMotion() == caughtUp);
    }
    CHECK (in (MELODEE_SCREEN_PUSH, 0)); // TIME again

    // A preset and a host knob flush both reach the sleeping page within the same title frame.
    const int before = paints;
    CHECK (a.loadPreset ("wide-space"));
    CHECK (paints > before && paint());
    const auto presetBody = outsideTitleMotion();
    Events move;
    move.param (0, a.id ("Morph"), 0.99);
    OutEvents notified;
    a.params->flush (a.plugin, &move.in, &notified.out);
    CHECK (paint() && outsideTitleMotion() != presetBody);
}

void testFxStrip (const std::string& folder)
{
    Instance a;
    const auto* screen = static_cast<const melodee_screen_t*> (a.plugin->get_extension (a.plugin, MELODEE_SCREEN_CLAP));
    CHECK (screen != nullptr);
    if (! screen)
        return;

    constexpr uint32_t width = MELODEE_SCREEN_WIDTH, height = MELODEE_SCREEN_STRIP_HEIGHT, stride = width + 7;
    constexpr uint32_t sentinel = 0xab123456;
    std::vector<uint32_t> padded ((height + 1) * stride, sentinel);
    melodee_screen_frame_t frame {};
    frame.pixels = padded.data();
    frame.width = width, frame.height = height, frame.stride = stride;
    frame.area = MELODEE_SCREEN_STRIP, frame.time_ms = 1440, frame.redraw_all = 1;
    frame.colour = 0x003ee0ff;
    for (int k = 0; k < 3; ++k)
        frame.pots[k] = -1, frame.pots_live[k] = 1;
    CHECK (screen->draw (a.plugin, &frame));
    for (uint32_t y = 0; y < height; ++y)
    {
        auto row = padded.begin() + y * stride;
        CHECK (std::none_of (row, row + width, [=] (auto p) { return p == sentinel; }));
        CHECK (std::all_of (row + width, row + stride, [=] (auto p) { return p == sentinel; }));
    }
    CHECK (std::all_of (padded.end() - stride, padded.end(), [=] (auto p) { return p == sentinel; }));
    const auto still = padded;
    CHECK (writePng ((folder + "/screen-fx-strip.png").c_str(), padded.data(), width, height, stride, 2));
    frame.redraw_all = 0;
    CHECK (! screen->draw (a.plugin, &frame) && padded == still);
    frame.colour = 0;
    CHECK (screen->draw (a.plugin, &frame) && padded[(height - 20) * stride + 4] == 0xff000000u);
    frame.colour = 0x003ee0ff;
    CHECK (screen->draw (a.plugin, &frame) && padded == still);
    frame.pots[1] = 90, frame.pots_live[1] = 0;
    CHECK (screen->draw (a.plugin, &frame));
    CHECK (std::equal (padded.begin(), padded.begin() + (height - 20) * stride, still.begin()));
    CHECK (! std::equal (padded.begin() + (height - 20) * stride, padded.end(), still.begin() + (height - 20) * stride));

    const auto beforeFilter = padded;
    a.setup ({ { "Filter", 5 }, { "Spread", 0.7 } }, 0.1);
    frame.time_ms += 100;
    CHECK (screen->draw (a.plugin, &frame));
    CHECK (! std::equal (padded.begin() + 40 * stride, padded.begin() + 105 * stride, beforeFilter.begin() + 40 * stride));
    const auto incremental = padded;
    frame.redraw_all = 1;
    CHECK (screen->draw (a.plugin, &frame) && padded == incremental);

    // Both area/height mismatches must be rejected before writing a pixel.
    frame.area = MELODEE_SCREEN_PAGE;
    CHECK (! screen->draw (a.plugin, &frame));
    frame.area = MELODEE_SCREEN_STRIP, frame.height = MELODEE_SCREEN_PAGE_HEIGHT;
    CHECK (! screen->draw (a.plugin, &frame) && padded == incremental);

    // Returning to the full page redraws it and preserves its group and current controls.
    std::vector<uint32_t> full (width * MELODEE_SCREEN_PAGE_HEIGHT, sentinel);
    frame.area = MELODEE_SCREEN_PAGE, frame.stride = width, frame.redraw_all = 0, frame.pixels = full.data();
    CHECK (screen->draw (a.plugin, &frame));
    CHECK (std::find (full.begin(), full.end(), sentinel) == full.end());
    const melodee_screen_input_t next { MELODEE_SCREEN_STEP, 1, 0 };
    CHECK (screen->input (a.plugin, &next));
    uint32_t ids[3] {};
    screen->controls (a.plugin, ids);
    CHECK (ids[0] == a.id ("Filter") && ids[1] == a.id ("Cube"));
    frame.area = MELODEE_SCREEN_STRIP, frame.height = height, frame.stride = stride, frame.pixels = padded.data();
    CHECK (screen->draw (a.plugin, &frame));
    screen->controls (a.plugin, ids);
    CHECK (ids[0] == a.id ("Filter") && ids[1] == a.id ("Cube"));
}
} // namespace

// The latency and tail the plug-in reports (measured in tests/engine.cpp), an absurd rate refused at activate,
// and a state loaded while the plug-in is deactivated — asleep, as after the host's idle gate — then played: it
// sounds from its first block as it was saved, the same to the bit as one loaded into a fresh instance, with
// nothing of what rang before the sleep.
void testLatencyTailAndSleep()
{
    {
        Instance a;
        const auto* latency = static_cast<const clap_plugin_latency*> (a.plugin->get_extension (a.plugin, CLAP_EXT_LATENCY));
        const auto* tail = static_cast<const clap_plugin_tail*> (a.plugin->get_extension (a.plugin, CLAP_EXT_TAIL));
        CHECK (latency != nullptr && latency->get (a.plugin) == 0);
        CHECK (tail != nullptr && tail->get (a.plugin) == 528000u); // the measured 11 s at 48 kHz, in frames
    }
    {
        Instance a (44100.0);
        const auto* tail = static_cast<const clap_plugin_tail*> (a.plugin->get_extension (a.plugin, CLAP_EXT_TAIL));
        CHECK (tail != nullptr && tail->get (a.plugin) == 485100u); // the same 11 s at 44.1 kHz
    }
    {
        const auto* d = factory->get_plugin_descriptor (factory, 0);
        const clap_plugin* p = factory->create_plugin (factory, &host, d->id);
        CHECK (p != nullptr && p->init (p));
        CHECK (! p->activate (p, 0.0, 1, 4096) && ! p->activate (p, std::numeric_limits<double>::quiet_NaN(), 1, 4096)
               && ! p->activate (p, 1e12, 1, 4096));
        CHECK (p->activate (p, 48000.0, 1, 4096));
        p->deactivate (p);
        p->destroy (p);
    }
    Memory saved;
    {
        Instance a;
        a.setup ({ { "Glide", 1.0 }, { "Morph", 0.8 }, { "Freq", 0.1 }, { "Filter", 8 }, { "Output", -12.0 }, { "Emphasis", 1.0 } }, 0.01);
        CHECK (a.state->save (a.plugin, &saved.out));
    }
    const Stereo x = loop (48000.0, 1.0);
    Instance asleep;
    asleep.setup ({ { "Filter", 8 }, { "Emphasis", 1.0 }, { "Freq", 0.0 } }, 0.01);
    asleep.run (x, 128); // rung up, then put to sleep
    asleep.plugin->stop_processing (asleep.plugin);
    asleep.plugin->deactivate (asleep.plugin);
    Memory intoAsleep;
    intoAsleep.bytes = saved.bytes;
    CHECK (asleep.state->load (asleep.plugin, &intoAsleep.in));
    CHECK (asleep.plugin->activate (asleep.plugin, 48000.0, 1, 4096) && asleep.plugin->start_processing (asleep.plugin));
    const Stereo woke = asleep.run (x, 128);
    Instance fresh;
    fresh.plugin->stop_processing (fresh.plugin);
    fresh.plugin->deactivate (fresh.plugin);
    Memory intoFresh;
    intoFresh.bytes = saved.bytes;
    CHECK (fresh.state->load (fresh.plugin, &intoFresh.in));
    CHECK (fresh.plugin->activate (fresh.plugin, 48000.0, 1, 4096) && fresh.plugin->start_processing (fresh.plugin));
    const Stereo expected = fresh.run (x, 128);
    CHECK (woke.l == expected.l && woke.r == expected.r);
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: host CortexFilter.clap OUT_FOLDER\n");
        return 2;
    }
    void* library = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr)
    {
        std::printf ("FAIL dlopen: %s\n", dlerror());
        return 1;
    }
    entry = static_cast<const clap_plugin_entry*> (dlsym (library, "clap_entry"));
    if (entry == nullptr || ! entry->init (argv[1]))
    {
        std::printf ("FAIL no clap_entry\n");
        return 1;
    }
    factory = static_cast<const clap_plugin_factory*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
    const std::string folder = argv[2];
    testDescription();
    testParameters();
    testState();
    testPresets();
    testResponse();
    testKnownFilters();
    testBlockSizes();
    testRouting();
    testExtremes();
    testSequencerGaps();
    testIdleGate();
    testPagePresses();
    testLatencyTailAndSleep();
    demos (folder);
    testScreen (folder);
    testFxStrip (folder);
    entry->deinit();
    std::printf ("host: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
