// Cortex Filter's parameters: one table for the CLAP side (names, ranges, text) and the engine (what a knob means).
// Ids are fixed forever: a saved project keeps its values by id. New parameters take new ids.
#pragma once

#include "cubes.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#ifndef CORTEX_VERSION
#define CORTEX_VERSION "0.1.0"
#endif

namespace cortex
{
namespace ParamId
{
enum : uint32_t
{
    cube = 0, morph, freq, transform, emphasis, glide,
    drive, mix, output, spread,
    lfoSync, lfoRate, lfoShape, lfoPhase, lfoMorph, lfoFreq, lfoTransform,
    envGain, envAttack, envRelease, envMorph, envFreq, envTransform,
    count
};
}

enum class Kind : uint8_t { continuous, integer, choice };
// How a value reads.
enum class Unit : uint8_t { percent, bipolar, decibels, lfoHz, glide, attack, release, degrees, choice };

struct Param
{
    uint32_t id;
    const char* name;
    const char* module;
    float min, max, def;
    Kind kind;
    Unit unit;
    const char* const* choices; // for Kind::choice: max - min + 1 names
};

// --- What the knobs mean ---------------------------------------------------------------------------------------

// Emphasis: every band's Q up to four times as sharp (+100 %) or half as sharp (-100 %), exponentially; widened
// boosts are evened out in morph.h's design().
constexpr float emphasisOctaves = 2.0f, emphasisWideOctaves = 1.0f; // log2 of the Q's factor at each end
constexpr float minQ = 0.1f, maxQ = 80.0f;

// Glide: how fast Morph, Freq and Transform follow their knobs (the time constant), 1 ms to 2 s.
inline float glideSeconds (float k) { return 0.001f * std::pow (2000.0f, k); }
inline float glideKnob (float s) { return std::log (s / 0.001f) / std::log (2000.0f); }

// The LFO's free rate, 0.02 Hz to 20 Hz.
inline float lfoHz (float k) { return 0.02f * std::pow (1000.0f, k); }
inline float lfoKnob (float hz) { return std::log (hz / 0.02f) / std::log (1000.0f); }

// The envelope follower: attack 0.1 ms to 200 ms, release 5 ms to 2 s. It reads the input from -48 dB (0) to 0 dB
// (full), after Env Gain.
inline float attackSeconds (float k) { return 0.0001f * std::pow (2000.0f, k); }
inline float attackKnob (float s) { return std::log (s / 0.0001f) / std::log (2000.0f); }
inline float releaseSeconds (float k) { return 0.005f * std::pow (400.0f, k); }
inline float releaseKnob (float s) { return std::log (s / 0.005f) / std::log (400.0f); }
constexpr float envFloorDb = -48.0f;

// Depths at full turn: an LFO swings an axis by half its length each way (from the middle, end to end); the
// envelope moves it by its whole length.
constexpr float lfoSwing = 0.5f, envSwing = 1.0f;
// Spread: the left and right channels sit this far apart on the Morph axis at full turn.
constexpr float spreadWidth = 0.5f;

// LFO Sync: Free runs at LFO Rate; the rest are a cycle's length in the host's beats, in step with the song.
inline const char* const lfoSyncChoices[] = { "Free", "8 Bars", "4 Bars", "2 Bars", "1 Bar", "1/2", "1/2T", "1/4", "1/4T",
                                              "1/8", "1/8T", "1/16", "1/16T", "1/32" };
constexpr double lfoSyncBeats[] = { 0.0, 32.0, 16.0, 8.0, 4.0, 2.0, 4.0 / 3.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
static_assert (sizeof (lfoSyncBeats) / sizeof (lfoSyncBeats[0]) == sizeof (lfoSyncChoices) / sizeof (lfoSyncChoices[0]), "a length a choice");
inline const char* const lfoShapeChoices[] = { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H", "Drift" };
enum class Shape : uint8_t { sine, triangle, sawUp, sawDown, square, sampleHold, drift };

using P = Param;
#define CONT(id, name, module, lo, hi, def, unit) P { ParamId::id, name, module, lo, hi, def, Kind::continuous, Unit::unit, nullptr }
#define CHOICE(id, name, module, list, def) P { ParamId::id, name, module, 0.0f, (float) (sizeof (list) / sizeof (list[0]) - 1), def, Kind::choice, Unit::choice, list }

// In the order a host lists them, three to a page as Melodee's Edit page shows them: the cube and where in it; the
// tone around it; the LFO; the envelope follower.
inline constexpr Param paramTable[] = {
    CHOICE (cube, "Cube", "Filter", cubeNames, 0.0f),
    CONT (morph, "Morph", "Filter", 0.0f, 1.0f, 0.0f, percent),
    CONT (freq, "Freq", "Filter", 0.0f, 1.0f, 0.6f, percent),
    CONT (transform, "Transform", "Filter", 0.0f, 1.0f, 0.3f, percent),
    CONT (emphasis, "Emphasis", "Filter", -1.0f, 1.0f, 0.0f, bipolar),
    CONT (glide, "Glide", "Filter", 0.0f, 1.0f, 0.4f, glide),
    CONT (drive, "Drive", "Output", 0.0f, 24.0f, 0.0f, decibels),
    CONT (mix, "Mix", "Output", 0.0f, 1.0f, 1.0f, percent),
    CONT (output, "Output", "Output", -24.0f, 12.0f, 0.0f, decibels),
    CONT (spread, "Spread", "Output", 0.0f, 1.0f, 0.0f, percent),
    CHOICE (lfoSync, "LFO Sync", "LFO", lfoSyncChoices, 0.0f),
    CONT (lfoRate, "LFO Rate", "LFO", 0.0f, 1.0f, 0.4f, lfoHz),
    CHOICE (lfoShape, "LFO Shape", "LFO", lfoShapeChoices, 0.0f),
    CONT (lfoPhase, "LFO Phase", "LFO", 0.0f, 1.0f, 0.25f, degrees),
    CONT (lfoMorph, "LFO > Morph", "LFO", -1.0f, 1.0f, 0.0f, bipolar),
    CONT (lfoFreq, "LFO > Freq", "LFO", -1.0f, 1.0f, 0.0f, bipolar),
    CONT (lfoTransform, "LFO > Transform", "LFO", -1.0f, 1.0f, 0.0f, bipolar),
    CONT (envGain, "Env Gain", "Envelope", 0.0f, 24.0f, 0.0f, decibels),
    CONT (envAttack, "Env Attack", "Envelope", 0.0f, 1.0f, 0.5f, attack),
    CONT (envRelease, "Env Release", "Envelope", 0.0f, 1.0f, 0.5f, release),
    CONT (envMorph, "Env > Morph", "Envelope", -1.0f, 1.0f, 0.0f, bipolar),
    CONT (envFreq, "Env > Freq", "Envelope", -1.0f, 1.0f, 0.0f, bipolar),
    CONT (envTransform, "Env > Transform", "Envelope", -1.0f, 1.0f, 0.0f, bipolar),
};
#undef CONT
#undef CHOICE

constexpr uint32_t paramCount = sizeof (paramTable) / sizeof (paramTable[0]);
static_assert (paramCount == ParamId::count, "every id has one row");
static_assert (paramCount <= 64, "the screen keeps a bit a row");

// The table's row for an id (a host gives ids, so they are looked up).
struct IndexOfId
{
    int16_t row[ParamId::count];
    constexpr IndexOfId() : row()
    {
        for (uint32_t i = 0; i < ParamId::count; ++i)
            row[i] = -1;
        for (uint32_t i = 0; i < paramCount; ++i)
            row[paramTable[i].id] = (int16_t) i;
    }
};
inline constexpr IndexOfId indexOfId {};
inline int paramIndex (uint32_t id) { return id < ParamId::count ? indexOfId.row[id] : -1; }

inline float clampParam (const Param& p, float v)
{
    if (! (v >= p.min)) v = p.min; // NaN too
    if (v > p.max) v = p.max;
    return p.kind == Kind::continuous ? v : std::round (v);
}

inline void formatParam (const Param& p, float v, char* text, uint32_t size)
{
    v = clampParam (p, v);
    // Each unit chosen by the value as it will print (99.96 ms prints as 100 ms, not 100.0 ms), so the text reads
    // back to the same text.
    auto time = [&] (float s) {
        const float ms = s * 1000.0f;
        if (ms < 9.995f) std::snprintf (text, size, "%.2f ms", ms);
        else if (ms < 99.95f) std::snprintf (text, size, "%.1f ms", ms);
        else if (ms < 999.5f) std::snprintf (text, size, "%.0f ms", ms);
        else std::snprintf (text, size, "%.2f s", s);
    };
    switch (p.unit)
    {
        case Unit::percent: std::snprintf (text, size, "%.0f %%", v * 100.0f); break;
        case Unit::bipolar: std::snprintf (text, size, "%+.0f %%", v * 100.0f); break;
        case Unit::decibels: std::snprintf (text, size, "%+.1f dB", v); break;
        case Unit::lfoHz: std::snprintf (text, size, lfoHz (v) < 0.9995f ? "%.3f Hz" : "%.2f Hz", lfoHz (v)); break;
        case Unit::glide: time (glideSeconds (v)); break;
        case Unit::attack: time (attackSeconds (v)); break;
        case Unit::release: time (releaseSeconds (v)); break;
        case Unit::degrees: std::snprintf (text, size, "%.0f deg", v * 360.0f); break;
        case Unit::choice: std::snprintf (text, size, "%s", p.choices[(int) v]); break;
    }
}

// Reads text as formatParam writes it (units optional); a choice by its name (any case, spaces around it ignored)
// or its number alone ("4 Bar" is neither: refused rather than read as choice 4).
inline bool parseParam (const Param& p, const char* text, float& v)
{
    if (p.kind == Kind::choice)
    {
        while (*text == ' ')
            ++text;
        size_t length = std::strlen (text);
        while (length > 0 && text[length - 1] == ' ')
            --length;
        const int n = (int) (p.max - p.min) + 1;
        for (int i = 0; i < n; ++i)
            if (std::strlen (p.choices[i]) == length && strncasecmp (text, p.choices[i], length) == 0)
                return v = (float) i, true;
        char* end = nullptr;
        const long number = std::strtol (text, &end, 10);
        if (end == text || end != text + length)
            return false;
        v = clampParam (p, (float) number);
        return true;
    }
    char* end = nullptr;
    double x = std::strtod (text, &end);
    if (end == text)
        return false;
    while (*end == ' ')
        ++end;
    const bool milli = (end[0] == 'm' && end[1] == 's');
    switch (p.unit)
    {
        case Unit::percent:
        case Unit::bipolar: v = (float) (x / 100.0); break;
        case Unit::lfoHz: v = lfoKnob ((float) x); break;
        case Unit::glide: v = glideKnob ((float) (milli ? x / 1000.0 : x)); break;
        case Unit::attack: v = attackKnob ((float) (milli ? x / 1000.0 : x)); break;
        case Unit::release: v = releaseKnob ((float) (milli ? x / 1000.0 : x)); break;
        case Unit::degrees: v = (float) (x / 360.0); break;
        default: v = (float) x; break;
    }
    v = clampParam (p, v); // a time or rate of 0 or less (a log of it NaN or -inf) is the knob's lowest
    return true;
}
} // namespace cortex
