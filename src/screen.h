// Cortex Filter's page on Melodee's screen (melodee_screen.h, ui/page.h).
//
// CUBE, the readout: on the left the filter cube in 3D, swaying slowly while sound runs through it, with the point
// the filter is at (a second one for the right channel when Spread splits them), its trail, the path the LFO will
// take, lines dropped from it to the cube's back walls, and the eight corners lit by how much of each the filter
// takes. On the right the filter's response in 3D: in TIME view the last moments of it, newest in front, receding
// into the past; in MORPH view the whole Morph axis at the current Freq and Transform, the response now lit where it
// sits in it. The wheel steps through the cubes, Push changes the view, K1-K3 turn Morph, Freq and Transform.
//
// The other groups list the knobs, the cube small beside them. Every response drawn is worked out from the same
// numbers the sound uses (morph.h).
#pragma once

#include "engine.h"
#include "morph.h"
#include "params.h"
#include "ui/page.h"

#include <atomic>
#include <cctype>

namespace cortex
{
// What the audio thread tells the page after each block: where each channel's filter is, the envelope, the LFO's
// place, the rate, and that it ran.
struct Feed
{
    std::atomic<float> l[3] {}, r[3] {};
    std::atomic<float> env { 0.0f }, rate { 48000.0f };
    std::atomic<double> lfo { 0.0 };
    std::atomic<bool> split { false };
    std::atomic<uint32_t> blocks { 0 };
};

// The cube in perspective: a point of it (each axis 0..1) on the screen, and its depth (larger is farther).
struct Camera
{
    float cosYaw, sinYaw, cosPitch, sinPitch, dist, focal, ox, oy;
    Camera (float yaw, float pitch, float distance, float focalLength, float x, float y)
        : cosYaw (std::cos (yaw)), sinYaw (std::sin (yaw)), cosPitch (std::cos (pitch)), sinPitch (std::sin (pitch)),
          dist (distance), focal (focalLength), ox (x), oy (y)
    {
    }
    void at (float x, float y, float z, float& sx, float& sy, float& depth) const
    {
        const float u = 2.0f * x - 1.0f, v = 2.0f * y - 1.0f, w = 2.0f * z - 1.0f;
        const float x1 = u * cosYaw - w * sinYaw, z1 = u * sinYaw + w * cosYaw;
        const float y2 = v * cosPitch + z1 * sinPitch, z2 = z1 * cosPitch - v * sinPitch;
        const float d = z2 + dist;
        sx = ox + focal * x1 / d, sy = oy - focal * y2 / d, depth = z2;
    }
    void at (float x, float y, float z, int& sx, int& sy) const
    {
        float fx, fy, d;
        at (x, y, z, fx, fy, d);
        sx = (int) std::lround (fx), sy = (int) std::lround (fy);
    }
};

class Screen final : public hdui::Page
{
public:
    Screen (const std::atomic<float>* values, const Feed& feed) : values (values), feed (feed)
    {
        colours = { 0xff05070e, 0xff0c1120, 0xff141c33, 0xff243050, 0xffe6eeff, 0xff8d9bc2, 0xff384464, 0xff3ee0ff, 0xff03121a };
        for (int i = 0; i < cols; ++i)
            columnHz[i] = 20.0f * std::pow (1000.0f, (float) i / (float) (cols - 1));
    }

    // The picture moves by itself: sound ran through the filter lately, or MORPH view's landscape is still catching
    // up with a change (it follows a few slices a frame, even while the sound sleeps).
    bool animating (const hdui::Host&) const override { return live || (groupIndex() == 0 && viewMorph && morphStale() > 0); }
    // A moving picture's next frame is due (about 30 a second).
    bool due (uint64_t nowMs) const { return nowMs - lastFrameMs >= frameMs; }

