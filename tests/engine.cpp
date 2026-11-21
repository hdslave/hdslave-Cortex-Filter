// Cortex Filter's engine and filter maths, checked from the inside: the cubes' data, the blend between corners, every point's
// filter being stable, the response maths against known filters, the LFO (shapes, rate, in step with the song,
// stopped, jumps), the envelope follower, the glides, Spread, the point staying inside the cube, and the page
// (its point and inputs) without a host.
//   engine-test
#include "engine.h"
#include "morph.h"
#include "params.h"
#include "presets.h"
#include "screen.h"
#include "signal.h"

#include <cmath>
#include <cstring>
#include <cstdio>
#include <random>
#include <vector>

using namespace cortex;

namespace
{
int failures = 0, checks = 0;
#define CHECK(c) do { ++checks; if (! (c)) { ++failures; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define NEAR(a, b, tol) do { ++checks; const double _a = (a), _b = (b); if (! (std::fabs (_a - _b) <= (tol))) { ++failures; std::printf ("FAIL %s:%d  %s = %g, %s = %g (tolerance %g)\n", __FILE__, __LINE__, #a, _a, #b, _b, (double) (tol)); } } while (0)

void testCubes()
{
    // Every cube uses some bands, and every corner's band has a positive frequency and Q.
    for (int c = 0; c < cubeCount; ++c)
    {
        const CubeTable& t = cubeTable (c);
        CHECK (t.bands >= 1 && t.bands <= maxBands);
        for (int corner = 0; corner < 8; ++corner)
            for (int b = 0; b < maxBands; ++b)
                CHECK (cubeSpecs[c].corner[corner].band[b].hz > 0.0f && cubeSpecs[c].corner[corner].band[b].q > 0.0f);
    }
    // Weights sum to one and pick a corner at a corner.
    float w[8];
    cornerWeights ({ 0.3f, 0.6f, 0.9f }, w);
    float sum = 0.0f;
    for (float v : w)
        sum += v;
    NEAR (sum, 1.0, 1e-6);
    cornerWeights ({ 1.0f, 0.0f, 1.0f }, w);
    NEAR (w[5], 1.0, 0.0);
    // A shifted cube: Freq at its ends moves every band by the cube's ratios (Low To Band: 1 kHz to 31.25 Hz, 16 kHz).
    Design lo, hi;
    design (cubeTable (0), { 0, 0, 0 }, 0.0f, 48000.0f, lo);
    design (cubeTable (0), { 0, 1, 0 }, 0.0f, 48000.0f, hi);
    NEAR (lo.g[0], std::tan (pi * 31.25f / 48000.0f), 1e-6);
    NEAR (hi.g[0], std::tan (pi * 16000.0f / 48000.0f), 1e-4);
}

// Loudness: no point of any cube (a 9 x 9 x 9 grid, Emphasis 0) peaks above +18 dB anywhere up to 0.45 of the rate
// (where bands pushed past it would pile up), at 44.1 and 48 kHz; and the resonating cubes' corners at the middle of
// Freq lose 2 to 7 dB on pink noise, so moving through a cube keeps its level.
void testLevels()
{
    const float rate = 48000.0f;
    for (float r : { 44100.0f, 48000.0f })
        for (int c = 0; c < cubeCount; ++c)
        {
            double top = -1e9;
            for (int i = 0; i < 9 * 9 * 9; ++i)
            {
                Design d;
                design (cubeTable (c), { (i % 9) / 8.0f, ((i / 9) % 9) / 8.0f, (i / 81) / 8.0f }, 0.0f, r, d);
                for (int f = 0; f < 240; ++f)
                    top = std::max (top, (double) responseDb (d, 20.0f * std::pow (0.45f * r / 20.0f, f / 239.0f), r));
            }
            if (top > 18.0)
                std::printf ("  %s peaks at %+.1f dB at %.0f Hz\n", cubeNames[c], top, r);
            CHECK (top <= 18.0);
        }
    for (int c : { 2, 3, 6, 7, 12, 15 }) // vowels, wah, bells, plates, chords
        for (int corner = 0; corner < 4; ++corner)
        {
            Design d;
            design (cubeTable (c), { (float) (corner & 1), 0.5f, (float) (corner >> 1) }, 0.0f, rate, d);
            double power = 0.0;
            for (int f = 0; f < 600; ++f)
                power += std::pow (10.0, responseDb (d, 20.0f * std::pow (1000.0f, (f + 0.5f) / 600), rate) / 10.0) / 600.0;
            const double pink = 10.0 * std::log10 (power);
            if (pink < -7.0 || pink > -2.0)
                std::printf ("  %s corner %d: %+.1f dB on pink noise\n", cubeNames[c], corner, pink);
            CHECK (pink >= -7.0 && pink <= -2.0);
        }
}

// Every point of every cube, at either end of Emphasis and at three rates, is a stable filter: finite coefficients,
// a positive frequency and damping, and an impulse that rings down.
void testStable()
{
    std::mt19937 random (7);
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);
    for (float rate : { 44100.0f, 48000.0f, 96000.0f })
        for (int c = 0; c < cubeCount; ++c)
            for (int n = 0; n < 24; ++n)
            {
                const Position p { unit (random), unit (random), unit (random) };
                const float emphasis = n < 8 ? 1.0f : (n < 16 ? -1.0f : 2.0f * unit (random) - 1.0f);
                Design d;
                design (cubeTable (c), p, emphasis, rate, d);
                bool ok = std::isfinite (d.wet) && std::isfinite (d.dryGain);
                for (int b = 0; b < d.bands; ++b)
                {
                    const auto& k = d.band[b];
                    ok &= d.g[b] > 0.0f && d.k[b] > 0.0f && std::isfinite (k.a1 + k.a2 + k.a3 + k.m0 + k.m1 + k.m2);
                }
                CHECK (ok);
                if (n == 0 && rate == 48000.0f)
                {
                    BandState s[maxBands];
                    double late = 0.0;
                    for (int i = 0; i < 48000 * 20; ++i) // a Q of 80 at 27 Hz takes a second to fall to a third
                    {
                        float y = i == 0 ? 1.0f : 0.0f;
                        for (int b = 0; b < d.bands; ++b)
                            y = runBand (d.band[b], s[b], y);
                        if (i >= 48000 * 20 - 4800)
                            late = std::max (late, (double) std::fabs (y));
                    }
                    CHECK (late < 1e-3);
                }
            }
}

