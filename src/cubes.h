// Cortex Filter's filter cubes. A cube is eight filters at the corners of a cube; the three knobs Morph (X), Freq (Y) and
// Transform (Z) pick a point inside it, and the filter there is the corners' filters blended by how near the point
// is to each (trilinear). Each corner filter is up to seven bands in series, each band a state-variable filter
// (a frequency and a Q) whose low pass, band pass and high pass outputs are mixed with its input: a band can be a
// peak or a dip (a bell, in dB), a low or high pass, a band pass, a notch or an all pass, and anything between, so
// a band morphs smoothly from one kind to another. Blending happens on the bands' settings (frequency and Q on a log
// scale, the mix linearly, a bell's gain in dB), never on the sound, so every point inside a cube is one stable
// filter of its own.
//
// Most cubes are four shapes (the X-Z square) with Freq moving the whole shape between two frequency ratios: Freq is
// then a true cutoff knob for the shape.
//
// Cubes are numbered by their place in this list, and a saved project keeps the number: add new cubes at the end,
// never reorder or remove one. A host that keeps values scaled to the parameter's range rather than as numbers
// (Melodee's knob bindings and recorded motion, 0..1000 of it) reads an old cube's place differently once the list
// grows: a new cube changes the Cube parameter's range for them.
#pragma once

#include <cstdint>