    // The audio thread's news and the animation's step, before each paint.
    void update (uint64_t nowMs)
    {
        const uint32_t blocks = feed.blocks.load (std::memory_order_acquire);
        if (blocks != seenBlocks)
            seenBlocks = blocks, lastBlockMs = nowMs, heard = true;
        live = heard && nowMs - lastBlockMs < liveMs;
        if (lastUpdateMs != 0 && live)
            swayMs += std::min<uint64_t> (nowMs - lastUpdateMs, 100);
        lastUpdateMs = nowMs;
        rate = std::max (8000.0f, feed.rate.load (std::memory_order_relaxed));
        if (rate != columnRate) // each column's tan(pi f / rate), for the responses
        {
            for (int i = 0; i < cols; ++i)
                columnT[i] = tanOf (columnHz[i], rate);
            columnRate = rate;
        }

        // Where the filter is: as the sound has it while it runs; otherwise from the knobs (the sound is asleep and
        // hears no knob: this is where it will start).
        if (live)
        {
            pointL = { feed.l[0].load (std::memory_order_relaxed), feed.l[1].load (std::memory_order_relaxed), feed.l[2].load (std::memory_order_relaxed) };
            pointR = { feed.r[0].load (std::memory_order_relaxed), feed.r[1].load (std::memory_order_relaxed), feed.r[2].load (std::memory_order_relaxed) };
            splitNow = feed.split.load (std::memory_order_relaxed);
        }
        else
            restingPoints();

        // The trail and the waterfall: a step each frame while live, and whenever the point or the filter moved.
        const int cube = (int) value (ParamId::cube);
        const float emphasis = value (ParamId::emphasis);
        const bool moved = pointL != trail[trailHead] || (splitNow && pointR != shownRight) || splitNow != shownSplit || cube != shownCube
                           || emphasis != shownEmphasis || rate != shownRate;
        if (moved || (live && nowMs - lastSliceMs >= sliceMs))
        {
            if (moved || trailCount == 0)
                trailHead = (trailHead + 1) % trailLength, trail[trailHead] = pointL, trailCount = std::min (trailCount + 1, trailLength);
            Design d;
            design (cubeTable (cube), pointL, emphasis, rate, d);
            sliceHead = (sliceHead + 1) % slices;
            for (int i = 0; i < cols; ++i)
                sliceDb[sliceHead][i] = responseDbAt (d, columnT[i]);
            sliceCount = std::min (sliceCount + 1, slices);
            if (splitNow)
            {
                design (cubeTable (cube), pointR, emphasis, rate, d);
                for (int i = 0; i < cols; ++i)
                    rightDb[i] = responseDbAt (d, columnT[i]);
            }
            lastSliceMs = nowMs, shownCube = cube, shownEmphasis = emphasis, shownRate = rate, shownRight = pointR, shownSplit = splitNow;
        }
    }
    void drew (uint64_t nowMs) { lastFrameMs = nowMs; }

    bool morphView() const { return viewMorph; }
    // MORPH view's slices not yet worked out for the cube and point drawn last (for the tests).
    int morphStale() const
    {
        int stale = 0;
        for (const Position& at : morphAt)
            stale += morphCube != (int) value (ParamId::cube) || at.y != pointL.y || at.z != pointL.z;
        return stale;
    }

protected:
    int groupCount() const override { return (int) (sizeof (groups) / sizeof (groups[0])); }
    const hdui::Group& groupAt (int i) const override { return groups[i]; }
    const char* title() const override { return "CORTEX FILTER"; }
    bool tallCustom() const override { return true; }

    float value (uint32_t id) const { return values[paramIndex (id)].load (std::memory_order_relaxed); }

    void read (uint32_t id, hdui::Value& out) const override
    {
        const int index = paramIndex (id);
        if (index < 0)
            return;
        const Param& p = paramTable[index];
        out.value = value (id);
        out.min = p.min, out.max = p.max, out.def = p.def;
        out.kind = p.kind == Kind::continuous ? hdui::Kind::continuous : (p.kind == Kind::integer ? hdui::Kind::stepped : hdui::Kind::choice);
        out.bipolar = p.min < 0.0f;
        formatParam (p, out.value, out.text, sizeof (out.text));
    }

    // CUBE: the wheel steps through the cubes, Push changes the view.
    bool customInput (int, hdui::Key key, int amount, bool, hdui::Edits& out) override
    {
        if (key == hdui::Key::wheel)
        {
            const float now = value (ParamId::cube), next = std::clamp (now + (float) amount, 0.0f, (float) (cubeCount - 1));
            if (next != now)
                out.set (ParamId::cube, next);
            return true;
        }
        if (key == hdui::Key::push)
            return viewMorph = ! viewMorph, true;
        return false;
    }

