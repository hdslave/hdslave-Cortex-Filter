// Cortex Filter's engine and filter maths, checked from the inside: the cubes' data, the blend between corners, every point's
// filter being stable, the response maths against known filters, the LFO (shapes, rate, in step with the song,
// stopped, jumps), the envelope follower, the glides, Spread, the point staying inside the cube, the sequencer (its
// rules, clocks, triggers and fades), the filters' settings (Null, gains, offsets, Transform > Dist, distortion), and
// the page (its point and inputs) without a host.
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
    e.set (ParamId::mix, 0.0f), e.set (ParamId::filter, 10), e.set (ParamId::drive, 12.0f), e.set (ParamId::emphasis, 1.0f);
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
    m.set (ParamId::filter, 3), m.set (ParamId::morph, 0.4f);
    m.reset();
    m.render (x.l.data(), x.l.data(), l.data(), r.data(), (uint32_t) l.size());
    bool mono = true;
    for (size_t i = 0; i < l.size(); ++i)
        mono &= l[i] == r[i];
    CHECK (mono);

    Engine loud;
    loud.prepare (48000.0, 512);
    loud.set (ParamId::filter, 13), loud.set (ParamId::transform, 1.0f), loud.set (ParamId::emphasis, 1.0f), loud.set (ParamId::output, 12.0f);
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
    Store store;
    Commands commands;
    Screen screen (values, feed, store, commands);
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
    CHECK (screen.input (hdui::Key::wheel, 3, false, edits) && edits.count == 1 && edits.list[0].id == ParamId::filter && edits.list[0].value == 4.0f);
    hdui::Edits none;
    CHECK (screen.input (hdui::Key::push, 0, false, none) && screen.morphView());
    values[paramIndex (ParamId::filter)].store (4.0f);
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
            e.set (ParamId::filter, 2), e.set (ParamId::morph, 0.0f), e.set (ParamId::freq, 0.5f), e.set (ParamId::transform, 0.0f);
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
        a.set (ParamId::filter, 8);
        renderSide (a, noise, 14400, 24000, true);
        a.set (ParamId::spread, 0.5f);
        const auto ra = renderSide (a, noise, 24000, 144000, true);
        b.set (ParamId::filter, 8), b.set (ParamId::spread, 0.5f);
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
            e->prepare (48000.0, 512), e->set (ParamId::envFreq, 0.5f), e->set (ParamId::filter, 3), e->reset();
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
        e.set (ParamId::filter, 14), e.set (ParamId::morph, 0.5f), e.set (ParamId::transform, 0.0f);
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
        e.set (ParamId::filter, 8), e.set (ParamId::transform, 1.0f), e.set (ParamId::emphasis, 1.0f);
        e.reset();
        const Stereo music = loop (48000.0, 2.0);
        const auto before = renderSide (e, music, 0, 48000, false);
        e.set (ParamId::filter, 1);
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
        fresh.set (ParamId::filter, 1), fresh.set (ParamId::transform, 1.0f), fresh.set (ParamId::emphasis, 1.0f);
        fresh.reset();
        renderSide (fresh, music, 0, 48000, false);
        float peakFresh = 0.0f;
        for (float v : renderSide (fresh, music, 48000, 52800, false))
            peakFresh = std::max (peakFresh, std::fabs (v));
        CHECK (peakAfter <= std::max (peakBefore, peakFresh) * 1.05f && quietest < 0.02f);
    }
}