// The response maths against filters known by heart: a 2-pole low pass at its cutoff is Q (-3 dB at 0.707); a bell
// is its gain at its centre and 0 dB far away; a notch is silent at its centre; an all pass is flat.
void testResponseMaths()
{
    const float rate = 48000.0f;
    auto single = [&] (Band band) {
        Design d;
        d.bands = 1;
        const float g = std::tan (pi * band.hz / rate), k = 1.0f / band.q;
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float bell = std::pow (10.0f, band.bellDb / 20.0f) - 1.0f, bp = band.bp + bell;
        d.band[0] = { a1, a2, a3, band.dry + band.hp, k * (bp - band.hp), band.lp - band.hp };
        d.g[0] = g, d.k[0] = k, d.wet = 1.0f, d.dryGain = 0.0f;
        return d;
    };
    NEAR (responseDb (single (lowpass (1000, 0.707f)), 1000, rate), -3.01, 0.02);
    NEAR (responseDb (single (lowpass (1000, 0.707f)), 20, rate), 0.0, 0.01);
    NEAR (responseDb (single (highpass (1000, 0.707f)), 1000, rate), -3.01, 0.02);
    NEAR (responseDb (single (peak (1000, 4.0f, 12.0f)), 1000, rate), 12.0, 0.01);
    NEAR (responseDb (single (peak (1000, 4.0f, 12.0f)), 30, rate), 0.0, 0.05);
    CHECK (responseDb (single (notch (1000, 4.0f)), 1000, rate) < -100.0f);
    for (float hz : { 50.0f, 700.0f, 1000.0f, 3000.0f, 12000.0f })
        NEAR (responseDb (single (allpass (1000, 2.0f)), hz, rate), 0.0, 0.01);
    // The same band, measured: its impulse's spectrum at a few frequencies.
    for (Band band : { lowpass (800, 3.0f), peak (2000, 6.0f, -9.0f), bandpass (300, 2.0f), allpass (5000, 1.0f) })
    {
        const Design d = single (band);
        BandState s;
        std::vector<float> h (1 << 15);
        for (size_t i = 0; i < h.size(); ++i)
            h[i] = runBand (d.band[0], s, i == 0 ? 1.0f : 0.0f);
        for (float hz : { 100.0f, 800.0f, 2000.0f, 9000.0f })
        {
            double re = 0.0, im = 0.0;
            for (size_t i = 0; i < h.size(); ++i)
                re += h[i] * std::cos (2.0 * M_PI * hz * i / rate), im -= h[i] * std::sin (2.0 * M_PI * hz * i / rate);
            NEAR (10.0 * std::log10 (re * re + im * im), responseDb (d, hz, rate), 0.01);
        }
    }
}