    void customKnobs (int, uint32_t ids[3]) const override
    {
        ids[0] = ParamId::morph, ids[1] = ParamId::freq, ids[2] = ParamId::transform;
    }

    void legend (const char* lines[6]) const override
    {
        if (groupIndex() == 0)
            lines[0] = "WHEEL  CUBE", lines[1] = "PUSH   VIEW", lines[2] = "-/+  GROUP", lines[3] = "K1 MORPH", lines[4] = "K2 FREQ",
            lines[5] = "K3 TRANSFORM";
        else
            Page::legend (lines);
    }

    // The header's right: the cube, and where in it.
    void drawStatus (hdui::Raster& r, const hdui::Host& h, int right) override
    {
        char text[40];
        const int cube = (int) value (ParamId::cube);
        std::snprintf (text, sizeof (text), "%02d %s", cube + 1, cubeNames[cube]);
        for (char* c = text; *c != 0; ++c)
            *c = (char) std::toupper ((unsigned char) *c);
        r.text (right - hdui::textWidth (text), headerY + 1, text, colours.text);
        auto pct = [] (float v) { return (int) std::lround (v * 100.0f); };
        if (splitNow)
            std::snprintf (text, sizeof (text), "X%d|%d Y%d Z%d", pct (pointL.x), pct (pointR.x), pct (pointL.y), pct (pointL.z));
        else
            std::snprintf (text, sizeof (text), "X%d Y%d Z%d", pct (pointL.x), pct (pointL.y), pct (pointL.z));
        r.text (right - hdui::textWidth (text), headerY + 13, text, live ? accent (h) : colours.muted);
    }

    // Beside a list: the cube, small, and the response under it.
    void drawPanel (hdui::Raster& r, const hdui::Host& h, int x, int y, int w, int height) override
    {
        drawCube (r, h, Camera (yaw(), pitch(), 4.6f, 128.0f, (float) (x + w / 2), (float) (y + 36)), false);
        const int top = y + height - 26;
        r.box (x, top - 2, w, 1, colours.rule);
        drawCurve (r, sliceDb[sliceHead], x, top, w, 26, accent (h));
    }

    void drawCustom (hdui::Raster& r, const hdui::Host& h, int x, int y, int w, int height) override
    {
        (void) w;
        drawCube (r, h, Camera (yaw(), pitch(), 4.6f, 184.0f, (float) (x + 80), (float) (y + height / 2 - 5)), true);
        drawWaterfall (r, h, x + 158, y + height - 12);
    }

private:
    const std::atomic<float>* values;
    const Feed& feed;

    static constexpr int cols = 112, slices = 18, trailLength = 24;
    static constexpr uint64_t frameMs = 33, sliceMs = 40, liveMs = 400;
    static constexpr float sliceDx = 2.4f, sliceDy = -3.8f, dbLow = -36.0f, dbHigh = 18.0f, sliceHeight = 64.0f;
    static constexpr hdui::Ink magenta = 0xffd040ff, deep = 0xff22183f, grid = 0xff101828, path = 0xff6a2c8c, edgeNear = 0xff8fb6e8,
                               edgeFar = 0xff27324f, white = 0xfff4f8ff;

    float columnHz[cols] {};
    double columnT[cols] {};
    float columnRate = 0.0f;
    float sliceDb[slices][cols] {};
    float rightDb[cols] {};
    int sliceHead = 0, sliceCount = 0;
    Position trail[trailLength] {};
    int trailHead = 0, trailCount = 0;
    Position pointL, pointR;
    bool splitNow = false, live = false, heard = false, viewMorph = false;
    uint32_t seenBlocks = 0;
    uint64_t lastBlockMs = 0, lastUpdateMs = 0, lastFrameMs = 0, lastSliceMs = 0, swayMs = 0;
    float rate = 48000.0f;
    int shownCube = -1;
    float shownEmphasis = 0.0f, shownRate = 0.0f;
    Position shownRight;
    bool shownSplit = false;
    // MORPH view's landscape, kept while its cube, Freq, Transform and Emphasis stay.
    static constexpr int morphSlices = 12;
    float morphDb[morphSlices][cols] {};
    Position morphAt[morphSlices] {}; // the Freq and Transform each slice was worked out at
    int morphNext = 0, morphCube = -1;
    float morphEmphasis = 0.0f, morphRate = 0.0f;

