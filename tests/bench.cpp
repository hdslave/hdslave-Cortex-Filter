// Cortex Filter's cost: runs the loop through the CLAP entry for SECONDS at 48 kHz in 128-frame blocks, as Melodee plays
// it, and prints the time a block takes here. tools/bench.sh runs it under callgrind for instruction counts (exact
// and repeatable, unlike timings on a computer that is not the MPC).
//   bench [worst|typical|still|paint|morph] [SECONDS]
// paint: the worst setting with the CUBE page painted every 40 ms of sound, as Melodee's screen thread would while it
// animates (its instructions less the worst's are the page's); morph: the same in the MORPH view.
#include "../src/cortex.cpp"
#include "signal.h"

#include <chrono>
#include <string>
#include <vector>

int main (int argc, char** argv)
{
    const std::string mode = argc > 1 ? argv[1] : "worst";
    const double seconds = argc > 2 ? std::atof (argv[2]) : 10.0;
    constexpr double rate = 48000.0;
    constexpr uint32_t block = 128;

    static const clap_host host { CLAP_VERSION, nullptr, "bench", "", "", "1", [] (const clap_host*, const char*) -> const void* { return nullptr; },
                                  [] (const clap_host*) {}, [] (const clap_host*) {}, [] (const clap_host*) {} };
    clap_entry.init ("");
    const auto* factory = static_cast<const clap_plugin_factory*> (clap_entry.get_factory (CLAP_PLUGIN_FACTORY_ID));
    std::vector<uint32_t> pixels (MELODEE_SCREEN_WIDTH * MELODEE_SCREEN_HEIGHT, 0);
    melodee_screen_canvas_t canvas {};
    canvas.pixels = pixels.data();
    canvas.width = MELODEE_SCREEN_WIDTH, canvas.height = MELODEE_SCREEN_HEIGHT, canvas.stride = MELODEE_SCREEN_WIDTH;
    canvas.accent = 0xff3ee0ff;
    uint64_t paintsDrawn = 0;
    const clap_plugin* plugin = factory->create_plugin (factory, &host, "com.hdslave.cortex-filter");
    plugin->init (plugin);
    plugin->activate (plugin, rate, 1, block);
    plugin->start_processing (plugin);

    // worst: a seven-band cube, both sides apart (Spread), the point always moving (a fast LFO on all three axes
    // and the envelope), Drive on. typical: a five-band cube moved slowly by the LFO. still: a cube standing still.
    std::vector<std::pair<uint32_t, double>> values;
    using namespace cortex::ParamId;
    if (mode == "worst" || mode == "paint" || mode == "morph")
        values = { { cube, 2 }, { spread, 0.6 }, { lfoRate, 0.9 }, { lfoMorph, 0.7 }, { lfoFreq, 0.5 }, { lfoTransform, 0.6 }, { envFreq, 0.4 },
                   { drive, 9 }, { mix, 0.8 } };
    else if (mode == "typical")
        values = { { cube, 6 }, { lfoSync, 4 }, { lfoMorph, 0.5 } };
    else
        values = { { cube, 4 }, { morph, 0.4 } };

    struct Events
    {
        std::vector<clap_event_param_value> list;
        clap_input_events in { this, [] (const clap_input_events* e) { return (uint32_t) static_cast<const Events*> (e->ctx)->list.size(); },
                               [] (const clap_input_events* e, uint32_t i) { return &static_cast<const Events*> (e->ctx)->list[i].header; } };
    } first, none;
    for (auto& [id, v] : values)
    {
        clap_event_param_value e {};
        e.header = { sizeof (e), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 };
        e.param_id = id;
        e.note_id = e.port_index = e.channel = e.key = -1;
        e.value = v;
        first.list.push_back (e);
    }
    clap_output_events out { nullptr, [] (const clap_output_events*, const clap_event_header*) { return true; } };

    const Stereo music = loop (rate, 4.0);
    std::vector<float> left (block), right (block);
    float* outs[2] = { left.data(), right.data() };
    clap_audio_buffer output {};
    output.data32 = outs;
    output.channel_count = 2;
    const uint64_t blocks = (uint64_t) (seconds * rate / block);
    double sum = 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    for (uint64_t b = 0; b < blocks; ++b)
    {
        const size_t at = (size_t) (b * block) % (music.l.size() - block);
        float* ins[2] = { const_cast<float*> (music.l.data()) + at, const_cast<float*> (music.r.data()) + at };
        clap_audio_buffer input {};
        input.data32 = ins;
        input.channel_count = 2;
        clap_event_transport t {};
        t.header = { sizeof (t), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0 };
        t.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_IS_PLAYING;
        t.tempo = 120.0;
        t.song_pos_beats = (clap_beattime) std::llround ((double) (b * block) * 2.0 / rate * (double) CLAP_BEATTIME_FACTOR);
        clap_process p {};
        p.frames_count = block;
        p.transport = &t;
        p.audio_inputs = &input, p.audio_inputs_count = 1;
        p.audio_outputs = &output, p.audio_outputs_count = 1;
        p.in_events = b == 0 ? &first.in : &none.in;
        p.out_events = &out;
        plugin->process (plugin, &p);
        sum += left[0];
        if ((mode == "paint" || mode == "morph") && b % 15 == 14) // every 40 ms of sound
        {
            static const auto* screen = static_cast<const clap_plugin_melodee_screen*> (plugin->get_extension (plugin, CLAP_EXT_MELODEE_SCREEN));
            if (mode == "morph" && b == 14)
            {
                const melodee_screen_input_t push { MELODEE_SCREEN_PUSH, 0, false };
                screen->input (plugin, &push);
            }
            canvas.now_ms = 1000 + (b * block * 1000) / (uint64_t) rate;
            canvas.whole = b == 14;
            paintsDrawn += screen->paint (plugin, &canvas) ? 1 : 0;
        }
    }
    const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("%s: %llu blocks, %.2f us a block here (checksum %.6f)", mode.c_str(), (unsigned long long) blocks, us / (double) std::max<uint64_t> (blocks, 1), sum);
    if (mode == "paint" || mode == "morph")
        std::printf (", %llu frames painted", (unsigned long long) paintsDrawn);
    std::printf ("\n");
    plugin->stop_processing (plugin);
    plugin->deactivate (plugin);
    plugin->destroy (plugin);
    clap_entry.deinit();
    return 0;
}