// An engine run in 8-frame blocks, each its own transport, so a control step falls on each block's first frame.
struct Rig
{
    Engine e;
    float in[2][8] {}, out[2][8] {};
    explicit Rig (double rate = 48000.0) { e.prepare (rate, 8); }
    void block (bool playing, bool placed, double beats, double bpm = 120.0)
    {
        e.transport (playing, placed, beats, bpm);
        e.render (in[0], in[1], out[0], out[1], 8);
    }
};

void testLfo()
{
    // Shapes.
    NEAR (lfoValue (Shape::sine, 0.25, 0), 1.0, 1e-6);
    NEAR (lfoValue (Shape::sine, 3.75, 0), -1.0, 1e-5);
    NEAR (lfoValue (Shape::triangle, 0.125, 0), 0.5, 1e-6);
    NEAR (lfoValue (Shape::triangle, 0.5, 0), 0.0, 1e-6);
    NEAR (lfoValue (Shape::sawUp, 0.75, 0), 0.5, 1e-6);
    NEAR (lfoValue (Shape::sawDown, 0.75, 0), -0.5, 1e-6);
    CHECK (lfoValue (Shape::square, 0.2, 0) == 1.0f && lfoValue (Shape::square, 0.7, 0) == -1.0f);
    CHECK (lfoValue (Shape::sampleHold, 5.1, 1) == lfoValue (Shape::sampleHold, 5.9, 1));
    CHECK (lfoValue (Shape::sampleHold, 5.1, 1) != lfoValue (Shape::sampleHold, 6.1, 1));
    CHECK (lfoValue (Shape::sampleHold, 5.1, 0) != lfoValue (Shape::sampleHold, 5.1, 1));
    NEAR (lfoValue (Shape::drift, 7.0, 2), lfoValue (Shape::sampleHold, 7.0, 2), 1e-6);
    NEAR (lfoValue (Shape::drift, 7.999999, 2), lfoValue (Shape::sampleHold, 8.0, 2), 1e-4);
    for (Shape s : { Shape::sampleHold, Shape::drift })
        for (int n = 0; n < 200; ++n)
        {
            const float v = lfoValue (s, n * 0.37, (uint32_t) n % 3);
            CHECK (v >= -1.0f && v <= 1.0f);
        }

    // In step with the song: 1 Bar on Morph, full depth around the middle, so Morph = 0.5 + 0.5 sin(2 pi beats / 4).
    {
        Rig r;
        r.e.set (ParamId::morph, 0.5f), r.e.set (ParamId::lfoSync, 4), r.e.set (ParamId::lfoMorph, 1.0f);
        r.e.reset();
        bool ok = true;
        for (double beats : { 0.0, 0.5, 1.0, 1.37, 3.0, 9.25, 17.0, 2.0 }) // the last two: a jump ahead, then back
        {
            r.block (true, true, beats);
            const double expect = std::clamp (0.5 + 0.5 * std::sin (2.0 * M_PI * beats / 4.0), 0.0, 1.0);
            if (std::fabs (r.e.left().x - expect) > 1e-5)
                ok = false, std::printf ("  synced LFO at beat %g: %g, not %g\n", beats, r.e.left().x, expect);
        }
        CHECK (ok);
        // Stopped, it runs on at the tempo: 8 frames at 120 bpm and 48 kHz are 1/3000 of a beat.
        r.block (true, true, 1.0);
        const double at = r.e.lfoPlace();
        for (int i = 0; i < 300; ++i)
            r.block (false, true, 1.0);
        NEAR (r.e.lfoPlace() - at, 300.0 / 3000.0 / 4.0, 1e-9);
        // Phase offsets: Freq a quarter cycle behind Morph's place, Transform half.
        r.e.set (ParamId::lfoFreq, 1.0f), r.e.set (ParamId::freq, 0.5f), r.e.set (ParamId::lfoTransform, 1.0f), r.e.set (ParamId::transform, 0.5f);
        r.e.set (ParamId::glide, 0.0f);
        for (int i = 0; i < 2000; ++i)
            r.block (true, true, 0.4);
        NEAR (r.e.left().y, 0.5 + 0.5 * std::sin (2.0 * M_PI * (0.1 + 0.25)), 1e-5);
        NEAR (r.e.left().z, 0.5 + 0.5 * std::sin (2.0 * M_PI * (0.1 + 0.5)), 1e-5);
    }
    // Free: LFO Rate's cycles a second, from its knob.
    {
        Rig r;
        r.e.set (ParamId::lfoRate, lfoKnob (2.0f));
        r.e.reset();
        for (int i = 0; i < 6000; ++i) // one second
            r.block (true, true, i * 8.0 / 24000.0);
        NEAR (r.e.lfoPlace(), 2.0, 1e-3);
    }
}