    // A slow sway while sound runs: the cube turns a little either way and nods, so its depth reads.
    float yaw() const { return -0.55f + 0.2f * std::sin ((float) swayMs * (2.0f * pi / 9000.0f)); }
    float pitch() const { return 0.36f + 0.06f * std::sin ((float) swayMs * (2.0f * pi / 14000.0f)); }

    // Where the filter will be, from the knobs: the point the sound would start from now.
    void restingPoints()
    {
        const Shape shape = (Shape) (int) value (ParamId::lfoShape);
        const double place = feed.lfo.load (std::memory_order_relaxed);
        const float offset = value (ParamId::lfoPhase);
        const uint32_t knobs[3] = { ParamId::morph, ParamId::freq, ParamId::transform };
        const uint32_t depths[3] = { ParamId::lfoMorph, ParamId::lfoFreq, ParamId::lfoTransform };
        float p[3];
        for (uint32_t a = 0; a < 3; ++a)
            p[a] = std::clamp (value (knobs[a]) + lfoSwing * value (depths[a]) * lfoValue (shape, place + (double) (offset * (float) a), a), 0.0f, 1.0f);
        pointL = pointR = { p[0], p[1], p[2] };
        const float spread = value (ParamId::spread);
        splitNow = spread > 0.0f;
        if (splitNow)
        {
            const float half = 0.5f * spreadWidth * spread, centre = std::clamp (p[0], half, 1.0f - half);
            pointL.x = centre - half, pointR.x = centre + half;
        }
    }

    // The LFO's path through the cube over one cycle, from the knobs (none for the random shapes, which have none).
    int orbit (Position* out, int count) const
    {
        const Shape shape = (Shape) (int) value (ParamId::lfoShape);
        const uint32_t knobs[3] = { ParamId::morph, ParamId::freq, ParamId::transform };
        const uint32_t depths[3] = { ParamId::lfoMorph, ParamId::lfoFreq, ParamId::lfoTransform };
        const uint32_t envs[3] = { ParamId::envMorph, ParamId::envFreq, ParamId::envTransform };
        if (shape == Shape::sampleHold || shape == Shape::drift)
            return 0;
        bool any = false;
        for (uint32_t d : depths)
            any |= value (d) != 0.0f;
        if (! any)
            return 0;
        const float offset = value (ParamId::lfoPhase), env = feed.env.load (std::memory_order_relaxed);
        for (int i = 0; i < count; ++i)
        {
            float p[3];
            for (uint32_t a = 0; a < 3; ++a)
                p[a] = std::clamp (value (knobs[a]) + lfoSwing * value (depths[a]) * lfoValue (shape, (double) i / count + (double) (offset * (float) a), a)
                                       + envSwing * value (envs[a]) * (live ? env : 0.0f),
                                   0.0f, 1.0f);
            out[i] = { p[0], p[1], p[2] };
        }
        return count;
    }

    static float depthShade (float depth) { return std::clamp ((depth + 1.6f) / 3.2f, 0.0f, 1.0f); }

