// Cortex Filter's sound: input -> Drive (a soft saturator) -> the morphing filter (morph.h) at the point the knobs, the
// LFO and the envelope follower pick -> Mix with the input -> Output -> a soft limit above full scale.
//
// The point moves at the control rate, every `controlFrames` frames counted from activation, whatever the host's
// blocks: the same input and events give the same output in any block size. Gains (Drive, Mix, Output) glide every
// sample. Each channel has its own filter state; Spread gives each its own point.
#pragma once

#include "morph.h"
#include "params.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cortex
{
#ifndef CORTEX_CONTROL_FRAMES
#define CORTEX_CONTROL_FRAMES 8 // tools/artifacts.sh builds it at 1 too, to measure what the steps cost
#endif
constexpr uint32_t controlFrames = CORTEX_CONTROL_FRAMES; // the point and the filter are worked out this often

// The left and right channels side by side, as one 2-lane vector (a NEON register on the MPC): each band runs both
// sides at once.
typedef float Pair __attribute__ ((vector_size (8)));
struct PairCoefs
{
    Pair a1, a2, a3, m0, m1, m2; // morph.h's BandCoefs, left and right
};
struct PairState
{
    Pair ic1, ic2;
};

// A value from 32 bits: the same for the same cycle (S&H and Drift follow the song's place).
inline float hashUnit (int64_t n, uint32_t axis)
{
    uint64_t x = (uint64_t) n * 0x9E3779B97F4A7C15ull + (uint64_t) (axis + 1) * 0xD1B54A32D192ED03ull;
    x ^= x >> 33, x *= 0xff51afd7ed558ccdull, x ^= x >> 33, x *= 0xc4ceb9fe1a85ec53ull, x ^= x >> 33;
    return (float) (x >> 40) * (2.0f / 16777216.0f) - 1.0f;
}

// The LFO's value (-1..1) `cycles` cycles from its start, for axis 0-2 (the random shapes draw per axis).
inline float lfoValue (Shape shape, double cycles, uint32_t axis)
{
    const double whole = std::floor (cycles);
    const float u = (float) (cycles - whole);
    const int64_t n = (int64_t) whole;
    switch (shape)
    {
        case Shape::sine: return std::sin (2.0f * pi * u);
        case Shape::triangle: return u < 0.25f ? 4.0f * u : (u < 0.75f ? 2.0f - 4.0f * u : 4.0f * u - 4.0f);
        case Shape::sawUp: return 2.0f * u - 1.0f;
        case Shape::sawDown: return 1.0f - 2.0f * u;
        case Shape::square: return u < 0.5f ? 1.0f : -1.0f;
        case Shape::sampleHold: return hashUnit (n, axis);
        case Shape::drift:
        {
            const float a = hashUnit (n, axis), b = hashUnit (n + 1, axis), s = u * u * (3.0f - 2.0f * u);
            return a + (b - a) * s;
        }
    }
    return 0.0f;
}

// Drive's saturator: a rational tanh, exact 1 from |u| = 3.
__attribute__ ((always_inline)) inline float saturate (float u)
{
    if (u >= 3.0f) return 1.0f;
    if (u <= -3.0f) return -1.0f;
    const float u2 = u * u;
    return u * (27.0f + u2) / (27.0f + 9.0f * u2);
}

// The output's limit: nothing below full scale, then a soft knee to at most 2. Not a number (a broken input) is
// silence, an infinity the ceiling.
__attribute__ ((always_inline)) inline float limit (float y)
{
    const float a = std::fabs (y);
    if (a <= 1.0f) return y;
    if (! (a < 1e30f))
        return y != y ? 0.0f : (y < 0.0f ? -2.0f : 2.0f);
    const float e = a - 1.0f, out = 1.0f + e / (1.0f + e);
    return y < 0.0f ? -out : out;
}

class Engine
{
public:
    void prepare (double sampleRate, uint32_t maxFrames)
    {
        (void) maxFrames; // the engine runs in control steps of its own, whatever the host's blocks
        rate = (float) sampleRate;
        derive();
        reset();
    }

    // Back to rest: filters emptied, every glide at its value, the LFO at its start.
    void reset()
    {
        for (auto& s : state)
            s = PairState {};
        for (int a = 0; a < 3; ++a)
            knob[a] = target[a];
        emphasisNow = emphasisTarget;
        mixNow = mixTarget, outNow = outTarget, driveNow = driveTarget, compNow = compTarget;
        spreadNow = spreadAmount, envGainNow = envGainLin;
        for (int a = 0; a < 3; ++a)
            lfoDepthNow[a] = lfoDepth[a], envDepthNow[a] = envDepth[a];
        cubeIndex = cubeTarget, switchGain = 1.0f, switchDir = 0;
        env = 0.0f, envNorm = 0.0f;
        lfoCycles = 0.0;
        untilTick = 0;
        designed = false;
        songPlaying = false;
    }

    // A state loaded while running (a project opening): every glide lands on its value at once, so the sound starts
    // as it was saved; the filters keep what they hold, and a different cube comes in at once with its filter empty.
    // (A preset, by contrast, glides in.)
    void settle()
    {
        for (int a = 0; a < 3; ++a)
            knob[a] = target[a], lfoDepthNow[a] = lfoDepth[a], envDepthNow[a] = envDepth[a];
        emphasisNow = emphasisTarget, spreadNow = spreadAmount, envGainNow = envGainLin;
        mixNow = mixTarget, outNow = outTarget, driveNow = driveTarget, compNow = compTarget;
        if (cubeTarget != cubeIndex)
        {
            cubeIndex = cubeTarget;
            for (auto& s : state)
                s = PairState {};
        }
        switchGain = 1.0f, switchDir = 0;
    }

    void set (uint32_t id, float v)
    {
        using namespace ParamId;
        switch (id)
        {
            case cube: cubeTarget = std::clamp ((int) v, 0, cubeCount - 1); break;
            case morph: target[0] = v; break;
            case freq: target[1] = v; break;
            case transform: target[2] = v; break;
            case emphasis: emphasisTarget = v; break;
            case glide: glideKnob = v; break;
            case drive: driveDb = v; break;
            case mix: mixTarget = v; break;
            case output: outputDb = v; break;
            case spread: spreadAmount = v; break;
            case lfoSync: syncIndex = std::clamp ((int) v, 0, (int) (sizeof (lfoSyncBeats) / sizeof (lfoSyncBeats[0])) - 1); break;
            case lfoRate: lfoRateKnob = v; break;
            case lfoShape: shape = (Shape) std::clamp ((int) v, 0, 6); break;
            case lfoPhase: phaseOffset = v; break;
            case lfoMorph: lfoDepth[0] = v; break;
            case lfoFreq: lfoDepth[1] = v; break;
            case lfoTransform: lfoDepth[2] = v; break;
            case envGain: envGainDb = v; break;
            case envAttack: attackKnob = v; break;
            case envRelease: releaseKnob = v; break;
            case envMorph: envDepth[0] = v; break;
            case envFreq: envDepth[1] = v; break;
            case envTransform: envDepth[2] = v; break;
            default: return;
        }
        derive();
    }

    // The host's transport at the block's start: whether it plays, the song's place in beats (if it gives one) and
    // the tempo. A synced LFO follows the place while the song plays, and runs on at the tempo while it does not.
    // The place is counted on in frames from where the song started (or jumped, or changed tempo), not read again
    // each block: a host's place is rounded (CLAP's to 2^-31 beats), and reading it each block would make the LFO's
    // last bits depend on where the blocks fall.
    void transport (bool playing, bool placed, double beats, double bpm)
    {
        const double perFrame = bpm / 60.0 / (double) rate;
        const bool now = playing && placed;
        if (now && (! songPlaying || perFrame != beatsPerFrame || std::fabs (beats - songBeatsAt (frame)) > 1e-6))
            anchorBeats = beats, anchorFrame = frame; // started, jumped or a new tempo
        songPlaying = now;
        beatsPerFrame = perFrame;
    }

    // `n` frames of the input (either side may be null: silence) to the output (outR may be null). In and out may be
    // the same buffers.
    void render (const float* inL, const float* inR, float* outL, float* outR, uint32_t n)
    {
        if (inL == nullptr)
            inL = inR;
        while (n > 0)
        {
            if (untilTick == 0)
                tick();
            const uint32_t run = std::min (n, untilTick);
            const float* l = inL != nullptr ? inL : silence; // no input: a control step of silence
            const float* r = inR != nullptr ? inR : l;
            if (driving)
                runBands<true> (l, r, outL, outR, run);
            else
                runBands<false> (l, r, outL, outR, run);
            if (inL != nullptr) inL += run;
            if (inR != nullptr) inR += run;
            outL += run;
            if (outR != nullptr) outR += run;
            n -= run;
            untilTick -= run;
            frame += run;
        }
    }

    // For the screen: where each channel's filter is, the envelope (0..1) and the LFO's place (in cycles).
    Position left() const { return pointL; }
    Position right() const { return pointR; }
    bool split() const { return splitNow; }
    float envelope() const { return envNorm; }
    double lfoPlace() const { return lfoCycles; }
    float sampleRate() const { return rate; }

private:
    float rate = 48000.0f;
    const float silence[controlFrames] {};

    // The knobs.
    int cubeTarget = 0;
    float target[3] = { 0.0f, 0.6f, 0.3f }, emphasisTarget = 0.0f, glideKnob = 0.4f;
    float driveDb = 0.0f, mixTarget = 1.0f, outputDb = 0.0f, spreadAmount = 0.0f;
    int syncIndex = 0;
    float lfoRateKnob = 0.4f, phaseOffset = 0.25f, lfoDepth[3] = {}, envGainDb = 0.0f, attackKnob = 0.5f, releaseKnob = 0.5f, envDepth[3] = {};
    Shape shape = Shape::sine;

    // What they come to.
    float glideCoef = 1.0f, smoothCoef = 1.0f, tickSmooth = 1.0f, lfoStep = 0.0f;
    float driveTarget = 1.0f, compTarget = 1.0f, outTarget = 1.0f, envGainLin = 1.0f, attackCoef = 1.0f, releaseCoef = 1.0f;

    // The running state. A new cube comes in under a dip of the filtered sound: down in 3 ms (switchDir -1), the cube
    // changed and its filter emptied, up in 3 ms (+1).
    int cubeIndex = 0, switchDir = 0;
    float switchGain = 1.0f, switchStep = 1.0f;
    float spreadNow = 0.0f, lfoDepthNow[3] = {}, envDepthNow[3] = {}, envGainNow = 1.0f; // glided as Emphasis is
    float knob[3] = { 0.0f, 0.6f, 0.3f }, emphasisNow = 0.0f;
    float mixNow = 1.0f, outNow = 1.0f, driveNow = 1.0f, compNow = 1.0f;
    float env = 0.0f, envNorm = 0.0f;
    double lfoCycles = 0.0;
    bool songPlaying = false;
    double anchorBeats = 0.0, beatsPerFrame = 120.0 / 60.0 / 48000.0;
    uint64_t frame = 0, anchorFrame = 0; // frames rendered since activation; the one the song's place was taken at
    uint32_t untilTick = 0;
    double songBeatsAt (uint64_t at) const { return anchorBeats + (double) (at - anchorFrame) * beatsPerFrame; }
    bool driving = false, splitNow = false, designed = false;
    Position pointL, pointR, designedL, designedR;
    int designedCube = -1;
    float designedEmphasis = 0.0f;
    Design designL, designR;
    int bandsNow = 0;
    bool pairedSplit = false; // pairs[] hold two designs (Spread) or one twice
    PairCoefs pairs[maxBands] {};
    Pair wetPair {}, dryPair {};
    PairState state[maxBands] {};

    static float onePole (float seconds, float frames, float rate) { return seconds <= 0.0f ? 1.0f : 1.0f - std::exp (-frames / (seconds * rate)); }

    void derive()
    {
        glideCoef = onePole (glideSeconds (glideKnob), (float) controlFrames, rate);
        smoothCoef = onePole (0.02f, 1.0f, rate);                   // Drive, Mix, Output: 20 ms
        tickSmooth = onePole (0.02f, (float) controlFrames, rate);  // Emphasis
        lfoStep = lfoHz (lfoRateKnob) * (float) controlFrames / rate;
        driveTarget = std::exp2 (driveDb * dbToLog2);
        compTarget = 1.0f / std::sqrt (driveTarget);
        outTarget = std::exp2 (outputDb * dbToLog2);
        envGainLin = std::exp2 (envGainDb * dbToLog2);
        attackCoef = onePole (attackSeconds (attackKnob), 1.0f, rate);
        switchStep = 1.0f / (0.003f * rate);
        releaseCoef = onePole (releaseSeconds (releaseKnob), 1.0f, rate);
    }

    // A glide's step; it lands on its value once close (or once a step is too small to move it in a float).
    static float glideTo (float now, float to, float coef)
    {
        const float next = now + (to - now) * coef;
        return next == now || std::fabs (to - next) < 1e-5f ? to : next;
    }

    // The control step: the LFO, the envelope, the glides, the point each channel is at and its filter.
    void tick()
    {
        untilTick = controlFrames;
        // The LFO's place now: the song's while it plays (synced), else counted on.
        const double beatsLong = lfoSyncBeats[syncIndex];
        if (beatsLong > 0.0 && songPlaying)
            lfoCycles = songBeatsAt (frame) / beatsLong;
        // The depths, Spread and Env Gain glide (a knob turned or a preset loaded moves the point smoothly).
        for (int a = 0; a < 3; ++a)
            lfoDepthNow[a] = glideTo (lfoDepthNow[a], lfoDepth[a], tickSmooth), envDepthNow[a] = glideTo (envDepthNow[a], envDepth[a], tickSmooth);
        spreadNow = glideTo (spreadNow, spreadAmount, tickSmooth);
        envGainNow = glideTo (envGainNow, envGainLin, tickSmooth);
        float lfo[3] = {};
        for (uint32_t a = 0; a < 3; ++a)
            if (lfoDepthNow[a] != 0.0f) // an axis the LFO does not move needs no value
                lfo[a] = lfoValue (shape, lfoCycles + (double) (phaseOffset * (float) a), a);
        lfoCycles += beatsLong > 0.0 ? (double) controlFrames * beatsPerFrame / beatsLong : (double) lfoStep;
        if (lfoCycles > 1e9) // a free LFO left running for years: keep the double's precision
            lfoCycles -= std::floor (lfoCycles);

        // The envelope, from -48 dB to 0 dB (read only if it moves something). A broken input (not a number, infinite)
        // has it start again from nothing.
        if (! (env < 1e30f))
            env = 0.0f;
        envNorm = envDepthNow[0] != 0.0f || envDepthNow[1] != 0.0f || envDepthNow[2] != 0.0f
                    ? std::clamp ((6.0206f * std::log2 (env + 1e-12f) - envFloorDb) / -envFloorDb, 0.0f, 1.0f)
                    : 0.0f;

        // The knobs' glides, then the point.
        Position p;
        float* axes[3] = { &p.x, &p.y, &p.z };
        for (int a = 0; a < 3; ++a)
        {
            knob[a] = glideTo (knob[a], target[a], glideCoef);
            *axes[a] = std::clamp (knob[a] + lfoSwing * lfoDepthNow[a] * lfo[a] + envSwing * envDepthNow[a] * envNorm, 0.0f, 1.0f);
        }
        emphasisNow = glideTo (emphasisNow, emphasisTarget, tickSmooth);
        Position l = p, r = p;
        splitNow = spreadNow > 0.0f;
        if (splitNow)
        {
            const float half = 0.5f * spreadWidth * spreadNow, centre = std::clamp (p.x, half, 1.0f - half);
            l.x = centre - half, r.x = centre + half;
        }
        else
            designedR = { -1.0f, -1.0f, -1.0f }; // unused: worked out again when Spread splits the sides
        pointL = l, pointR = r;

        // A new cube: the dip down, then the change at the bottom of it (the filter emptied, so nothing rings over
        // from the old cube), then up. Before anything is heard it changes at once.
        if (cubeTarget != cubeIndex)
        {
            if (! designed || switchGain <= 0.0f)
            {
                cubeIndex = cubeTarget;
                for (auto& st : state)
                    st = PairState {};
                switchDir = designed ? 1 : 0;
            }
            else
                switchDir = -1;
        }
        else if (switchDir < 0)
            switchDir = 1; // back to the cube it was leaving
        if (switchDir > 0 && switchGain >= 1.0f)
            switchDir = 0, switchGain = 1.0f;

        const bool again = ! designed || designedCube != cubeIndex || designedEmphasis != emphasisNow;
        const CubeTable& cube = cubeTable (cubeIndex);
        bool changed = again || splitNow != pairedSplit;
        if (again || l != designedL)
            design (cube, l, emphasisNow, rate, designL), designedL = l, changed = true;
        if (splitNow && (again || r != designedR))
            design (cube, r, emphasisNow, rate, designR), designedR = r, changed = true;
        if (again && designedCube != cubeIndex)
            for (int b = designL.bands; b < maxBands; ++b) // bands a new cube does not use start empty if it later does
                state[b] = PairState {};
        designed = true, designedCube = cubeIndex, designedEmphasis = emphasisNow;
        const Design& dr = splitNow ? designR : designL;
        bandsNow = designL.bands;
        pairedSplit = splitNow;
        for (int b = 0; changed && b < bandsNow; ++b) // the two sides' coefficients side by side, when they moved
        {
            const BandCoefs &cl = designL.band[b], &cr = dr.band[b];
            pairs[b] = { Pair { cl.a1, cr.a1 }, Pair { cl.a2, cr.a2 }, Pair { cl.a3, cr.a3 }, Pair { cl.m0, cr.m0 }, Pair { cl.m1, cr.m1 },
                         Pair { cl.m2, cr.m2 } };
        }
        if (changed)
            wetPair = Pair { designL.wet, dr.wet }, dryPair = Pair { designL.dryGain, dr.dryGain };

        // Drive and the gains: settled ones snap to their value (so 0 dB Drive is no saturator at all). A gain's
        // per-sample glide stalls a few 1e-5 short of its value, where its steps no longer move a float.
        for (auto [now, to] : { std::pair<float*, float> { &mixNow, mixTarget }, { &outNow, outTarget }, { &driveNow, driveTarget }, { &compNow, compTarget } })
            if (std::fabs (*now - to) <= 3e-4f * std::fabs (to) + 1e-6f)
                *now = to;
        driving = driveNow != 1.0f || driveTarget != 1.0f;

        // A filter that blew up (it should not): emptied.
        for (int b = 0; b < bandsNow; ++b)
            if (! std::isfinite (state[b].ic1[0] + state[b].ic1[1] + state[b].ic2[0] + state[b].ic2[1]))
            {
                for (auto& s : state)
                    s = PairState {};
                break;
            }
    }

    // A control step's frames, the bands' count fixed so the loop over them unrolls and their states stay in registers.
    template <bool drive>
    void runBands (const float* inL, const float* inR, float* outL, float* outR, uint32_t n)
    {
        switch (bandsNow)
        {
            case 1: runFrames<drive, 1> (inL, inR, outL, outR, n); break;
            case 2: runFrames<drive, 2> (inL, inR, outL, outR, n); break;
            case 3: runFrames<drive, 3> (inL, inR, outL, outR, n); break;
            case 4: runFrames<drive, 4> (inL, inR, outL, outR, n); break;
            case 5: runFrames<drive, 5> (inL, inR, outL, outR, n); break;
            case 6: runFrames<drive, 6> (inL, inR, outL, outR, n); break;
            case 7: runFrames<drive, 7> (inL, inR, outL, outR, n); break;
            default: runFrames<drive, 0> (inL, inR, outL, outR, n); break;
        }
        static_assert (maxBands == 7, "a case a band count");
    }

    // The frames: the envelope, the gains' glides, Drive, the bands (both sides at once, morph.h's runBand on each
    // lane), Mix, Output and the limit. The state is copied into locals for the step so nothing is reloaded after each
    // store to the output (which may be the input).
    template <bool drive, int N>
    void runFrames (const float* inL, const float* inR, float* outL, float* outR, uint32_t n)
    {
        PairCoefs c[N > 0 ? N : 1];
        PairState s[N > 0 ? N : 1];
        for (int b = 0; b < N; ++b)
            c[b] = pairs[b], s[b] = state[b];
        const Pair wet = wetPair, dryGain = dryPair;
        const float eg = envGainNow, attack = attackCoef, release = releaseCoef, smooth = smoothCoef;
        const float switchBy = (float) switchDir * switchStep;
        float sw = switchGain;
        const float mixTo = mixTarget, outTo = outTarget, driveTo = driveTarget, compTo = compTarget;
        float follow = env, mixAt = mixNow, outAt = outNow, driveAt = driveNow, compAt = compNow;
        for (uint32_t i = 0; i < n; ++i)
        {
            const float xl = inL[i], xr = inR[i];
            // The envelope follows the louder side.
            const float level = std::max (std::fabs (xl), std::fabs (xr)) * eg;
            follow += (level > follow ? attack : release) * (level - follow);
            mixAt += (mixTo - mixAt) * smooth;
            outAt += (outTo - outAt) * smooth;
            const Pair x { xl, xr };
            Pair in = x;
            if (drive)
            {
                // The saturator blends in over Drive's first 6 dB (its gain from 1 to 2), so Drive at 0 dB is the
                // input itself and turning it up from there is smooth.
                driveAt += (driveTo - driveAt) * smooth;
                compAt += (compTo - compAt) * smooth;
                const float amount = std::min (driveAt - 1.0f, 1.0f);
                in = x + amount * (Pair { saturate (xl * driveAt) * compAt, saturate (xr * driveAt) * compAt } - x);
            }
            Pair y = in;
            for (int b = 0; b < N; ++b)
            {
                const Pair v3 = y - s[b].ic2;
                const Pair v1 = c[b].a1 * s[b].ic1 + c[b].a2 * v3;
                const Pair v2 = s[b].ic2 + c[b].a2 * s[b].ic1 + c[b].a3 * v3;
                s[b].ic1 = 2.0f * v1 - s[b].ic1;
                s[b].ic2 = 2.0f * v2 - s[b].ic2;
                y = c[b].m0 * y + c[b].m1 * v1 + c[b].m2 * v2;
            }
            sw = std::clamp (sw + switchBy, 0.0f, 1.0f);
            y = (wet * y + dryGain * in) * sw;
            const Pair o = (x * (1.0f - mixAt) + y * mixAt) * outAt;
            outL[i] = limit (o[0]);
            if (outR != nullptr)
                outR[i] = limit (o[1]);
        }
        for (int b = 0; b < N; ++b)
            state[b] = s[b];
        env = follow, mixNow = mixAt, outNow = outAt, driveNow = driveAt, compNow = compAt, switchGain = sw;
    }
};
} // namespace cortex