void testEnvelopeGlideSpread()
{
    // The envelope reads -6 dB as 42/48 of the way up; with Env Gain 6 dB it is full.
    {
        Rig r;
        for (int c = 0; c < 2; ++c)
            for (float& v : r.in[c])
                v = 0.5f;
        r.e.set (ParamId::envFreq, 1.0f), r.e.set (ParamId::freq, 0.0f);
        r.e.reset();
        for (int i = 0; i < 6000; ++i)
            r.block (false, false, 0.0);
        NEAR (r.e.envelope(), 42.0 / 48.0, 2e-3);
        NEAR (r.e.left().y, r.e.envelope(), 1e-6);
        r.e.set (ParamId::envGain, 6.0f);
        for (int i = 0; i < 6000; ++i)
            r.block (false, false, 0.0);
        NEAR (r.e.envelope(), 1.0, 1e-3);
        // Silence: it falls back to nothing.
        for (int c = 0; c < 2; ++c)
            for (float& v : r.in[c])
                v = 0.0f;
        for (int i = 0; i < 60000; ++i)
            r.block (false, false, 0.0);
        NEAR (r.e.envelope(), 0.0, 0.0);
    }
    // Glide: a knob's step is 63 % of the way after its time.
    {
        Rig r;
        r.e.set (ParamId::glide, glideKnob (0.1f)), r.e.set (ParamId::morph, 0.0f);
        r.e.reset();
        r.e.set (ParamId::morph, 1.0f);
        for (int i = 0; i < 600; ++i) // 4800 frames: 0.1 s
            r.block (false, false, 0.0);
        NEAR (r.e.left().x, 1.0 - std::exp (-1.0), 0.01);
        for (int i = 0; i < 60000; ++i)
            r.block (false, false, 0.0);
        CHECK (r.e.left().x == 1.0f); // settled exactly
    }
    // Spread: the two sides sit spreadWidth * Spread apart, inside the cube, around Morph.
    for (float morph : { 0.0f, 0.3f, 1.0f })
        for (float spread : { 0.25f, 1.0f })
        {
            Rig r;
            r.e.set (ParamId::morph, morph), r.e.set (ParamId::spread, spread);
            r.e.reset();
            r.block (false, false, 0.0);
            CHECK (r.e.split());
            NEAR (r.e.right().x - r.e.left().x, spreadWidth * spread, 1e-6);
            CHECK (r.e.left().x >= 0.0f && r.e.right().x <= 1.0f);
            CHECK (r.e.left().y == r.e.right().y && r.e.left().z == r.e.right().z);
        }
    // Depths past the cube's walls stop at them.
    {
        Rig r;
        r.e.set (ParamId::morph, 0.9f), r.e.set (ParamId::lfoMorph, 1.0f), r.e.set (ParamId::lfoShape, (float) Shape::square);
        r.e.set (ParamId::lfoSync, 7);
        r.e.reset();
        r.block (true, true, 0.1);
        CHECK (r.e.left().x == 1.0f);
        r.block (true, true, 0.6);
        NEAR (r.e.left().x, 0.4, 1e-6);
    }
}