    void drawCube (hdui::Raster& r, const hdui::Host& h, const Camera& cam, bool big)
    {
        const hdui::Ink hi = accent (h);
        // The back walls: the face of each axis farther from the eye.
        auto depthOf = [&] (float x, float y, float z) { float sx, sy, d; cam.at (x, y, z, sx, sy, d); return d; };
        const float backX = depthOf (0, 0.5f, 0.5f) > depthOf (1, 0.5f, 0.5f) ? 0.0f : 1.0f;
        const float backY = depthOf (0.5f, 0, 0.5f) > depthOf (0.5f, 1, 0.5f) ? 0.0f : 1.0f;
        const float backZ = depthOf (0.5f, 0.5f, 0) > depthOf (0.5f, 0.5f, 1) ? 0.0f : 1.0f;
        auto seg = [&] (Position a, Position b, hdui::Ink c, int dash = 0) {
            int x0, y0, x1, y1;
            cam.at (a.x, a.y, a.z, x0, y0);
            cam.at (b.x, b.y, b.z, x1, y1);
            r.line (x0, y0, x1, y1, c, dash);
        };
        if (big)
            for (float t : { 0.25f, 0.5f, 0.75f })
            {
                seg ({ backX, t, 0 }, { backX, t, 1 }, grid), seg ({ backX, 0, t }, { backX, 1, t }, grid);
                seg ({ 0, backY, t }, { 1, backY, t }, grid), seg ({ t, backY, 0 }, { t, backY, 1 }, grid);
                seg ({ 0, t, backZ }, { 1, t, backZ }, grid), seg ({ t, 0, backZ }, { t, 1, backZ }, grid);
            }

        // The LFO's path, dotted.
        Position ring[48];
        const int n = orbit (ring, 48);
        for (int i = 0; i < n; ++i)
            seg (ring[i], ring[(i + 1) % n], path, big ? 2 : 1);

        // The edges, brighter nearer.
        static constexpr uint8_t edges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
        auto corner = [] (int c) { return Position { (float) (c & 1), (float) ((c >> 1) & 1), (float) ((c >> 2) & 1) }; };
        for (const auto& e : edges)
        {
            const Position a = corner (e[0]), b = corner (e[1]);
            const float d = depthOf (0.5f * (a.x + b.x), 0.5f * (a.y + b.y), 0.5f * (a.z + b.z));
            seg (a, b, hdui::mix (edgeNear, edgeFar, (int) (depthShade (d) * 256.0f)));
        }

        // The corners, lit by how much of each the filter takes now.
        float w[8];
        cornerWeights (pointL, w);
        for (int c = 0; c < 8; ++c)
        {
            int sx, sy;
            const Position p = corner (c);
            cam.at (p.x, p.y, p.z, sx, sy);
            const float lit = std::sqrt (w[c]);
            r.disc (sx, sy, big ? 1 + (int) (lit * 3.0f) : 1, hdui::mix (colours.dim, hi, (int) (lit * 256.0f)));
        }

        // Lines dropped to the back walls, and where they land.
        if (big)
            for (const Position& foot : { Position { backX, pointL.y, pointL.z }, Position { pointL.x, backY, pointL.z }, Position { pointL.x, pointL.y, backZ } })
            {
                seg (pointL, foot, hdui::mix (colours.bg, hi, 120), 2);
                int sx, sy;
                cam.at (foot.x, foot.y, foot.z, sx, sy);
                r.box (sx - 1, sy, 3, 1, hdui::mix (colours.bg, hi, 170)), r.box (sx, sy - 1, 1, 3, hdui::mix (colours.bg, hi, 170));
            }

        // The trail, fading.
        if (big)
            for (int i = 1; i < trailCount; ++i)
            {
                const Position& a = trail[(trailHead - i + 1 + trailLength) % trailLength];
                const Position& b = trail[(trailHead - i + trailLength) % trailLength];
                seg (b, a, hdui::mix (colours.bg, hi, 220 - 200 * i / trailLength));
            }

        // The point (the right channel's beside it when split), glowing.
        auto glow = [&] (const Position& p, hdui::Ink c) {
            int sx, sy;
            cam.at (p.x, p.y, p.z, sx, sy);
            if (big)
                r.disc (sx, sy, 8, c, 40), r.disc (sx, sy, 5, c, 110);
            r.disc (sx, sy, big ? 3 : 2, c);
            r.disc (sx, sy, big ? 1 : 0, white);
        };
        if (splitNow)
            glow (pointR, magenta);
        glow (pointL, hi);

        // The axes' names, by the corner where all three start.
        if (big)
        {
            int ax, ay, bx, by, cx, cy;
            cam.at (0.5f, 0, 0, ax, ay);
            cam.at (0, 0.5f, 0, bx, by);
            cam.at (1, 0, 0.5f, cx, cy);
            r.text (ax - 17, ay + 5, "MORPH", colours.muted);
            r.text (std::max (2, bx - 34), by - 6, "FREQ", colours.muted);
            r.text (std::min (cx + 6, 150 - hdui::textWidth ("XFORM")), cy + 2, "XFORM", colours.muted);
        }
    }

