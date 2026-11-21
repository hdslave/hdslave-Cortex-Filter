// How loud Cortex Filter's settings are against the input (dB of RMS, the dry input 0): each preset on the test loop;
// each cube on the loop and on pink noise over a grid of points (its quietest, loudest and middle); Emphasis at both
// ends; Drive. For keeping presets and cubes at the same loudness as bypass.
//   balance   (built and run by tools/balance.sh)
#include "engine.h"
#include "presets.h"
#include "../tests/signal.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace cortex;

namespace
{
Stereo pink (size_t frames)
{
    Stereo s { std::vector<float> (frames), std::vector<float> (frames) };
    Noise n (9);
    float b[2][7] = {};
    for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < 2; ++c)
        {
            const float w = n.next();
            float* p = b[c];
            p[0] = 0.99886f * p[0] + w * 0.0555179f, p[1] = 0.99332f * p[1] + w * 0.0750759f, p[2] = 0.96900f * p[2] + w * 0.1538520f;
            p[3] = 0.86650f * p[3] + w * 0.3104856f, p[4] = 0.55000f * p[4] + w * 0.5329522f, p[5] = -0.7616f * p[5] - w * 0.0168980f;
            const float out = (p[0] + p[1] + p[2] + p[3] + p[4] + p[5] + p[6] + w * 0.5362f) * 0.06f;
            p[6] = w * 0.115926f;
            (c == 0 ? s.l : s.r)[i] = out;
        }
    return s;
}

// Loudness as a meter reads it: the signal K-weighted (ITU-R BS.1770's two filters at 48 kHz: a high shelf, then a
// gentle high pass), then its mean square, in dB. Plain RMS would make anything that trims this bass-heavy loop's low
// end seem much quieter than it sounds.
double rmsDb (const Stereo& s, size_t from = 0)
{
    double sum = 0.0;
    for (const auto* x : { &s.l, &s.r })
    {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0, v1 = 0, v2 = 0;
        for (size_t i = 0; i < x->size(); ++i)
        {
            const double in = (*x)[i];
            const double y = 1.53512485958697 * in - 2.69169618940638 * x1 + 1.19839281085285 * x2 + 1.69065929318241 * y1 - 0.73248077421585 * y2;
            const double v = y - 2.0 * y1 + y2 + 1.99004745483398 * v1 - 0.99007225036621 * v2;
            x2 = x1, x1 = in, y2 = y1, y1 = y, v2 = v1, v1 = v;
            if (i >= from)
                sum += v * v;
        }
    }
    return 10.0 * std::log10 (sum / (2.0 * (double) (s.l.size() - from)) + 1e-30);
}

Stereo run (const Stereo& in, const float* values)
{
    Engine e;
    e.prepare (48000.0, 128);
    for (uint32_t i = 0; i < paramCount; ++i)
        e.set (paramTable[i].id, values[i]);
    e.reset();
    Stereo o { std::vector<float> (in.l.size()), std::vector<float> (in.l.size()) };
    for (size_t at = 0; at < in.l.size(); at += 128)
    {
        const uint32_t n = (uint32_t) std::min<size_t> (128, in.l.size() - at);
        e.transport (true, true, (double) at / 24000.0, 120.0);
        e.render (in.l.data() + at, in.r.data() + at, o.l.data() + at, o.r.data() + at, n);
    }
    return o;
}

void defaults (float* v)
{
    for (uint32_t i = 0; i < paramCount; ++i)
        v[i] = paramTable[i].def;
}
} // namespace