// The sequencer's rules, as the module's manual has them: Inc and Dec move circularly and load the filter there;
// Pause and Halt stop them (a Reset ends a Pause without moving; out of a Halt it goes to the first step); a Bumper
// is a wall, and at the first step a Pause already ended; Fade scales what follows; the wheel skips the commands.
void testSequencer()
{
    Store st;
    auto program = [&] (int q, std::initializer_list<int> steps) {
        uint8_t list[maxSteps] = {};
        int n = 0;
        for (int code : steps)
            list[n++] = (uint8_t) code;
        st.setSequence (q, list, n);
    };
    Sequencer s;
    program (9, { 1, 2, stepPause, 3, stepHalt, 4 });
    s.load (st, 9);
    CHECK (s.on (st) && s.pos == 0 && s.filter == 1 && s.mode == Sequencer::Mode::normal);
    s.inc (st);
    CHECK (s.pos == 1 && s.filter == 2);
    s.inc (st);
    CHECK (s.pos == 2 && s.filter == 2 && s.mode == Sequencer::Mode::paused);
    s.inc (st), s.dec (st);
    CHECK (s.pos == 2 && s.mode == Sequencer::Mode::paused); // a Pause holds against Inc and Dec
    s.reset (st);
    CHECK (s.pos == 2 && s.mode == Sequencer::Mode::normal); // ended, nothing moved
    s.inc (st);
    CHECK (s.pos == 3 && s.filter == 3);
    s.inc (st);
    CHECK (s.pos == 4 && s.mode == Sequencer::Mode::halted);
    s.inc (st);
    CHECK (s.pos == 4);
    s.reset (st);
    CHECK (s.pos == 0 && s.filter == 1 && s.mode == Sequencer::Mode::normal); // out of a Halt: the first step
    s.dec (st);
    CHECK (s.pos == 5 && s.filter == 4); // round the end
    s.inc (st);
    CHECK (s.pos == 0 && s.filter == 1);

    program (10, { stepBumper, 1, 2, stepBumper });
    s.load (st, 10);
    CHECK (s.pos == 0 && s.filter == 0 && s.mode == Sequencer::Mode::normal); // a Bumper first: a Pause already ended
    s.inc (st), s.inc (st);
    CHECK (s.pos == 2 && s.filter == 2);
    s.inc (st);
    CHECK (s.pos == 2); // the wall
    s.dec (st), s.dec (st);
    CHECK (s.pos == 1 && s.filter == 1);

    program (11, { stepFade + 3, 1, stepFade + 8, 2 });
    s.load (st, 11);
    CHECK (s.fade == 3 && s.filter == 0);
    s.inc (st);
    CHECK (s.fade == 3 && s.filter == 1);
    s.inc (st), s.inc (st);
    CHECK (s.fade == 8 && s.filter == 2 && fadeScales[s.fade] == 8.0f);

    program (12, { 1, stepPause, 2, stepHalt, 3 });
    s.load (st, 12);
    s.browse (st, 1);
    CHECK (s.pos == 2 && s.filter == 2 && s.mode == Sequencer::Mode::normal); // past the Pause, which does nothing
    s.browse (st, 2);
    CHECK (s.pos == 0 && s.filter == 1);
    s.browse (st, -1);
    CHECK (s.pos == 4 && s.filter == 3);

    program (13, { stepHalt, 1 });
    s.load (st, 13);
    CHECK (s.mode == Sequencer::Mode::halted);
    s.inc (st);
    CHECK (s.pos == 0);
    s.reset (st);
    CHECK (s.pos == 0 && s.mode == Sequencer::Mode::halted); // a Halt first: it never gets further
    s.load (st, 14);
    CHECK (! s.on (st)); // empty
    s.inc (st), s.reset (st);
    CHECK (s.pos == 0 && s.filter == 0);
    // The factory's sequences are all filters in use or commands.
    for (int q = 1; q <= factorySequenceCount; ++q)
        for (int i = 0; i < st.length (q); ++i)
        {
            const int code = st.step (q, i);
            CHECK ((isFilterStep (code) && st.used (code)) || code == stepPause || code == stepHalt || code == stepBumper || isFadeStep (code));
        }
    // Steps put in, taken out, added.
    program (15, { 1, 2, 3 });
    CHECK (st.insertStep (15, 1, stepPause) && st.length (15) == 4 && st.step (15, 1) == stepPause && st.step (15, 3) == 3);
    CHECK (st.deleteStep (15, 0) && st.length (15) == 3 && st.step (15, 0) == stepPause && st.step (15, 3) == 0);
    for (int i = 0; i < maxSteps; ++i)
        st.insertStep (15, st.length (15), 1);
    CHECK (st.length (15) == maxSteps && ! st.insertStep (15, 0, 1));
}

// An engine in 8-frame blocks with the song playing at 120 bpm from beat 0 (a beat is 24000 frames at 48 kHz).
struct Song
{
    Engine e;
    std::vector<float> in[2], out[2];
    uint64_t at = 0;
    bool playing = true;
    explicit Song (double rate = 48000.0)
    {
        e.prepare (rate, 8);
        for (auto* v : { &in[0], &in[1], &out[0], &out[1] })
            v->assign (8, 0.0f);
    }
    void level (float v)
    {
        for (int c = 0; c < 2; ++c)
            std::fill (in[c].begin(), in[c].end(), v);
    }
    // `frames` more, the output's left side kept if `keep`.
    void run (uint64_t frames, std::vector<float>* keep = nullptr)
    {
        for (uint64_t f = 0; f < frames; f += 8, at += 8)
        {
            e.transport (playing, true, (double) at / 24000.0, 120.0);
            e.render (in[0].data(), in[1].data(), out[0].data(), out[1].data(), 8);
            if (keep != nullptr)
                keep->insert (keep->end(), out[0].begin(), out[0].end());
        }
    }
};