namespace cortex
{
constexpr int maxBands = 7;

// One band: y = dry * x + lp * LP + bp * BP + hp * HP + (10^(bellDb / 20) - 1) * BP, where LP, BP and HP are the
// state-variable filter's outputs at `hz` and `q`, BP normalized to unity gain at its centre.
struct Band
{
    float hz = 1000.0f, q = 0.707f, dry = 1.0f, lp = 0.0f, bp = 0.0f, hp = 0.0f, bellDb = 0.0f;
};

// A corner's filter: its bands in series, then gainDb, then `dry` of the input mixed in parallel (a phaser's
// notches come from that).
struct Frame
{
    Band band[maxBands];
    float gainDb = 0.0f, dry = 0.0f;
};

struct CubeSpec
{
    const char* name;
    Frame corner[8]; // corner x + 2y + 4z: x Morph, y Freq, z Transform, each 0 or 1
};

// --- Bands ------------------------------------------------------------------------------------------------------
constexpr Band peak (float hz, float q, float db) { return { hz, q, 1.0f, 0.0f, 0.0f, 0.0f, db }; }
constexpr Band lowpass (float hz, float q = 0.707f) { return { hz, q, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f }; }
constexpr Band highpass (float hz, float q = 0.707f) { return { hz, q, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f }; }
constexpr Band bandpass (float hz, float q, float gain = 1.0f) { return { hz, q, 0.0f, 0.0f, gain, 0.0f, 0.0f }; }
constexpr Band notch (float hz, float q) { return { hz, q, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f }; }
constexpr Band allpass (float hz, float q) { return { hz, q, 1.0f, 0.0f, -2.0f, 0.0f, 0.0f }; }
constexpr Band flat (float hz) { return { hz, 0.707f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f }; } // passes all: a band at rest

constexpr Frame frame (Band b0 = flat (1000.0f), Band b1 = flat (1000.0f), Band b2 = flat (1000.0f), Band b3 = flat (1000.0f),
                       Band b4 = flat (1000.0f), Band b5 = flat (1000.0f), Band b6 = flat (1000.0f))
{
    return { { b0, b1, b2, b3, b4, b5, b6 }, 0.0f, 0.0f };
}
constexpr Frame withGain (Frame f, float db) { return f.gainDb = db, f; }
constexpr Frame withDry (Frame f, float dry) { return f.dry = dry, f; }
constexpr Frame scaled (Frame f, float ratio)
{
    for (auto& b : f.band)
        b.hz *= ratio;
    return f;
}

// A cube of four shapes (a: Morph 0 Transform 0, b: Morph 1 Transform 0, c: Morph 0 Transform 1, d: Morph 1
// Transform 1), Freq moving them from `low` to `high` times their frequencies.
constexpr CubeSpec shifted (const char* name, Frame a, Frame b, Frame c, Frame d, float low, float high)
{
    return { name, { scaled (a, low), scaled (b, low), scaled (a, high), scaled (b, high), scaled (c, low), scaled (d, low),
                     scaled (c, high), scaled (d, high) } };
}

// Six modal peaks at `ratios` times `hz` followed by a gentle low pass at `hz * topRatio` that rolls off raw high-end
// bleed above the resonant body (unlike 7 pure peaking bands, which pass broadband hiss flat to Nyquist).
constexpr Frame resonators (float hz, const float (&ratios)[6], float topRatio, float q, float db, float gainDb)
{
    Frame f = frame();
    for (int i = 0; i < 6; ++i)
        f.band[i] = peak (hz * ratios[i], q, db);
    f.band[6] = lowpass (hz * topRatio, 0.707f);
    return withGain (f, gainDb);
}

// Seven peaks or dips with alternating depths (`evenDb` on bands 0, 2, 4, 6; `oddDb` on 1, 3, 5) so blending peaks to
// dips passes through an alternating peak/notch comb rather than cancelling flat to 0 dB across the middle of the cube.
constexpr Frame combBands (float hz, const float (&ratios)[7], float q, float evenDb, float oddDb, float gainDb)
{
    Frame f = frame();
    for (int i = 0; i < 7; ++i)
        f.band[i] = peak (hz * ratios[i], q, (i & 1) == 0 ? evenDb : oddDb);
    return withGain (f, gainDb);
}

// A sung vowel: five formants as peaks (frequency, level in dB and bandwidth of each, a tenor's), between a high
// pass and a low pass. Because the bands run in series, overlapping upper-formant skirts (F2-F5) stack; `seriesTrim`
// offsets that buildup so the cascade's actual peaks match the tenor formant ratios without a harsh 2.6-3.2 kHz spike.
// `makeupDb` evens the vowels' loudness.
struct Vowel
{
    float hz[5], db[5], bandwidth[5], makeupDb;
};
constexpr Frame vowel (const Vowel& v)
{
    constexpr float lift = 18.5f;
    constexpr float seriesTrim[5] = { 0.0f, -2.5f, -3.5f, -3.5f, -3.0f };
    Frame f = frame (highpass (85.0f, 0.7f));
    for (int i = 0; i < 5; ++i)
    {
        const float db = lift + v.db[i] + seriesTrim[i];
        f.band[1 + i] = peak (v.hz[i], v.hz[i] / (v.bandwidth[i] * 1.6f), db < 2.0f ? 2.0f : db);
    }
    f.band[6] = lowpass (5000.0f, 0.7f);
    return withGain (f, -11.5f + v.makeupDb);
}
constexpr Vowel vowelA { { 650, 1080, 2650, 2900, 3250 }, { 0, -6, -7, -8, -22 }, { 80, 90, 120, 130, 140 }, 1.2f };
constexpr Vowel vowelE { { 400, 1700, 2600, 3200, 3580 }, { 0, -14, -12, -14, -20 }, { 70, 80, 100, 120, 120 }, 1.8f };
constexpr Vowel vowelI { { 290, 1870, 2800, 3250, 3540 }, { 0, -15, -18, -20, -30 }, { 40, 90, 100, 120, 120 }, 2.6f };
constexpr Vowel vowelO { { 400, 800, 2600, 2800, 3000 }, { 0, -10, -12, -12, -26 }, { 40, 80, 100, 120, 120 }, 2.7f };
constexpr Vowel vowelU { { 350, 600, 2700, 2900, 3300 }, { 0, -20, -17, -14, -26 }, { 40, 60, 100, 120, 120 }, 2.8f };

// Six all passes at `ratios` times `hz`, half mixed with the input: the notches of a phaser.
constexpr Frame phaser (float hz, const float (&ratios)[6], float q)
{
    Frame f = frame();
    for (int i = 0; i < 6; ++i)
        f.band[i] = allpass (hz * ratios[i], q);
    return withDry (f, 0.5f);
}

constexpr float harmonics[7] = { 1, 2, 3, 4, 5, 6, 7 };
constexpr float oddHarmonics[7] = { 1, 3, 5, 7, 9, 11, 13 };
constexpr float harmonics6[6] = { 1, 2, 3, 4, 5, 6 };
constexpr float bellPartials6[6] = { 0.5f, 1.0f, 1.19f, 1.5f, 2.0f, 3.0f };        // hum, prime, tierce, quint, nominal, octave
constexpr float membraneModes6[6] = { 1.0f, 1.594f, 2.136f, 2.653f, 2.918f, 3.156f }; // a struck round plate
constexpr float barModes6[6] = { 1.0f, 2.756f, 5.404f, 8.933f, 13.34f, 18.64f };   // a struck free bar
constexpr float majorChord6[6] = { 1.0f, 1.26f, 1.5f, 2.0f, 2.52f, 3.0f };
constexpr float minorChord6[6] = { 1.0f, 1.189f, 1.5f, 2.0f, 2.378f, 3.0f };
constexpr float phaserTight[6] = { 1.0f, 1.6f, 2.6f, 4.2f, 6.8f, 11.0f };
constexpr float phaserWide[6] = { 1.0f, 2.2f, 4.8f, 10.6f, 23.0f, 51.0f };

constexpr float wide = 1.0f / 32.0f, tall = 16.0f; // a sweep's ends: 1 kHz from about 31 Hz to 16 kHz

// --- The cubes --------------------------------------------------------------------------------------------------
inline constexpr CubeSpec cubeSpecs[] = {
    // 0. A low pass morphing to a band pass (Morph); Transform from a gentle 2-pole to a resonant 4-pole; Freq the
    // cutoff, about 31 Hz to 16 kHz.
    shifted ("Low To Band", frame (lowpass (1000, 0.707f), flat (1000)), withGain (frame (bandpass (1000, 0.707f, 1.25f), flat (1000)), 3.0f),
             withGain (frame (lowpass (1000, 0.707f), lowpass (1000, 4.5f)), -1.5f),
             withGain (frame (bandpass (1000, 1.3f, 1.35f), peak (1000, 4.5f, 10.0f)), 1.0f), wide, tall),
    // 1. The same from a high pass.
    shifted ("High To Band", frame (highpass (1000, 0.707f), flat (1000)), withGain (frame (bandpass (1000, 0.707f, 1.25f), flat (1000)), 3.0f),
             withGain (frame (highpass (1000, 0.707f), highpass (1000, 4.5f)), -1.5f),
             withGain (frame (bandpass (1000, 1.3f, 1.35f), peak (1000, 4.5f, 10.0f)), 1.0f), wide, tall),
    // 2-3. Vowels: Morph and Transform move between four; Freq shifts the voice an octave down or up.
    shifted ("Vowels A E O U", vowel (vowelA), vowel (vowelE), vowel (vowelO), vowel (vowelU), 0.5f, 2.0f),
    shifted ("Vowels I U E A", vowel (vowelI), vowel (vowelU), vowel (vowelE), vowel (vowelA), 0.5f, 2.0f),
    // 4. A phaser: Morph spreads its notches from close to far apart, Transform sharpens them, Freq sweeps them.
    shifted ("Phase Drift", phaser (60, phaserTight, 0.7f), phaser (60, phaserWide, 0.7f), phaser (60, phaserTight, 3.0f),
             phaser (60, phaserWide, 3.0f), 1.0f, 32.0f),
    // 5. Peaks on a harmonic series (Morph: all harmonics to the odd ones); Transform turns the peaks to dips through
    // an active alternating comb; Freq the root, 27 Hz to 880 Hz.
    shifted ("Harmonic Comb", combBands (110, harmonics, 10.0f, 15.0f, 12.0f, -13.5f), combBands (110, oddHarmonics, 10.0f, 15.0f, 12.0f, -12.5f),
             combBands (110, harmonics, 6.0f, -12.0f, -24.0f, 0.5f), combBands (110, oddHarmonics, 6.0f, -12.0f, -24.0f, 0.5f), 0.25f, 8.0f),
    // 6. A wah to a talking three-peak mouth (Morph); Transform sharpens; Freq is the pedal, 300 Hz to 2.2 kHz.
    shifted ("Wah Talk",
             withGain (frame (highpass (70), peak (500, 4.5f, 16.0f), flat (1300), flat (2600), lowpass (5000)), -6.5f),
             withGain (frame (highpass (70), peak (500, 5.0f, 14.0f), peak (1300, 5.5f, 12.0f), peak (2600, 6.5f, 9.0f), lowpass (5000)), -7.0f),
             withGain (frame (highpass (70), peak (500, 9.5f, 19.0f), flat (1300), flat (2600), lowpass (5000)), -6.5f),
             withGain (frame (highpass (70), peak (500, 9.5f, 17.0f), peak (1300, 10.0f, 15.0f), peak (2600, 10.5f, 12.0f), lowpass (5000)), -7.5f),
             0.6f, 4.4f),
    // 7. Ringing peaks from a harmonic series to a church bell's partials (Morph); Transform rings longer.
    shifted ("Bell Tower", resonators (220, harmonics6, 8.0f, 14.0f, 15.0f, -11.5f), resonators (220, bellPartials6, 5.5f, 14.0f, 15.0f, -12.0f),
             resonators (220, harmonics6, 8.0f, 34.0f, 19.0f, -10.5f), resonators (220, bellPartials6, 5.5f, 34.0f, 19.0f, -10.5f), 0.25f, 4.0f),
    // 8. Full range to a telephone's band (Morph); Transform adds a honk and sharp edges; Freq moves the band.
    shifted ("Radio Tuner", frame (highpass (50, 0.7f), flat (1500), lowpass (14000, 0.7f), flat (3200)),
             frame (highpass (380, 1.0f), peak (1500, 1.0f, 6.0f), lowpass (3200, 1.2f), lowpass (3200, 0.6f)),
             withGain (frame (highpass (50, 2.2f), peak (1000, 4.5f, 11.0f), lowpass (12000, 2.2f), flat (3200)), -4.5f),
             withGain (frame (highpass (380, 2.5f), peak (1500, 5.0f, 12.0f), lowpass (3200, 2.5f), lowpass (3200, 0.6f)), -5.5f),
             0.35f, 2.8f),
    // 9. A squelchy resonant low pass to a resonant band pass (Morph); Transform splits the peak into two a fifth
    // apart (Band 1 sits at 1500 Hz throughout so its peak never collides with Band 0's at 1000 Hz); Freq the cutoff.
    shifted ("Acid Twins", withGain (frame (lowpass (1000, 6.5f), lowpass (1500, 0.65f), flat (1500)), -3.0f),
             withGain (frame (bandpass (1000, 0.9f, 1.35f), peak (1000, 6.5f, 14.0f), flat (1500)), -3.5f),
             withGain (frame (lowpass (1000, 6.5f), lowpass (1500, 6.5f), flat (1500)), -5.5f),
             withGain (frame (bandpass (1000, 0.9f, 1.35f), peak (1000, 6.5f, 14.0f), peak (1500, 6.5f, 13.0f)), -4.0f), wide, tall),
    // 10. Notches like a tube's anti-resonances (Morph: uneven to harmonic); Transform adds peaks between them.
    shifted ("Hollow Tube",
             frame (notch (300, 3.0f), notch (690, 3.0f), notch (1170, 3.0f), notch (1680, 3.0f), lowpass (6000), flat (480), flat (1450)),
             frame (notch (300, 8.0f), notch (600, 8.0f), notch (900, 8.0f), notch (1200, 8.0f), lowpass (6000), flat (450), flat (1050)),
             withGain (frame (notch (300, 3.0f), notch (690, 3.0f), notch (1170, 3.0f), notch (1680, 3.0f), lowpass (6000),
                              peak (480, 4.0f, 10.0f), peak (1450, 4.0f, 10.0f)), -4.5f),
             withGain (frame (notch (300, 8.0f), notch (600, 8.0f), notch (900, 8.0f), notch (1200, 8.0f), lowpass (6000),
                              peak (450, 6.0f, 10.0f), peak (1050, 6.0f, 10.0f)), -4.5f),
             0.25f, 4.0f),
    // 11. Five peaks turning like a barber's pole (Shepard filter): every band climbs monotonically while the lowest
    // fades in and the highest fades out, avoiding series crossing spikes; Transform turns them to staggered dips.
    shifted ("Swirl",
             withGain (frame (peak (140, 4.5f, 0.0f), peak (350, 4.5f, 12.0f), peak (900, 4.5f, 12.0f), peak (2300, 4.5f, 12.0f), peak (5800, 4.5f, 11.0f)), -7.0f),
             withGain (frame (peak (350, 4.5f, 12.0f), peak (900, 4.5f, 12.0f), peak (2300, 4.5f, 12.0f), peak (5800, 4.5f, 11.0f), peak (12000, 4.5f, 0.0f)), -7.0f),
             withGain (frame (peak (140, 4.0f, 0.0f), peak (350, 4.0f, -20.0f), peak (900, 4.0f, -10.0f), peak (2300, 4.0f, -20.0f), peak (5800, 4.0f, -10.0f)), 0.5f),
             withGain (frame (peak (350, 4.0f, -20.0f), peak (900, 4.0f, -10.0f), peak (2300, 4.0f, -20.0f), peak (5800, 4.0f, -10.0f), peak (12000, 4.0f, 0.0f)), 0.5f),
             0.5f, 2.0f),
    // 12. A struck plate's modes to a bar's (Morph); Transform rings longer.
    shifted ("Metal Plate", resonators (300, membraneModes6, 5.5f, 16.0f, 15.0f, -16.0f), resonators (300, barModes6, 22.0f, 16.0f, 15.0f, -8.5f),
             resonators (300, membraneModes6, 5.5f, 38.0f, 19.0f, -13.5f), resonators (300, barModes6, 22.0f, 38.0f, 19.0f, -8.0f), 0.25f, 4.0f),
    // 13. A tone control for mixing: dark to bright (Morph); Transform scoops the mids.
    shifted ("Tilt Air",
             frame (peak (90, 0.6f, 6.0f), peak (2000, 0.8f, -3.0f), peak (9000, 0.6f, -6.0f), highpass (20), lowpass (7000, 0.6f)),
             frame (peak (90, 0.6f, -4.0f), peak (2000, 0.8f, 2.0f), peak (10000, 0.6f, 7.0f), highpass (110), lowpass (20000)),
             frame (peak (90, 0.6f, 6.0f), peak (800, 0.7f, -8.0f), peak (9000, 0.6f, -6.0f), highpass (20), lowpass (7000, 0.6f)),
             frame (peak (90, 0.6f, -4.0f), peak (800, 0.7f, -8.0f), peak (10000, 0.6f, 7.0f), highpass (110), lowpass (20000)),
             0.5f, 2.0f),
    // 14. Low end: a sub boom to a punchy mid-bass with the boxiness cut (Morph); Transform resonates.
    shifted ("Sub Thump",
             withGain (frame (peak (60, 2.0f, 10.0f), flat (450), lowpass (3000), highpass (25)), -3.0f),
             withGain (frame (peak (120, 1.2f, 8.0f), notch (450, 1.2f), lowpass (6000), highpass (25)), -2.0f),
             withGain (frame (peak (60, 5.5f, 13.0f), flat (450), lowpass (2000, 1.8f), highpass (30, 1.4f)), -6.0f),
             withGain (frame (peak (120, 4.5f, 11.0f), notch (450, 3.0f), lowpass (4000, 2.2f), highpass (30, 1.4f)), -5.5f),
             0.5f, 2.0f),
    // 15. Resonators tuned to a chord: major to minor (Morph); Transform rings longer; Freq the root, 55 to 440 Hz.
    shifted ("Chord Bank", resonators (110, majorChord6, 5.0f, 12.0f, 14.0f, -14.5f), resonators (110, minorChord6, 5.0f, 12.0f, 14.0f, -14.5f),
             resonators (110, majorChord6, 5.0f, 30.0f, 18.0f, -12.0f), resonators (110, minorChord6, 5.0f, 30.0f, 18.0f, -12.0f), 0.5f, 4.0f),
};
constexpr int cubeCount = (int) (sizeof (cubeSpecs) / sizeof (cubeSpecs[0]));

// The names, for the Cube parameter's choices.
inline constexpr const char* cubeNames[] = {
    "Low To Band", "High To Band", "Vowels A E O U", "Vowels I U E A", "Phase Drift", "Harmonic Comb", "Wah Talk",
    "Bell Tower", "Radio Tuner", "Acid Twins", "Hollow Tube", "Swirl", "Metal Plate", "Tilt Air", "Sub Thump", "Chord Bank",
};
static_assert (sizeof (cubeNames) / sizeof (cubeNames[0]) == cubeCount, "a name a cube");
constexpr bool namesMatch()
{
    for (int i = 0; i < cubeCount; ++i)
    {
        const char *a = cubeSpecs[i].name, *b = cubeNames[i];
        while (*a != 0 && *a == *b)
            ++a, ++b;
        if (*a != *b)
            return false;
    }
    return true;
}
static_assert (namesMatch(), "the choices name the cubes in order");
} // namespace cortex
