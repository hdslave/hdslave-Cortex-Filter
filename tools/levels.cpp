// Each cube's level: for each corner and the middle, the filter's gain on pink noise (the average of its response's
// power, every octave weighted the same, 20 Hz to 20 kHz) and its highest peak. For setting the corners' gains so
// moving through a cube keeps its loudness.
//   levels   (built and run by tools/levels.sh)
#include "morph.h"

#include <cmath>
#include <cstdio>

using namespace cortex;

int main()
{
    const float rate = 48000.0f;
    for (int c = 0; c < cubeCount; ++c)
    {
        std::printf ("%2d %-15s", c, cubeNames[c]);
        double worst = -1e9, least = 1e9;
        for (int k = 0; k < 9; ++k)
        {
            const Position p = k < 8 ? Position { (float) (k & 1), (float) ((k >> 1) & 1), (float) ((k >> 2) & 1) } : Position { 0.5f, 0.5f, 0.5f };
            Design d;
            design (cubeTable (c), p, 0.0f, rate, d);
            double power = 0.0, peak = -1e9;
            const int n = 600;
            for (int i = 0; i < n; ++i)
            {
                const float hz = 20.0f * std::pow (1000.0f, (i + 0.5f) / n);
                const double db = responseDb (d, hz, rate);
                power += std::pow (10.0, db / 10.0) / n;
                peak = std::max (peak, db);
            }
            const double pink = 10.0 * std::log10 (power);
            worst = std::max (worst, peak), least = std::min (least, pink);
            std::printf (" %c%+5.1f/%+5.1f", k < 8 ? '0' + k : 'm', pink, peak);
        }
        // The loudest peak anywhere: a grid through the cube.
        double top = -1e9;
        Position at;
        for (int i = 0; i < 9 * 9 * 9; ++i)
        {
            const Position p { (i % 9) / 8.0f, ((i / 9) % 9) / 8.0f, (i / 81) / 8.0f };
            Design d;
            design (cubeTable (c), p, 0.0f, rate, d);
            for (int f = 0; f < 300; ++f)
            {
                const double db = responseDb (d, 20.0f * std::pow (1000.0f, (f + 0.5f) / 300), rate);
                if (db > top)
                    top = db, at = p;
            }
        }
        std::printf ("  top %+5.1f at (%.2f %.2f %.2f)\n", top, at.x, at.y, at.z);
    }
}