// The sound: Mix 0 gives the input back to the bit; a mono signal through a still filter comes out the same on both
// sides; gains above full scale are held under 2.
void testSound()
{
    Engine e;
    e.prepare (48000.0, 512);
    e.set (ParamId::mix, 0.0f), e.set (ParamId::cube, 9), e.set (ParamId::drive, 12.0f), e.set (ParamId::emphasis, 1.0f);
    e.reset();
    const Stereo x = loop (48000.0, 1.0);
    std::vector<float> l (x.l.size()), r (x.l.size());
    e.render (x.l.data(), x.r.data(), l.data(), r.data(), (uint32_t) l.size());
    bool same = true;
    for (size_t i = 0; i < l.size(); ++i)
        same &= l[i] == x.l[i] && r[i] == x.r[i];
    CHECK (same);

    Engine m;
    m.prepare (48000.0, 512);
    m.set (ParamId::cube, 2), m.set (ParamId::morph, 0.4f);
    m.reset();
    m.render (x.l.data(), x.l.data(), l.data(), r.data(), (uint32_t) l.size());
    bool mono = true;
    for (size_t i = 0; i < l.size(); ++i)
        mono &= l[i] == r[i];
    CHECK (mono);

    Engine loud;
    loud.prepare (48000.0, 512);
    loud.set (ParamId::cube, 12), loud.set (ParamId::transform, 1.0f), loud.set (ParamId::emphasis, 1.0f), loud.set (ParamId::output, 12.0f);
    loud.reset();
    const Stereo n = whiteNoise (48000, 1.0f, 3);
    loud.render (n.l.data(), n.r.data(), l.data(), r.data(), 48000);
    float peak = 0.0f;
    bool finite = true;
    for (size_t i = 0; i < 48000; ++i)
        peak = std::max ({ peak, std::fabs (l[i]), std::fabs (r[i]) }), finite &= std::isfinite (l[i]) && std::isfinite (r[i]);
    CHECK (finite && peak <= 2.0f && peak > 1.0f);
    CHECK (limit (0.75f) == 0.75f && limit (-1.0f) == -1.0f && limit (1e30f) <= 2.0f);
    CHECK (saturate (3.5f) == 1.0f && std::fabs (saturate (0.01f) - 0.01f) < 1e-6f);
}

// The page with no host: it follows the knobs while no sound runs, answers the wheel and Push on CUBE, and names
// Morph, Freq and Transform for K1-K3.
void testPage()
{
    std::atomic<float> values[paramCount];
    for (uint32_t i = 0; i < paramCount; ++i)
        values[i].store (paramTable[i].def);
    Feed feed;
    Screen screen (values, feed);
    std::vector<uint32_t> pixels (hdui::width * hdui::height);
    hdui::Host h;
    h.nowMs = 1000;
    screen.update (h.nowMs);
    CHECK (screen.paint (h, pixels.data(), hdui::width, true));
    CHECK (! screen.animating (h));
    uint32_t ids[3];
    screen.knobs (ids);
    CHECK (ids[0] == ParamId::morph && ids[1] == ParamId::freq && ids[2] == ParamId::transform);
    hdui::Edits edits;
    CHECK (screen.input (hdui::Key::wheel, 3, false, edits) && edits.count == 1 && edits.list[0].id == ParamId::cube && edits.list[0].value == 3.0f);
    hdui::Edits none;
    CHECK (screen.input (hdui::Key::push, 0, false, none) && screen.morphView());
    values[paramIndex (ParamId::cube)].store (3.0f);
    values[paramIndex (ParamId::morph)].store (0.75f);
    h.nowMs += 50;
    screen.update (h.nowMs);
    CHECK (screen.paint (h, pixels.data(), hdui::width, false)); // the point moved with the knob
    CHECK (screen.morphStale() == 0); // a new cube: the whole MORPH landscape at once
    // Freq moved: the landscape follows a few slices a paint, all of it within four.
    values[paramIndex (ParamId::freq)].store (0.2f);
    int paintsToCatchUp = 0;
    do
    {
        h.nowMs += 50;
        screen.update (h.nowMs);
        screen.paint (h, pixels.data(), hdui::width, false);
    } while (screen.morphStale() > 0 && ++paintsToCatchUp < 10);
    CHECK (screen.morphStale() == 0 && paintsToCatchUp <= 3);
    // Sound running: it animates, then stops a little after the sound does.
    feed.blocks.fetch_add (1);
    h.nowMs += 50;
    screen.update (h.nowMs);
    CHECK (screen.animating (h));
    h.nowMs += 1000;
    screen.update (h.nowMs);
    CHECK (! screen.animating (h));
}