    // A response as one line: dbLow at the floor, dbHigh at the top.
    void drawCurve (hdui::Raster& r, const float* db, int x, int y, int w, int height, hdui::Ink c) const
    {
        int prev = -1;
        for (int i = 0; i < w; ++i)
        {
            const float v = db[i * (cols - 1) / std::max (1, w - 1)];
            const int top = y + height - 1 - (int) std::lround (std::clamp ((v - dbLow) / (dbHigh - dbLow), 0.0f, 1.0f) * (float) (height - 1));
            if (prev >= 0)
                r.box (x + i, std::min (prev, top), 1, std::abs (prev - top) + 1, c);
            prev = top;
        }
    }

    hdui::Ink sliceInk (float t, hdui::Ink front) const
    {
        return t < 0.5f ? hdui::mix (front, magenta, (int) (t * 2.0f * 256.0f)) : hdui::mix (magenta, deep, (int) ((t - 0.5f) * 2.0f * 256.0f));
    }

    // One slice of the waterfall at depth `t` (0 front, 1 back). Slices are drawn front to back, each only where it
    // rises above everything nearer: `horizon` holds the highest row drawn in each column so far. So each pixel is
    // written once, and what is behind a slice stays hidden under it.
    void drawSlice (hdui::Raster& r, int* horizon, const float* db, int x0, int base, float t, hdui::Ink line, hdui::Ink fill) const
    {
        const int ox = x0 + (int) std::lround (t * (slices - 1) * sliceDx), oy = base + (int) std::lround (t * (slices - 1) * sliceDy);
        int prev = -1;
        for (int i = 0; i < cols; ++i)
        {
            const float v = std::clamp ((db[i] - dbLow) / (dbHigh - dbLow), 0.0f, 1.04f);
            const int top = std::max (0, oy - (int) std::lround (v * sliceHeight)), x = ox + i;
            if (x < 0 || x >= hdui::width)
                continue;
            int& h = horizon[x];
            uint32_t* column = r.px + x;
            for (int y = top + 1, floor = std::min ({ h, oy + 1, hdui::height }); y < floor; ++y) // under the line
                column[y * r.stride] = fill;
            const int a = prev >= 0 ? std::min (prev, top) : top, b = prev >= 0 ? std::max (prev, top) : top;
            for (int y = a, end = std::min ({ b + 1, h, hdui::height }); y < end; ++y) // the line, from the last column's
                column[y * r.stride] = line;
            h = std::min (h, a);
            prev = top;
        }
    }