void program (Store& st, int q, std::initializer_list<int> steps)
{
    uint8_t list[maxSteps] = {};
    int n = 0;
    for (int code : steps)
        list[n++] = (uint8_t) code;
    st.setSequence (q, list, n);
}

// A Null filter in a spare slot, its fades as given.
void nullFilter (Store& st, int slot, float enter, float exit)
{
    st.setField (slot, fieldCube, 0.0f);
    st.setField (slot, fieldEnter, fadeKnob (enter));
    st.setField (slot, fieldExit, fadeKnob (exit));
}

void testSequencerEngine()
{
    // Inc Clock 1/4: a step each beat, none at the start; Reset Clock 1 Bar: back to the first step on each bar,
    // even with an Inc on the same beat.
    {
        Song s;
        program (s.e.bank(), 9, { 1, 2, 3, 4, 5, 6 });
        s.e.set (ParamId::sequence, 9), s.e.set (ParamId::incClock, 7), s.e.set (ParamId::resetClock, 1);
        s.e.reset();
        s.run (23992);
        CHECK (s.e.sequencer().pos == 0 && s.e.slotNow() == 1);
        s.run (16);
        CHECK (s.e.sequencer().pos == 1);
        s.run (400); // past the dip: 3 ms out, 3 ms in
        CHECK (s.e.slotNow() == 2);
        s.run (24000 * 2);
        CHECK (s.e.sequencer().pos == 3);
        s.run (24000);
        CHECK (s.e.sequencer().pos == 0); // beat 4: Inc, then Reset
        // Stopped, the clocks stop; started again, the sequence starts again.
        s.run (24000);
        CHECK (s.e.sequencer().pos == 1);
        s.playing = false;
        s.run (48000);
        CHECK (s.e.sequencer().pos == 1);
        s.playing = true;
        s.run (8);
        CHECK (s.e.sequencer().pos == 0);
    }
    // Dec Clock walks back; the gates fire on their rising edge only; the page's triggers and wheel.
    {
        Song s;
        program (s.e.bank(), 9, { 1, 2, 3, 4 });
        s.e.set (ParamId::sequence, 9), s.e.set (ParamId::incClock, 0), s.e.set (ParamId::decClock, 4); // a bar
        s.e.reset();
        s.run (96000 + 8);
        CHECK (s.e.sequencer().pos == 3);
        s.e.set (ParamId::trigInc, 1.0f), s.e.set (ParamId::trigInc, 1.0f);
        s.run (8);
        CHECK (s.e.sequencer().pos == 0);
        s.e.set (ParamId::trigInc, 0.0f), s.e.set (ParamId::trigInc, 1.0f), s.e.set (ParamId::trigDec, 1.0f);
        s.run (8);
        CHECK (s.e.sequencer().pos == 0); // Inc then Dec
        s.e.trigger (0, 2), s.e.browse (-1);
        s.run (8);
        CHECK (s.e.sequencer().pos == 1);
        const uint32_t fired = s.e.triggerCount();
        s.e.set (ParamId::trigReset, 1.0f);
        s.run (8);
        CHECK (s.e.sequencer().pos == 0 && s.e.triggerCount() == fired + 1);
    }
    // The envelope as a trigger: one Inc for each burst over Trig Level, none while it stays over it.
    {
        Song s;
        s.playing = false;
        program (s.e.bank(), 9, { 1, 2, 3, 4, 5 });
        s.e.set (ParamId::sequence, 9), s.e.set (ParamId::envTrigger, 1), s.e.set (ParamId::trigLevel, -20.0f);
        s.e.set (ParamId::envAttack, attackKnob (0.001f)), s.e.set (ParamId::envRelease, releaseKnob (0.02f));
        s.e.reset();
        for (int burst = 0; burst < 3; ++burst)
        {
            s.level (0.5f);
            s.run (4800);
            s.level (0.0f);
            s.run (9600);
        }
        CHECK (s.e.sequencer().pos == 3);
        s.e.set (ParamId::envTrigger, 3); // now Reset
        s.level (0.5f);
        s.run (4800);
        CHECK (s.e.sequencer().pos == 0);
    }
    // Fades: from one Null filter to another, the sound dips over the first's Exit Fade and comes back over the
    // second's Enter Fade, scaled by a Fade step.
    for (int scaled = 0; scaled < 2; ++scaled)
    {
        Song s;
        s.playing = false;
        Store& st = s.e.bank();
        nullFilter (st, 40, 0.01f, 0.05f), nullFilter (st, 41, 0.1f, 0.01f);
        program (st, 9, { stepFade + (scaled ? 6 : 5), 40, 41 });
        s.e.set (ParamId::sequence, 9), s.e.set (ParamId::filter, 40);
        s.e.reset();
        s.level (0.5f);
        s.run (4800);
        std::vector<float> out;
        s.e.trigger (0); // to filter 40: no change
        s.e.trigger (0); // the next run's first step: to 41
        s.run (8);
        s.run (48000, &out);
        const double k = scaled ? 2.0 : 1.0, exit = 0.05 * 48000 * k, enter = 0.1 * 48000 * k;
        NEAR (out[(size_t) (exit / 2)], 0.25, 0.01);
        NEAR (out[(size_t) (exit + enter / 2)], 0.25, 0.01);
        CHECK (out[(size_t) (exit + enter + 100)] == 0.5f);
        CHECK (s.e.slotNow() == 41);
    }
}