// Times hold at every rate: the free LFO's cycles a second, a glide's time constant, the envelope's release, the
// synced LFO's place, and Drive, Mix and Output's 20 ms.
void testRates()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        const uint32_t second = (uint32_t) rate;
        auto quiet = [&] (Engine& e, uint32_t frames, float level = 0.0f) {
            std::vector<float> in (frames, level), out (frames);
            for (uint32_t at = 0; at < frames; at += 128)
            {
                const uint32_t n = std::min<uint32_t> (128, frames - at);
                e.transport (false, false, 0.0, 120.0);
                e.render (in.data() + at, in.data() + at, out.data() + at, out.data() + at, n);
            }
            return out;
        };
        {
            Engine e;
            e.prepare (rate, 128);
            e.set (ParamId::lfoRate, lfoKnob (2.0f));
            e.reset();
            quiet (e, second);
            NEAR (e.lfoPlace(), 2.0, 2e-3);
        }
        {
            Engine e;
            e.prepare (rate, 128);
            e.set (ParamId::glide, glideKnob (0.1f)), e.set (ParamId::morph, 0.0f);
            e.reset();
            e.set (ParamId::morph, 1.0f);
            quiet (e, second / 10);
            NEAR (e.left().x, 1.0 - std::exp (-1.0), 0.01);
        }
        {
            // Release 100 ms from a steady -6 dB: a time constant later it is down by e (8.7 dB).
            Engine e;
            e.prepare (rate, 128);
            e.set (ParamId::envFreq, 1.0f), e.set (ParamId::envRelease, releaseKnob (0.1f)), e.set (ParamId::freq, 0.0f);
            e.reset();
            quiet (e, second, 0.5f);
            const float held = e.envelope();
            quiet (e, second / 10);
            NEAR (held, 42.0 / 48.0, 2e-3);
            NEAR (e.envelope(), (20.0 * std::log10 (0.5 * std::exp (-1.0)) + 48.0) / 48.0, 0.01);
        }
        {
            // Mix from 1 to 0 on a steady input through a high pass (which passes none of it): 20 ms later the dry
            // share is 1 - 1/e.
            Engine e;
            e.prepare (rate, 128);
            e.set (ParamId::cube, 1), e.set (ParamId::morph, 0.0f), e.set (ParamId::freq, 0.5f), e.set (ParamId::transform, 0.0f);
            e.reset();
            const auto settled = quiet (e, second, 0.5f);
            CHECK (std::fabs (settled.back()) < 1e-4f);
            e.set (ParamId::mix, 0.0f);
            const auto out = quiet (e, second / 50, 0.5f);
            NEAR (out.back() / 0.5, 1.0 - std::exp (-1.0), 0.02);
        }
        {
            // The synced LFO at this rate: 1 Bar on Morph, the song at beat 1.37 and then 9.25.
            Engine e;
            e.prepare (rate, 8);
            e.set (ParamId::morph, 0.5f), e.set (ParamId::lfoSync, 4), e.set (ParamId::lfoMorph, 1.0f);
            e.reset();
            float in[8] = {}, out[8] = {};
            for (double beats : { 1.37, 9.25 })
            {
                e.transport (true, true, beats, 120.0);
                e.render (in, in, out, out, 8);
                NEAR (e.left().x, 0.5 + 0.5 * std::sin (2.0 * M_PI * beats / 4.0), 1e-5);
            }
        }
    }
}