    void drawWaterfall (hdui::Raster& r, const hdui::Host& h, int x0, int base)
    {
        const hdui::Ink hi = accent (h);
        const hdui::Ink bg = colours.bg;
        int horizon[hdui::width];
        std::fill (horizon, horizon + hdui::width, hdui::height);
        if (viewMorph)
        {
            // MORPH: the Morph axis from 0 (front) to 1 (back), the response now lit where it is.
            // The landscape follows Freq and Transform a few slices a frame (all of it within four frames), so a
            // moving point costs a few responses a frame, not twelve; a new cube, Emphasis or rate redraws it at once.
            const int cube = (int) value (ParamId::cube);
            const float emphasis = value (ParamId::emphasis);
            const bool reshaped = cube != morphCube || emphasis != morphEmphasis || rate != morphRate;
            const int start = morphNext;
            for (int k = 0, budget = reshaped ? morphSlices : 3; k < morphSlices && budget > 0; ++k)
            {
                const int s = (start + k) % morphSlices;
                if (! reshaped && morphAt[s].y == pointL.y && morphAt[s].z == pointL.z)
                    continue;
                Design d;
                design (cubeTable (cube), { (float) s / (morphSlices - 1), pointL.y, pointL.z }, emphasis, rate, d);
                for (int i = 0; i < cols; ++i)
                    morphDb[s][i] = responseDbAt (d, columnT[i]);
                morphAt[s] = { 0.0f, pointL.y, pointL.z };
                morphNext = (s + 1) % morphSlices;
                --budget;
            }
            morphCube = cube, morphEmphasis = emphasis, morphRate = rate;
            const float now = pointL.x;
            bool nowDrawn = false;
            for (int s = 0; s < morphSlices; ++s) // front to back, the response now where it sits among them
            {
                const float t = (float) s / (morphSlices - 1);
                if (! nowDrawn && now <= t)
                {
                    drawSlice (r, horizon, sliceDb[sliceHead], x0, base, now, white, hdui::mix (bg, hi, 70));
                    nowDrawn = true;
                }
                const hdui::Ink ink = sliceInk (0.25f + 0.75f * t, hdui::mix (hi, colours.muted, 128));
                drawSlice (r, horizon, morphDb[s], x0, base, t, ink, hdui::mix (bg, ink, 22));
            }
            r.text (x0 + 2, base - 136, "MORPH", colours.muted);
            r.text (x0 + 2 + 36, base - 136, "->", colours.dim);
        }
        else
        {
            // TIME: newest in front.
            for (int age = 0; age < sliceCount; ++age)
            {
                const float t = (float) age / (slices - 1);
                const hdui::Ink ink = age == 0 ? hi : sliceInk (t, hi);
                drawSlice (r, horizon, sliceDb[(sliceHead - age + slices) % slices], x0, base, t, ink, hdui::mix (bg, ink, age == 0 ? 40 : 22));
            }
            if (splitNow)
                drawSliceLine (r, rightDb, x0, base, magenta);
            r.text (x0 + 2, base - 136, "TIME", colours.muted);
        }
        // The front's floor, its 0 dB and its frequencies.
        r.box (x0, base + 1, cols, 1, colours.rule);
        const int zero = base - (int) std::lround ((0.0f - dbLow) / (dbHigh - dbLow) * sliceHeight);
        for (int i = 0; i < cols; i += 4)
            r.dot (x0 + i, zero, hdui::mix (bg, colours.text, 90));
        for (float hz : { 100.0f, 1000.0f, 10000.0f })
        {
            const int at = x0 + (int) std::lround (std::log (hz / 20.0f) / std::log (1000.0f) * (cols - 1));
            r.box (at, base + 1, 1, 3, colours.muted);
            const char* label = hz < 500.0f ? "100" : (hz < 5000.0f ? "1K" : "10K");
            r.text (at - hdui::textWidth (label) / 2, base + 3, label, colours.dim);
        }
    }

    // The right channel's response, a line over the front slice.
    void drawSliceLine (hdui::Raster& r, const float* db, int x0, int base, hdui::Ink c) const
    {
        for (int i = 0; i < cols; i += 2)
        {
            const float v = std::clamp ((db[i] - dbLow) / (dbHigh - dbLow), 0.0f, 1.04f);
            r.box (x0 + i, base - (int) std::lround (v * sliceHeight), 1, 1, c);
        }
    }

    // The groups. CUBE draws itself; the rest list the knobs.
    static constexpr hdui::Field filter[] = {
        { ParamId::cube, "CUBE" }, { ParamId::morph, "MORPH" }, { ParamId::freq, "FREQ" }, { ParamId::transform, "TRANSFORM" },
        { ParamId::emphasis, "EMPHASIS" }, { ParamId::glide, "GLIDE" },
    };
    static constexpr hdui::Field lfo[] = {
        { ParamId::lfoSync, "SYNC" }, { ParamId::lfoRate, "RATE" }, { ParamId::lfoShape, "SHAPE" }, { ParamId::lfoPhase, "PHASE" },
        { ParamId::lfoMorph, "LFO>MORPH" }, { ParamId::lfoFreq, "LFO>FREQ" }, { ParamId::lfoTransform, "LFO>XFORM" },
    };
    static constexpr hdui::Field envelope[] = {
        { ParamId::envGain, "ENV GAIN" }, { ParamId::envAttack, "ATTACK" }, { ParamId::envRelease, "RELEASE" },
        { ParamId::envMorph, "ENV>MORPH" }, { ParamId::envFreq, "ENV>FREQ" }, { ParamId::envTransform, "ENV>XFORM" },
    };
    static constexpr hdui::Field out[] = {
        { ParamId::drive, "DRIVE" }, { ParamId::mix, "MIX" }, { ParamId::output, "OUTPUT" }, { ParamId::spread, "SPREAD" },
    };
    static constexpr hdui::Group groups[] = {
        { "CUBE", nullptr, 0 }, { "FILTER", filter, 6 }, { "LFO", lfo, 7 }, { "ENV", envelope, 6 }, { "OUT", out, 4 },
    };
};
} // namespace cortex