// The clocks through a gap in the blocks while the song plays on (the host lets the sound sleep after half a second of
// silence and wakes it when sound comes back): after it the sequence is where it would be had it run throughout, for
// the presets' clocks and others, gaps short and long, mid-cell or across Resets, past maxReplay boundaries and far
// past it. A loop one Reset long starts the sequence again each time round; a jump back fires a clock only when it
// lands on one of its boundaries.
void testClockGaps()
{
    struct Clocks
    {
        const char* name;
        int sequence, inc, dec, reset;
    };
    const Clocks list[] = {
        { "vowel walk", 6, 9, 0, 2 },       { "wander", 8, 7, 8, 0 },          { "acid sequence", 5, 11, 0, 1 },
        { "bar gate", 7, 9, 0, 1 },         { "a pause", 9, 7, 0, 1 },         { "1/32 and 1/16T", 10, 13, 12, 0 },
        { "1/32 alone", 10, 13, 0, 0 },     { "1/32, a Bumper, 16 bars", 11, 13, 0, 5 }, { "both, a Pause, 4 bars", 9, 11, 10, 3 },
    };
    const uint64_t beat = 24000; // at 120 bpm and 48 kHz
    const uint64_t gaps[] = { 8 * 3001, beat * 4, beat * 4 + 8 * 1001, beat * 13 + 8 * 777, beat * 70 + 8 * 5, beat * 163 + 8 * 2000 };
    auto programs = [] (Store& st) {
        program (st, 9, { 1, 2, stepPause, 3, 4 });
        program (st, 10, { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 });
        program (st, 11, { stepFade + 4, 1, 2, stepBumper, 3, 4, 5 });
    };
    int shown = 0;
    for (const Clocks& k : list)
        for (uint64_t start : { beat * 6, beat * 9 + 8 * 1234 })
            for (uint64_t gap : gaps)
            {
                Song a, b;
                for (Song* s : { &a, &b })
                {
                    programs (s->e.bank());
                    s->e.set (ParamId::sequence, (float) k.sequence), s->e.set (ParamId::incClock, (float) k.inc);
                    s->e.set (ParamId::decClock, (float) k.dec), s->e.set (ParamId::resetClock, (float) k.reset);
                    s->e.reset();
                    s->run (start);
                }
                a.run (gap);
                b.at += gap; // the song plays on, the sound does not run
                a.run (8), b.run (8);
                const Sequencer &x = a.e.sequencer(), &y = b.e.sequencer();
                const bool same = x.pos == y.pos && x.mode == y.mode && x.fade == y.fade && x.filter == y.filter;
                if (! same && shown++ < 6)
                    std::printf ("  %s, gap %.3f beats from %.3f: step %d (%d) against %d (%d) run throughout\n", k.name, (double) gap / beat,
                                 (double) start / beat, y.pos + 1, (int) y.mode, x.pos + 1, (int) x.mode);
                CHECK (same);
            }
    // Far past maxReplay (40,000 bars of 1/32s): as many steps on as the clock passed, in a bounded replay.
    {
        Song s;
        programs (s.e.bank());
        s.e.set (ParamId::sequence, 10), s.e.set (ParamId::incClock, 13);
        s.e.reset();
        s.run (beat * 3 + 8 * 11);
        const int before = s.e.sequencer().pos;
        const int64_t lastStep = (int64_t) s.at - 8, gap = (int64_t) beat * 160000 + 8 * 4321;
        s.at += (uint64_t) gap;
        s.run (8);
        const int64_t passed = ((int64_t) s.at - 8) / 3000 - lastStep / 3000; // a 1/32 is 3000 frames
        CHECK (s.e.sequencer().pos == (int) ((before + passed) % 11));
    }
    // A loop one Reset long (the song back at the bar's start each time round): the Reset each time.
    {
        Song s;
        programs (s.e.bank());
        s.e.set (ParamId::sequence, 10), s.e.set (ParamId::incClock, 7), s.e.set (ParamId::resetClock, 1);
        s.e.reset();
        for (int pass = 0; pass < 3; ++pass)
        {
            s.at = 0;
            s.run (8);
            CHECK (s.e.sequencer().pos == 0);
            s.run (beat * 4 - 8);
            CHECK (s.e.sequencer().pos == 3);
        }
    }
    // A jump back into the middle of a beat fires no Inc; onto a beat it does, the same cell or another.
    {
        Song s;
        programs (s.e.bank());
        s.e.set (ParamId::sequence, 10), s.e.set (ParamId::incClock, 7);
        s.e.reset();
        s.run (beat * 3 + beat / 2);
        CHECK (s.e.sequencer().pos == 3);
        s.at = beat + beat / 2;
        s.run (8);
        CHECK (s.e.sequencer().pos == 3);
        s.at = beat;
        s.run (beat / 4);
        CHECK (s.e.sequencer().pos == 4);
        s.at = beat;
        s.run (8);
        CHECK (s.e.sequencer().pos == 5);
        s.at = 8 * 5; // back to just after a beat
        s.run (8);
        CHECK (s.e.sequencer().pos == 5);
    }
}