// Review fixes, each against its failure: the right side after Spread goes to 0 and back (with a cube change
// between), a broken input sample, Drive leaving 0 dB on a loud tone, and a cube change while a resonator rings.
std::vector<float> renderSide (Engine& e, const Stereo& x, size_t from, size_t to, bool right)
{
    std::vector<float> l (to - from), r (to - from);
    e.render (x.l.data() + from, x.r.data() + from, l.data(), r.data(), (uint32_t) (to - from));
    return right ? r : l;
}

void testReviewFixes()
{
    const Stereo noise = whiteNoise (48000 * 3, 0.3f, 4);
    {
        // Spread on, off, a seven-band cube, Spread on again at the same value: the right side is that cube's.
        Engine a, b;
        for (Engine* e : { &a, &b })
            e->prepare (48000.0, 512), e->set (ParamId::glide, 0.0f);
        a.set (ParamId::spread, 0.5f);
        a.reset();
        renderSide (a, noise, 0, 4800, true);
        a.set (ParamId::spread, 0.0f);
        renderSide (a, noise, 4800, 14400, true);
        a.set (ParamId::cube, 7);
        renderSide (a, noise, 14400, 24000, true);
        a.set (ParamId::spread, 0.5f);
        const auto ra = renderSide (a, noise, 24000, 144000, true);
        b.set (ParamId::cube, 7), b.set (ParamId::spread, 0.5f);
        b.reset();
        renderSide (b, noise, 0, 24000, true);
        const auto rb = renderSide (b, noise, 24000, 144000, true);
        double worst = 0.0, level = 0.0;
        for (size_t i = 72000; i < ra.size(); ++i)
            worst = std::max (worst, (double) std::fabs (ra[i] - rb[i])), level = std::max (level, (double) std::fabs (rb[i]));
        CHECK (level > 0.01 && worst < 1e-4);
    }
    {
        // An infinity and a NaN in the input: every output stays a number, and the sound is back within a second.
        Stereo bad = noise, good = noise;
        bad.l[1000] = INFINITY, bad.r[30000] = NAN;
        good.l[1000] = 0.0f, good.r[30000] = 0.0f;
        Engine a, b;
        for (Engine* e : { &a, &b })
            e->prepare (48000.0, 512), e->set (ParamId::envFreq, 0.5f), e->set (ParamId::cube, 2), e->reset();
        std::vector<float> al (bad.l.size()), ar (bad.l.size()), bl (bad.l.size()), br (bad.l.size());
        a.render (bad.l.data(), bad.r.data(), al.data(), ar.data(), (uint32_t) al.size());
        b.render (good.l.data(), good.r.data(), bl.data(), br.data(), (uint32_t) bl.size());
        bool finite = true;
        double worst = 0.0;
        for (size_t i = 0; i < al.size(); ++i)
        {
            finite &= std::isfinite (al[i]) && std::isfinite (ar[i]);
            if (i >= 96000)
                worst = std::max ({ worst, (double) std::fabs (al[i] - bl[i]), (double) std::fabs (ar[i] - br[i]) });
        }
        CHECK (finite && worst < 1e-4);
    }
    {
        // Drive from 0 dB to a little: on a tone at -1 dBFS the output moves no faster than the tone does.
        Engine e;
        e.prepare (48000.0, 512);
        e.set (ParamId::cube, 13), e.set (ParamId::morph, 0.5f), e.set (ParamId::transform, 0.0f);
        e.reset();
        Stereo tone { std::vector<float> (24000), std::vector<float> (24000) };
        for (size_t i = 0; i < tone.l.size(); ++i)
            tone.l[i] = tone.r[i] = 0.89f * std::sin (2.0f * pi * 200.0f * (float) i / 48000.0f);
        auto before = renderSide (e, tone, 0, 12000, false);
        e.set (ParamId::drive, 0.5f);
        auto after = renderSide (e, tone, 12000, 24000, false);
        double steady = 0.0, change = 0.0;
        for (size_t i = 6001; i < before.size(); ++i)
            steady = std::max (steady, (double) std::fabs (before[i] - before[i - 1]));
        change = std::fabs (after[0] - before.back());
        for (size_t i = 1; i < 2400; ++i)
            change = std::max (change, (double) std::fabs (after[i] - after[i - 1]));
        CHECK (change < steady * 1.1);
    }
    {
        // From a ringing Bell Tower to Low To Band while the loop plays: a dip to silence, no burst above either cube.
        Engine e;
        e.prepare (48000.0, 512);
        e.set (ParamId::cube, 7), e.set (ParamId::transform, 1.0f), e.set (ParamId::emphasis, 1.0f);
        e.reset();
        const Stereo music = loop (48000.0, 2.0);
        const auto before = renderSide (e, music, 0, 48000, false);
        e.set (ParamId::cube, 0);
        const auto after = renderSide (e, music, 48000, 52800, false);
        float peakBefore = 0.0f, peakAfter = 0.0f, quietest = 1.0f;
        for (size_t i = 24000; i < before.size(); ++i)
            peakBefore = std::max (peakBefore, std::fabs (before[i]));
        for (float v : after)
            peakAfter = std::max (peakAfter, std::fabs (v));
        for (size_t i = 141; i <= 146; ++i) // the dip's bottom, 3 ms in: the loudest sample there
            quietest = i == 141 ? std::fabs (after[i]) : std::max (quietest, std::fabs (after[i]));
        // The new cube on its own, the same passage: the switch may be as loud as either cube, never louder.
        Engine fresh;
        fresh.prepare (48000.0, 512);
        fresh.set (ParamId::cube, 0), fresh.set (ParamId::transform, 1.0f), fresh.set (ParamId::emphasis, 1.0f);
        fresh.reset();
        renderSide (fresh, music, 0, 48000, false);
        float peakFresh = 0.0f;
        for (float v : renderSide (fresh, music, 48000, 52800, false))
            peakFresh = std::max (peakFresh, std::fabs (v));
        CHECK (peakAfter <= std::max (peakBefore, peakFresh) * 1.05f && quietest < 0.02f);
    }
}

