// Test signals for Cortex Filter's tests and demos, the same on every run: a loop (a beat, a bass line and a chord at 120
// bpm, made from simple waves and seeded noise), white noise, an impulse.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

struct Stereo
{
    std::vector<float> l, r;
};

// A small noise source: the same numbers for the same seed.
struct Noise
{
    uint32_t state;
    explicit Noise (uint32_t seed) : state (seed * 2654435761u + 1u) {}
    float next() // -1..1
    {
        state = state * 1664525u + 1013904223u;
        return (float) (state >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }
};

inline Stereo whiteNoise (size_t frames, float level, uint32_t seed)
{
    Stereo s { std::vector<float> (frames), std::vector<float> (frames) };
    Noise n (seed);
    for (size_t i = 0; i < frames; ++i)
        s.l[i] = level * n.next(), s.r[i] = level * n.next();
    return s;
}

inline Stereo impulse (size_t frames, float level, size_t at = 0)
{
    Stereo s { std::vector<float> (frames, 0.0f), std::vector<float> (frames, 0.0f) };
    if (at < frames)
        s.l[at] = s.r[at] = level;
    return s;
}

// Two bars at 120 bpm, looped for `seconds`: kick on the beats, snare on two and four, off-beat hats, a saw bass
// line and a soft chord (detuned a little apart left and right). Peaks under 0.9.
inline Stereo loop (double rate, double seconds, uint32_t seed = 1)
{
    const size_t frames = (size_t) (seconds * rate);
    Stereo s { std::vector<float> (frames), std::vector<float> (frames) };
    Noise noise (seed);
    const double step = 0.125; // a sixteenth at 120 bpm, in seconds
    const double bass[8] = { 55.0, 55.0, 65.41, 49.0, 55.0, 82.41, 73.42, 49.0 }; // a note a beat over two bars
    const double chord[3] = { 220.0, 261.63, 329.63 };
    double bassPhase = 0.0, padPhase[2][3] = {}, kickPhase = 0.0, snarePhase = 0.0;
    float hatHp = 0.0f, bassLp = 0.0f;
    for (size_t i = 0; i < frames; ++i)
    {
        const double t = (double) i / rate;
        const double beatT = std::fmod (t, 4.0 * step);
        // Kick: a falling sine on every beat.
        const double kf = 45.0 + 105.0 * std::exp (-beatT * 30.0);
        kickPhase += kf / rate;
        const float kick = (float) (std::sin (2.0 * M_PI * kickPhase) * std::exp (-beatT * 9.0)) * 0.55f;
        // Snare on beats two and four: noise and a tone.
        const long beatInBar = (long) std::floor (t / (4.0 * step)) % 4;
        const double snareT = (beatInBar == 1 || beatInBar == 3) ? beatT : 1.0;
        snarePhase += 185.0 / rate;
        const float wn = noise.next();
        const float snare = (float) std::exp (-snareT * 22.0) * (0.25f * wn + 0.15f * (float) std::sin (2.0 * M_PI * snarePhase));
        // Hats on the off-beat eighths: high-passed noise.
        const double hatT = std::fmod (t + step * 2.0, step * 4.0) < step ? std::fmod (t + step * 2.0, step * 4.0) : 1.0;
        const float hn = noise.next();
        const float hat = (hn - hatHp) * (float) std::exp (-hatT * 60.0) * 0.18f;
        hatHp = hn;
        // Bass: a saw a beat long, softened.
        const long beat = (long) std::floor (t / (4.0 * step)) % 8;
        bassPhase += bass[beat] / rate;
        bassPhase -= std::floor (bassPhase);
        const float saw = (float) (2.0 * bassPhase - 1.0) * (float) std::exp (-beatT * 3.0);
        bassLp += 0.25f * (saw - bassLp);
        const float bassOut = bassLp * 0.3f;
        // The chord: three saws a side, the right a little sharp.
        float pad[2] = { 0.0f, 0.0f };
        for (int side = 0; side < 2; ++side)
            for (int n = 0; n < 3; ++n)
            {
                padPhase[side][n] += chord[n] * (side == 0 ? 1.0 : 1.003) / rate;
                padPhase[side][n] -= std::floor (padPhase[side][n]);
                pad[side] += (float) (2.0 * padPhase[side][n] - 1.0) * 0.045f;
            }
        s.l[i] = 0.7f * (kick + snare + hat + bassOut + pad[0]);
        s.r[i] = 0.7f * (kick + snare + hat * 0.8f + bassOut + pad[1]);
    }
    return s;
}