int main()
{
    const Stereo music = loop (48000.0, 8.0), noise = pink (48000 * 4);
    const double dryMusic = rmsDb (music, 4800), dryNoise = rmsDb (noise, 4800);
    std::printf ("presets on the loop (dB against dry):\n");
    for (const auto& p : presetTable)
    {
        float v[paramCount];
        presetValuesOf (p, v);
        const Stereo o = run (music, v);
        float peak = 0.0f;
        for (size_t i = 0; i < o.l.size(); ++i)
            peak = std::max ({ peak, std::fabs (o.l[i]), std::fabs (o.r[i]) });
        std::printf ("  %-15s %+6.1f dB   peak %.2f\n", p.name, rmsDb (o, 4800) - dryMusic, peak);
    }
    std::printf ("cubes: loop min/mid/max over x,z in {0,.5,1} at y .5, and y .25/.75 at the middle; pink the same; Emphasis -1/+1 at the middle (loop)\n");
    for (int c = 0; c < cubeCount; ++c)
    {
        double lo[2] = { 1e9, 1e9 }, hi[2] = { -1e9, -1e9 }, mid[2] = {};
        for (int k = 0; k < 9 + 2; ++k)
        {
            float v[paramCount];
            defaults (v);
            v[paramIndex (ParamId::filter)] = (float) (c + 1);
            v[paramIndex (ParamId::glide)] = 0.0f;
            v[paramIndex (ParamId::morph)] = k < 9 ? (k % 3) * 0.5f : 0.5f;
            v[paramIndex (ParamId::transform)] = k < 9 ? (k / 3) * 0.5f : 0.5f;
            v[paramIndex (ParamId::freq)] = k < 9 ? 0.5f : (k == 9 ? 0.25f : 0.75f);
            for (int s = 0; s < 2; ++s)
            {
                const double db = rmsDb (run (s == 0 ? music : noise, v), 4800) - (s == 0 ? dryMusic : dryNoise);
                lo[s] = std::min (lo[s], db), hi[s] = std::max (hi[s], db);
                if (k == 4)
                    mid[s] = db;
            }
        }
        double emph[2];
        for (int e = 0; e < 2; ++e)
        {
            float v[paramCount];
            defaults (v);
            v[paramIndex (ParamId::filter)] = (float) (c + 1), v[paramIndex (ParamId::morph)] = 0.5f, v[paramIndex (ParamId::freq)] = 0.5f;
            v[paramIndex (ParamId::transform)] = 0.5f, v[paramIndex (ParamId::emphasis)] = e == 0 ? -1.0f : 1.0f;
            emph[e] = rmsDb (run (music, v), 4800) - dryMusic;
        }
        std::printf ("  %2d %-15s loop %+6.1f %+6.1f %+6.1f   pink %+6.1f %+6.1f %+6.1f   emph %+6.1f %+6.1f\n", c, cubeNames[c], lo[0], mid[0], hi[0],
                     lo[1], mid[1], hi[1], emph[0], emph[1]);
    }
    std::printf ("factory filters F17-F32 against their cube as it is (loop, every knob at the middle; Transform 70 %% too for those it drives):\n");
    for (int slot = 17; slot <= factoryFilterCount; ++slot)
    {
        const Store bank;
        double db[3];
        for (int k = 0; k < 3; ++k)
        {
            float v[paramCount];
            defaults (v);
            const int cube = (int) bank.field (slot, fieldCube);
            v[paramIndex (ParamId::filter)] = k == 0 ? (float) (cube > 0 ? cube : 32) : (float) slot;
            v[paramIndex (ParamId::glide)] = 0.0f, v[paramIndex (ParamId::morph)] = 0.5f, v[paramIndex (ParamId::freq)] = 0.5f;
            v[paramIndex (ParamId::transform)] = k == 2 ? 0.7f : 0.5f;
            db[k] = rmsDb (run (music, v), 4800) - dryMusic;
        }
        std::printf ("  F%02d %-13s %+6.1f dB (cube %+6.1f)   Transform 70 %% %+6.1f\n", slot, Store().name (slot), db[1], db[0], db[2]);
    }
    std::printf ("drive at Init, Mix 100 %% (loop):");
    for (float d : { 0.0f, 6.0f, 12.0f, 18.0f, 24.0f })
    {
        float v[paramCount];
        defaults (v);
        v[paramIndex (ParamId::drive)] = d, v[paramIndex (ParamId::freq)] = 1.0f, v[paramIndex (ParamId::transform)] = 0.0f;
        std::printf ("  %2.0f dB: %+5.1f", d, rmsDb (run (music, v), 4800) - dryMusic);
    }
    std::printf ("\n");
}