// Texts at the units' edges read back to the same text (99.96 ms prints as 100 ms, which reads as 0.1 s and prints
// as 100 ms again).
void testTextEdges()
{
    struct Edge { uint32_t id; float value; };
    const Edge edges[] = {
        { ParamId::glide, glideKnob (0.09996f) }, { ParamId::glide, glideKnob (0.009996f) }, { ParamId::glide, glideKnob (0.9996f) },
        { ParamId::envAttack, attackKnob (0.0009996f) }, { ParamId::envRelease, releaseKnob (0.9996f) }, { ParamId::lfoRate, lfoKnob (0.9996f) },
    };
    for (const Edge& e : edges)
    {
        const Param& p = paramTable[paramIndex (e.id)];
        char text[32], again[32];
        formatParam (p, e.value, text, sizeof (text));
        float v = 0.0f;
        CHECK (parseParam (p, text, v));
        formatParam (p, v, again, sizeof (again));
        if (std::strcmp (text, again) != 0)
            std::printf ("  %s: \"%s\" reads back as \"%s\"\n", p.name, text, again);
        CHECK (std::strcmp (text, again) == 0);
    }
}

void testPresets()
{
    for (const auto& p : presetTable)
    {
        float v[paramCount];
        presetValuesOf (p, v);
        for (uint32_t i = 0; i < p.count; ++i)
            CHECK (v[paramIndex (p.values[i].id)] == p.values[i].value); // every listed value is in range as written
    }
}
} // namespace

int main()
{
    testCubes();
    testStable();
    testLevels();
    testResponseMaths();
    testLfo();
    testEnvelopeGlideSpread();
    testSound();
    testPage();
    testReviewFixes();
    testRates();
    testTextEdges();
    testPresets();
    std::printf ("engine: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