void testFilterSettings()
{
    const Stereo x = loop (48000.0, 1.0);
    std::vector<float> l (x.l.size()), r (x.l.size());
    // Null (F32, Bypass): the input to the bit.
    {
        Engine e;
        e.prepare (48000.0, 512);
        e.set (ParamId::filter, 32);
        e.reset();
        e.render (x.l.data(), x.r.data(), l.data(), r.data(), (uint32_t) l.size());
        CHECK (l == x.l && r == x.r);
    }
    // Each side's gain: -6 dB left, +6 dB right on a Null filter.
    {
        Engine e;
        e.prepare (48000.0, 512);
        e.bank().setField (40, fieldCube, 0.0f), e.bank().setField (40, fieldGainLeft, -6.0f), e.bank().setField (40, fieldGainRight, 6.0f);
        e.set (ParamId::filter, 40);
        e.reset();
        std::vector<float> in (4800, 0.1f);
        e.render (in.data(), in.data(), l.data(), r.data(), 4800);
        NEAR (l[4000], 0.1 * std::pow (10.0, -6.0 / 20.0), 1e-6);
        NEAR (r[4000], 0.1 * std::pow (10.0, 6.0 / 20.0), 1e-6);
    }
    // Offsets and gains move and scale the point around the middle; the point stays inside the cube.
    {
        Rig rig;
        Store& st = rig.e.bank();
        st.setField (1, fieldFreqOffset, 40.0f), st.setField (1, fieldMorphGain, -10.0f), st.setField (1, fieldTransformGain, 20.0f);
        rig.e.set (ParamId::glide, 0.0f), rig.e.set (ParamId::morph, 0.2f), rig.e.set (ParamId::freq, 0.6f), rig.e.set (ParamId::transform, 0.7f);
        rig.e.reset();
        rig.block (false, false, 0.0);
        NEAR (rig.e.left().x, 0.8, 1e-6);
        NEAR (rig.e.left().y, 0.8, 1e-6);
        NEAR (rig.e.left().z, 0.9, 1e-6);
        rig.e.set (ParamId::transform, 1.0f);
        for (int i = 0; i < 100; ++i)
            rig.block (false, false, 0.0);
        CHECK (rig.e.left().z == 1.0f);
        NEAR (rig.e.control (2), 1.0, 1e-6);
    }
    // Transform > Dist: Transform sits at its offset; the knob drives the distortion. At its bottom (-30 dB and
    // more under it) there is none: the same sound as the filter with Transform at the offset.
    {
        const Stereo tone = [] {
            Stereo t { std::vector<float> (24000), std::vector<float> (24000) };
            for (size_t i = 0; i < t.l.size(); ++i)
                t.l[i] = t.r[i] = 0.5f * std::sin (2.0f * pi * 220.0f * (float) i / 48000.0f);
            return t;
        }();
        auto render = [&] (bool drives, float knob) {
            Engine e;
            e.prepare (48000.0, 512);
            e.bank().setField (10, fieldTransformDist, drives ? 1.0f : 0.0f), e.bank().setField (10, fieldTransformOffset, drives ? 40.0f : 0.0f);
            e.set (ParamId::filter, 10), e.set (ParamId::glide, 0.0f), e.set (ParamId::transform, drives ? knob : 0.7f), e.set (ParamId::morph, 0.3f);
            e.reset();
            std::vector<float> a (tone.l.size()), b (tone.l.size());
            e.render (tone.l.data(), tone.r.data(), a.data(), b.data(), (uint32_t) a.size());
            CHECK (std::fabs (e.left().z - 0.7f) < 1e-6f);
            return a;
        };
        const auto plain = render (false, 0.0f), clean = render (true, 0.0f), driven = render (true, 1.0f);
        CHECK (clean == plain);
        double differ = 0.0, peak = 0.0;
        for (size_t i = 0; i < driven.size(); ++i)
            differ = std::max (differ, (double) std::fabs (driven[i] - plain[i])), peak = std::max (peak, (double) std::fabs (driven[i]));
        CHECK (differ > 0.01 && peak <= 2.0);
    }
    // Distortion on one side only: the other side is as it was, to the bit; the distorted side stays finite and
    // within the limit on loud noise, and its meter shows it bending.
    {
        const Stereo n = whiteNoise (48000, 1.0f, 8);
        auto render = [&] (float left, float right, float* meters) {
            Engine e;
            e.prepare (48000.0, 512);
            e.bank().setField (8, fieldDistLeft, left), e.bank().setField (8, fieldDistRight, right);
            e.set (ParamId::filter, 8), e.set (ParamId::transform, 1.0f);
            e.reset();
            Stereo o { std::vector<float> (n.l.size()), std::vector<float> (n.l.size()) };
            e.render (n.l.data(), n.r.data(), o.l.data(), o.r.data(), (uint32_t) o.l.size());
            e.takeBend (meters);
            return o;
        };
        float m[2], quiet[2];
        const Stereo a = render (40.0f, -30.0f, m), b = render (-30.0f, -30.0f, quiet);
        CHECK (a.r == b.r && a.l != b.l);
        bool ok = true;
        for (float v : a.l)
            ok &= std::isfinite (v) && std::fabs (v) <= 2.0f;
        CHECK (ok && m[0] > 1.0f && m[1] == 0.0f && quiet[0] == 0.0f);
    }
    // An empty filter is heard as Null; every factory filter is in use, and the rest of the bank empty.
    {
        Store st;
        for (int s = 1; s <= slotCount; ++s)
            CHECK (st.used (s) == (s <= factoryFilterCount));
        CHECK (st.cubeOf (50) == -1 && std::strcmp (st.name (50), "--empty--") == 0 && std::strcmp (st.name (7), "Wah Talk") == 0);
        CHECK (std::strcmp (st.name (17), "Deep Acid") == 0 && st.cubeOf (32) == -1 && st.used (32));
        st.copy (17, 50);
        CHECK (st.used (50) && std::strcmp (st.name (50), "Deep Acid") == 0 && st.field (50, fieldFreqOffset) == -30.0f);
        CHECK (st.emptyAfter (49) == 51 && st.emptyAfter (64) == 33 && st.usedFrom (32, 1) == 50 && st.usedFrom (50, -1) == 32);
        st.factorySlot (50);
        CHECK (! st.used (50));
        // 0.1.0's dip for a cube change: 3 ms, exactly.
        CHECK (fadeSeconds (fadeDefault) == 0.003f && st.field (1, fieldEnter) == fadeDefault);
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
        { ParamId::enterFade, fadeKnob (0.09996f) }, { ParamId::exitFade, fadeKnob (0.9996f) }, { ParamId::enterFade, fadeDefault },
        { ParamId::morphGain, -99.0f }, { ParamId::freqOffset, -100.0f }, { ParamId::distLeft, 70.0f },
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
    testSequencer();
    testSequencerEngine();
    testClockGaps();
    testFilterSettings();
    testTextEdges();
    testPresets();
    std::printf ("engine: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
