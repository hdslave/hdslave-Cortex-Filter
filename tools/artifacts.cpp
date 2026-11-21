// What the control steps cost in sound: a 440 Hz tone through filters moving fast, and how much of the output lands
// above 3 kHz (in dB under the whole output). A filter moving smoothly puts a tone's energy only near the tone; the
// filter's steps (every CORTEX_CONTROL_FRAMES frames) add images of it near the step rate. tools/artifacts.sh builds
// this at 8 frames (the plug-in) and at 1 (worked out every frame) and prints both.
//   artifacts
#include "engine.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace cortex;

namespace
{
// Energy above 3 kHz, in dB under the whole: four high-pass biquads (48 dB an octave) at 3 kHz.
double highShareDb (const std::vector<float>& x)
{
    const double w = 2.0 * M_PI * 3000.0 / 48000.0, alpha = std::sin (w) / (2.0 * 0.7071), c = std::cos (w);
    const double b0 = (1.0 + c) / 2.0, b1 = -(1.0 + c), b2 = b0, a0 = 1.0 + alpha, a1 = -2.0 * c, a2 = 1.0 - alpha;
    std::vector<double> y (x.begin(), x.end());
    for (int stage = 0; stage < 4; ++stage)
    {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (double& v : y)
        {
            const double out = (b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
            x2 = x1, x1 = v, y2 = y1, y1 = out, v = out;
        }
    }
    double all = 0.0, high = 0.0;
    for (size_t i = 9600; i < x.size(); ++i)
        all += (double) x[i] * x[i], high += y[i] * y[i];
    return 10.0 * std::log10 (high / all + 1e-30);
}

struct Case
{
    const char* name;
    std::vector<std::pair<uint32_t, float>> values;
    bool gated = false; // the tone switched on and off every 100 ms (for the envelope)
};
} // namespace

int main()
{
    using namespace ParamId;
    const Case cases[] = {
        { "LFO 5 Hz sweeping Freq end to end, resonant", { { filter, 1 }, { morph, 0.3f }, { transform, 1.0f }, { emphasis, 0.5f }, { lfoRate, lfoKnob (5.0f) }, { lfoFreq, 1.0f } } },
        { "LFO 20 Hz on Freq, half depth, resonant", { { filter, 1 }, { morph, 0.3f }, { transform, 1.0f }, { lfoRate, 1.0f }, { lfoFreq, 0.5f } } },
        { "LFO 3 Hz across vowels (Morph, Transform)", { { filter, 3 }, { lfoRate, lfoKnob (3.0f) }, { lfoMorph, 1.0f }, { lfoTransform, 1.0f } } },
        { "LFO 1 Hz through Bell Tower's ringing peaks", { { filter, 8 }, { transform, 1.0f }, { lfoRate, lfoKnob (1.0f) }, { lfoMorph, 1.0f }, { lfoFreq, 0.4f } } },
        { "envelope (0.1 ms attack) on Freq, gated tone", { { filter, 10 }, { envFreq, 1.0f }, { envAttack, 0.0f }, { envRelease, 0.2f } }, true },
        { "standing still (the floor)", { { filter, 1 }, { morph, 0.3f }, { transform, 1.0f } } },
    };
    for (const Case& c : cases)
    {
        Engine e;
        e.prepare (48000.0, 512);
        for (auto [id, v] : c.values)
            e.set (id, v);
        e.reset();
        const size_t frames = 48000 * 3;
        std::vector<float> in (frames), outL (frames), outR (frames);
        for (size_t i = 0; i < frames; ++i)
            in[i] = 0.5f * std::sin (2.0f * (float) M_PI * 440.0f * (float) i / 48000.0f) * (! c.gated || (i / 4800) % 2 == 0 ? 1.0f : 0.0f);
        e.render (in.data(), in.data(), outL.data(), outR.data(), (uint32_t) frames);
        std::printf ("  %-48s %7.1f dB\n", c.name, highShareDb (outL));
    }
}
