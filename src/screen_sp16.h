// SP-16 touchscreen editor. Draw directly from the filter and sequencer state at the host canvas size.
#pragma once

#include "screen.h"
#include "sixteen_screen.h"
#include "ui/sp16_font.h"

namespace cortex::sp16
{
using Ink = hdui::Ink;
constexpr Ink bg = 0xff090e1b, face = 0xff0c1120, tile = 0xff141e33, selected = 0xff25304b;
constexpr Ink border = 0xff354363, grid = 0xff243050, white = 0xffeff5ff, muted = 0xffa8b7d3;
constexpr Ink cyan = 0xff42ddff, violet = 0xffbfa4ff, magenta = 0xffd040ff;

struct Point
{
    float x, y;
};

struct Canvas
{
    const sixteen_screen_surface_t &s;

    void box(int x, int y, int w, int h, Ink colour) const
    {
        for (int j = y; j < y + h; ++j)
            std::fill_n(s.pixels + j * s.stride + x, w, colour);
    }
    void pixel(int x, int y, Ink color, int alpha) const
    {
        if ((unsigned)x >= (unsigned)s.width || (unsigned)y >= (unsigned)s.height || alpha <= 0)
            return;
        auto &dst = s.pixels[y * s.stride + x];
        dst = hdui::mix(dst, color, std::min(alpha, 256));
    }

    // Two covered pixels per column (or row) keep oblique traces smooth on the LCD.
    void line(Point a, Point b, Ink color) const
    {
        const bool steep = std::abs(b.y - a.y) > std::abs(b.x - a.x);
        if (steep)
        {
            std::swap(a.x, a.y);
            std::swap(b.x, b.y);
        }
        if (a.x > b.x)
            std::swap(a, b);
        const float slope = b.x - a.x > .001f ? (b.y - a.y) / (b.x - a.x) : 0.f;
        for (int x = (int)std::lround(a.x), last = (int)std::lround(b.x); x <= last; ++x)
        {
            const float y = a.y + (x - a.x) * slope;
            const int iy = (int)std::floor(y), fraction = (int)((y - iy) * 256.f);
            if (steep)
            {
                pixel(iy, x, color, 256 - fraction);
                pixel(iy + 1, x, color, fraction);
            }
            else
            {
                pixel(x, iy, color, 256 - fraction);
                pixel(x, iy + 1, color, fraction);
            }
        }
    }

    void disc(Point at, int radius, Ink color) const
    {
        for (int y = -radius; y <= radius; ++y)
            for (int x = -radius; x <= radius; ++x)
                pixel((int)std::lround(at.x) + x, (int)std::lround(at.y) + y, color,
                      (int)(256.f * std::clamp(radius + .5f - std::sqrt((float)(x * x + y * y)), 0.f, 1.f)));
    }

    // Fill a projected cube face with a scanline tint. Its four corners stay inside the scope.
    void faceGradient(const std::array<Point, 4> &points, Ink upper, Ink lower) const
    {
        float top = points[0].y, bottom = top;
        for (const auto p : points)
        {
            top = std::min(top, p.y);
            bottom = std::max(bottom, p.y);
        }
        for (int y = (int)std::ceil(top); y < bottom; ++y)
        {
            float left = (float)s.width, right = 0.f;
            for (int i = 0; i < 4; ++i)
            {
                const auto a = points[i], b = points[(i + 1) % 4];
                if ((a.y <= y && b.y > y) || (b.y <= y && a.y > y))
                {
                    const float x = a.x + (b.x - a.x) * (y - a.y) / (b.y - a.y);
                    left = std::min(left, x);
                    right = std::max(right, x);
                }
            }
            const int x = (int)std::ceil(left), w = (int)std::ceil(right) - x;
            if (w > 0)
                box(x, y, w, 1, hdui::mix(upper, lower, (int)(256 * (y - top) / (bottom - top))));
        }
    }

    static int width(const char *str, int size)
    {
        int w = 0;
        for (; *str; ++str)
        {
            const auto ch = (unsigned char)*str;
            w += font::glyphs[size][(ch >= 32 && ch < 127 ? ch : '?') - 32].advance;
        }
        return w;
    }

    // The FX slot presents 285 canvas rows in 232.5 LCD pixels. Place the original
    // Barlow coverage rows on the LCD grid so letters keep their aspect and baseline.
    void text(int x, int y, int right, const char *str, Ink colour, int size) const
    {
        constexpr float canvasRowsPerPixel = SIXTEEN_SCREEN_HEIGHT / 232.5f;
        const int textTop = (int)std::lround(y / canvasRowsPerPixel);
        for (; *str; ++str)
        {
            const auto ch = (unsigned char)*str;
            const auto &g = font::glyphs[size][(ch >= 32 && ch < 127 ? ch : '?') - 32];
            if (x + g.advance > right)
                return;
            for (int j = 0; j < g.h; ++j)
            {
                const int row = textTop + g.y + j;
                const int first = std::max(0, (int)std::lround(row * canvasRowsPerPixel));
                const int end = std::min((int)s.height, (int)std::lround((row + 1) * canvasRowsPerPixel));
                for (int i = 0; i < g.w; ++i)
                {
                    const int dx = x + g.x + i;
                    if (dx < 0 || dx >= right)
                        continue;
                    const int k = j * g.w + i;
                    const auto byte = font::masks[g.offset + k / 2];
                    const int a = (k & 1) ? byte & 15 : byte >> 4;
                    if (a)
                        for (int dy = first; dy < end; ++dy)
                        {
                            auto &dst = s.pixels[dy * s.stride + dx];
                            dst = hdui::mix(dst, colour, a == 15 ? 256 : a * 17);
                        }
                }
            }
            x += g.advance;
        }
    }

};

struct Editor
{
    // The 780 x 285 extension is presented in a 780 x 232 physical-pixel FX viewport.
    static constexpr int navX = 218, tabW = 70, navH = 54;
    static constexpr int scopeW = 500, cubeW = 216, controlsX = 506, colW = 137;
    static constexpr int top = 58, listTop = 101, listRowH = 43, visibleRows = 4;
    enum class Target
    {
        none,
        control,
        cube,
        plot,
        list
    };
    Target target = Target::none;
    int card = -1, downX = 0, downY = 0, listOrigin = 0, startRow = 0;
    uint32_t dragId = hdui::noParam;
    float initial = 0.f, initialMorph = 0.f, initialFreq = 0.f;
    bool moved = false, dirty = true;
    int lastGroup = -1, lastFrame = -1;
    std::array<uint32_t, 6> lastIds{};
    std::array<float, 6> lastValues{};
    wordmark::Plate logo{};

    void drawLogo(const Canvas &c, int frame)
    {
        logo = wordmark::fixed;
        wordmark::drawMotion(logo.data(), wordmark::width, frame);
        // The source is the original chrome artwork. Restore its physical aspect in the FX viewport.
        for (int y = 0; y < 35; ++y)
        {
            const float sy = y * (wordmark::height - 1) / 34.f;
            const int iy = (int)sy;
            for (int x = 0; x < 202; ++x)
            {
                const float sx = x * (wordmark::width - 1) / 201.f;
                const int ix = (int)sx, jx = std::min(ix + 1, wordmark::width - 1);
                const Ink *a = logo.data() + iy * wordmark::width;
                const Ink *b = logo.data() + std::min(iy + 1, wordmark::height - 1) * wordmark::width;
                const int fx = (int)((sx - ix) * 256), fy = (int)((sy - iy) * 256);
                const Ink p = hdui::mix(hdui::mix(a[ix], a[jx], fx), hdui::mix(b[ix], b[jx], fx), fy);
                c.s.pixels[(9 + y) * c.s.stride + 8 + x] = p;
            }
        }
    }

    void navigation(const Canvas &c, Screen &screen) const
    {
        c.box(0, 0, 780, navH, wordmark::background);
        c.box(0, navH - 1, 780, 1, border);
        for (int i = 0; i < 8; ++i)
        {
            const int x = navX + i * tabW;
            const bool active = i == screen.groupIndex();
            c.box(x, 6, tabW - 3, 42, active ? cyan : face);
            const char *name = screen.groupName(i);
            c.text(x + (tabW - 3 - Canvas::width(name, 1)) / 2, 18, x + tabW - 4, name, active ? bg : muted, 1);
        }
    }

    void cube(const Canvas &c, const Screen &screen) const
    {
        const Camera camera(screen.yaw(), screen.pitch(), 4.9f, 195.f, 107.f, 168.f);
        auto project = [&](float x, float y, float z)
        {
            Point p;
            float depth;
            camera.at(x, y, z, p.x, p.y, depth);
            p.y = 165.f + 1.22f * (p.y - 165.f);
            return p;
        };
        c.faceGradient({project(0, 0, 1), project(1, 0, 1), project(1, 1, 1), project(0, 1, 1)}, 0xff303954,
                       0xff182337);
        c.faceGradient({project(0, 0, 0), project(0, 0, 1), project(0, 1, 1), project(0, 1, 0)}, 0xff283a51,
                       0xff131c30);
        c.faceGradient({project(0, 0, 0), project(1, 0, 0), project(1, 0, 1), project(0, 0, 1)}, 0xff17233a,
                       0xff30314f);
        // Subdivisions on the floor and the two far planes establish scale through the cube.
        for (int i = 0; i <= 6; ++i)
        {
            const float t = i / 6.f;
            c.line(project(t, 0, 0), project(t, 0, 1), grid);
            c.line(project(0, 0, t), project(1, 0, t), grid);
            c.line(project(t, 0, 1), project(t, 1, 1), grid);
            c.line(project(0, t, 1), project(1, t, 1), grid);
            c.line(project(0, t, 0), project(0, t, 1), grid);
            c.line(project(0, 0, t), project(0, 1, t), grid);
        }
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j)
            {
                c.line(project(0, i, j), project(1, i, j), j ? border : Screen::edgeNear);
                c.line(project(i, 0, j), project(i, 1, j), j ? border : Screen::edgeNear);
                c.line(project(i, j, 0), project(i, j, 1), 0xff796c98);
            }
        const auto pos = screen.pointL;
        const auto p = project(pos.x, pos.y, pos.z);
        c.line(project(pos.x, 0, pos.z), p, 0xff3b788d);
        for (int i = screen.trailCount - 1; i > 0; --i)
        {
            const auto &a = screen.trail[(screen.trailHead - i + Screen::trailLength) % Screen::trailLength];
            const auto &b = screen.trail[(screen.trailHead - i + 1 + Screen::trailLength) % Screen::trailLength];
            c.line(project(a.x, a.y, a.z), project(b.x, b.y, b.z), hdui::mix(grid, cyan, 210 - i * 6));
        }
        if (screen.splitNow)
            c.disc(project(screen.pointR.x, screen.pointR.y, screen.pointR.z), 4, magenta);
        c.disc(p, 9, 0xff15435b);
        c.disc(p, 5, cyan);
        c.disc(p, 2, white);
        c.text(12, top + 9, cubeW, "Morph space", white, 1);
        char sequence[32];
        screen.sequenceStatus(sequence, sizeof sequence, 14);
        c.text(std::max(105, cubeW - 9 - Canvas::width(sequence, 0)), top + 12, cubeW - 5, sequence, muted, 0);
        const char *axes[] = {"M", "F", screen.drives ? "D" : "T"};
        for (int i = 0; i < 3; ++i)
        {
            const int x = 12 + i * 66, mid = x + 32, y = top + 39;
            const int delta = (int)std::lround(17 * std::clamp(screen.controlNow[i], -1.f, 1.f));
            c.text(x, y - 4, x + 14, axes[i], muted, 0);
            c.box(mid - 18, y, 37, 2, border);
            c.box(std::min(mid, mid + delta), y, std::max(1, std::abs(delta)), 2, violet);
            c.box(mid, y - 2, 1, 6, muted);
        }
        auto axis = [&](Point pt, int dx, int dy, const char *name)
        { c.text((int)pt.x + dx, (int)pt.y + dy, cubeW, name, violet, 0); };
        axis(project(1, 0, 0), 6, -4, "X");
        axis(project(0, 1, 0), -13, -6, "Y");
        axis(project(1, 0, 1), 7, 0, "Z");
        char filter[64];
        std::snprintf(filter, sizeof filter, "F%02d   %s", screen.slotNow, screen.store.name(screen.slotNow));
        c.text(12, 259, cubeW - 5, filter, muted, 1);
    }

    void response(const Canvas &c, const Screen &screen) const
    {
        constexpr int x = cubeW + 15, floor = 244, height = 78, depthX = 56, depthY = 59;
        constexpr int width = scopeW - x - depthX - 10;
        const bool landscape = screen.morphView() && screen.groupIndex() == Screen::cubeGroup;
        c.text(x, top + 9, scopeW, "Filter response", white, 1);
        const char *mode = landscape ? "MORPH" : "TIME";
        const int chip = Canvas::width(mode, 0) + 14;
        c.box(scopeW - chip - 7, top + 7, chip, 21, selected);
        c.text(scopeW - chip, top + 12, scopeW - 7, mode, violet, 0);
        const char *meterNames[] = {"IN", "DIST", "OUT"};
        for (int i = 0; i < 3; ++i)
        {
            const int left = x + i * 85, w = 46, bar = left + 31;
            c.text(left, top + 31, bar - 1, meterNames[i], muted, 0);
            for (int ch = 0; ch < 2; ++ch)
            {
                const float peak = screen.meter[2 * i + ch];
                const float scale = i == 1 ? peak / 3.f : (peak > 0.f ? (20.f * std::log10(peak) + 48.f) / 54.f : 0.f);
                const int active = (int)std::lround(w * std::clamp(scale, 0.f, 1.f));
                const int y = top + 32 + ch * 5;
                c.box(bar, y, w, 3, border);
                const auto color =
                    i == 1 ? (peak > 1.f ? Screen::red : Screen::yellow)
                           : (peak > 1.f ? Screen::red : (scale > 42.f / 54.f ? Screen::yellow : Screen::green));
                c.box(bar, y, active, 3, color);
            }
        }
        for (float u : {.233f, .566f, .900f, 1.f})
            c.line({x + width * u, floor}, {x + width * u + depthX, floor - depthY}, grid);
        c.box(x + depthX, floor - depthY, width, 1, grid);

        // Front-to-back occlusion, depth colours and right-channel overlay follow Cortex's waterfall.
        // Before history fills, extend the oldest response back to the horizon at its held setting.
        std::array<int, scopeW> horizon;
        horizon.fill(floor + 1);
        auto responseY = [&](const float *db, int i, int base)
        {
            const float fi = (float)i * (Screen::cols - 1) / width;
            const int a = (int)fi, b = std::min(a + 1, Screen::cols - 1);
            const float v = db[a] + (db[b] - db[a]) * (fi - a);
            return base - (int)std::lround(
                              height * std::clamp((v - Screen::dbLow) / (Screen::dbHigh - Screen::dbLow), 0.f, 1.04f));
        };
        auto slice = [&](const float *db, float depth, Ink ink, Ink fill)
        {
            const int ox = x + (int)std::lround(depth * depthX), base = floor - (int)std::lround(depth * depthY);
            int prev = responseY(db, 0, base);
            for (int i = 0; i <= width; ++i)
            {
                const int col = ox + i, y = responseY(db, i, base), limit = horizon[col];
                const int begin = std::min(y, prev), end = std::min(limit, std::max(y, prev) + 1);
                for (int row = y + 1; row < std::min(limit, base + 1); ++row)
                    c.s.pixels[row * c.s.stride + col] = fill;
                for (int row = begin; row < end; ++row)
                    c.s.pixels[row * c.s.stride + col] = ink;
                horizon[col] = std::min(limit, begin);
                prev = y;
            }
        };
        if (landscape)
        {
            bool drewCurrent = false;
            for (int i = 0; i < Screen::morphSlices; ++i)
            {
                const float t = (float)i / (Screen::morphSlices - 1);
                if (!drewCurrent && screen.pointL.x <= t)
                {
                    slice(screen.sliceDb[screen.sliceHead], screen.pointL.x, white, hdui::mix(bg, cyan, 70));
                    drewCurrent = true;
                }
                const auto ink = screen.sliceInk(.25f + .75f * t, hdui::mix(cyan, muted, 128));
                slice(screen.morphDb[i], t, ink, hdui::mix(bg, ink, 22));
            }
        }
        else if (screen.sliceCount)
        {
            for (int age = 0; age < Screen::slices; ++age)
            {
                const float t = (float)age / (Screen::slices - 1);
                const int index =
                    (screen.sliceHead - std::min(age, screen.sliceCount - 1) + Screen::slices) % Screen::slices;
                const auto ink = screen.sliceInk(t, cyan);
                slice(screen.sliceDb[index], t, ink, hdui::mix(bg, ink, age == 0 ? 40 : 22));
            }
            if (screen.splitNow)
                for (int i = 0; i <= width; i += 2)
                    c.pixel(x + i, responseY(screen.rightDb, i, floor), magenta, 256);
        }
        for (int i = 0; i < 3; ++i)
        {
            constexpr const char *labels[] = {"100 Hz", "1k", "10k"};
            constexpr float decades[] = {.233f, .566f, .900f};
            const int tx = x + (int)(width * decades[i]);
            c.box(tx, floor + 1, 1, 5, border);
            c.text(tx - Canvas::width(labels[i], 0) / 2, 254, scopeW, labels[i], muted, 0);
        }
    }

    static int rowCount(const Screen &s)
    {
        if (s.groupIndex() != Screen::seqGroup)
            return s.group().count;
        const int q = s.sequenceNow();
        return q ? s.store.length(q) + 2 : 0;
    }
    static int cursor(const Screen &s)
    {
        return s.groupIndex() == Screen::seqGroup ? s.seqCursor : s.cursor[s.current];
    }
    static int firstRow(const Screen &s)
    {
        return std::clamp(cursor(s) - 1, 0, std::max(0, rowCount(s) - visibleRows));
    }
    static void selectRow(Screen &s, int row)
    {
        row = std::clamp(row, 0, std::max(0, rowCount(s) - 1));
        if (s.groupIndex() == Screen::seqGroup)
        {
            s.seqCursor = row;
            s.editingStep = false;
            s.seqRestoreArmed = false;
        }
        else
        {
            s.cursor[s.current] = row;
            s.editing = false;
            s.restoreArmed = false;
        }
    }

    void list(const Canvas &c, Screen &s) const
    {
        const bool seq = s.groupIndex() == Screen::seqGroup;
        const int q = s.sequenceNow(), count = rowCount(s), first = firstRow(s);
        char heading[64];
        if (seq && q)
            std::snprintf(heading, sizeof heading, "%s", sequenceChoices[q]);
        else
            std::snprintf(heading, sizeof heading, "%s", seq ? "Sequence" : s.groupName(s.current));
        c.text(12, top + 10, cubeW - 44, heading, white, 1);
        if (!count)
        {
            c.text(12, 151, cubeW, "Sequence off", white, 2);
            c.text(12, 187, cubeW, "K1 selects sequence", muted, 1);
            return;
        }
        char page[16];
        std::snprintf(page, sizeof page, "%d/%d", cursor(s) + 1, count);
        c.text(cubeW - 10 - Canvas::width(page, 0), top + 13, cubeW, page, violet, 0);
        for (int i = 0; i < visibleRows && i + first < count; ++i)
        {
            const int row = first + i, y = listTop + i * listRowH;
            const bool here = row == cursor(s);
            c.box(5, y, cubeW - 10, listRowH - 3, here ? selected : face);
            if (here)
                c.box(5, y, 3, listRowH - 3, cyan);
            const bool editing = here && (seq ? s.seqEditing() : s.isEditing());
            if (editing)
            {
                c.box(5, y, cubeW - 10, 2, violet);
                c.box(5, y + listRowH - 5, cubeW - 10, 2, violet);
                c.box(cubeW - 7, y, 2, listRowH - 3, violet);
            }
            char value[64]{};
            if (seq)
            {
                const int len = count - 2;
                if (row < len)
                    s.choiceText(here && s.editingStep ? s.choices[s.choice] : s.store.step(q, row), value,
                                 sizeof value);
                else
                    std::snprintf(value, sizeof value, "%s",
                                  row == len ? (len == maxSteps ? "Sequence full" : "+ Add step")
                                             : (s.seqRestoreArmed ? "Confirm restore" : "Restore"));
                char idx[12]{};
                if (row < len)
                    std::snprintf(idx, sizeof idx, "%02d", row + 1);
                const bool playing = s.feed.sequence.load(std::memory_order_relaxed) == q &&
                                     s.feed.step.load(std::memory_order_relaxed) == row;
                c.text(14, y + 11, 42, idx, playing ? cyan : muted, 1);
                c.text(42, y + 10, cubeW - 8, value, here ? white : muted, 1);
            }
            else
            {
                const auto &f = s.group().fields[row];
                hdui::Value v;
                s.read(f.id, v);
                const int left = 13, right = cubeW - 11;
                const int valueX = right - Canvas::width(v.text, 0);
                const bool stacked = valueX < left + Canvas::width(f.label, 0) + 6;
                c.text(left, y + (stacked ? 3 : 12), right, f.label, here ? white : muted, 0);
                c.text(std::max(left, valueX), y + (stacked ? 22 : 12), right, v.text, here ? cyan : muted, 0);
            }
        }
    }

    static int controlHeight(const uint32_t *ids)
    {
        int rows = 3;
        while (rows > 1 && paramIndex(ids[2 * rows - 1]) < 0 && paramIndex(ids[2 * rows - 2]) < 0)
            --rows;
        return (285 - top + 1) / rows;
    }

    void cards(const Canvas &c, const std::array<uint32_t, 6> &ids, const std::array<float, 6> &v) const
    {
        const int h = controlHeight(ids.data());
        c.box(controlsX, top, 780 - controlsX, 285 - top, bg);
        for (int i = 0; i < 6; ++i)
        {
            const int n = paramIndex(ids[i]);
            if (n < 0)
                continue;
            const int x = controlsX + i % 2 * colW, y = top + i / 2 * h, w = colW - 4;
            const bool held = target == Target::control && card == i;
            c.box(x, y, w, h - 4, held ? selected : tile);
            c.box(x + 1, y, w - 2, 1, held ? violet : 0xff263550);
            const auto &param = paramTable[n];
            const Ink accent = i < 3 ? cyan : violet;
            char value[64]{};
            formatParam(param, v[i], value, sizeof value);
            const int labelSize = Canvas::width(param.name, 1) <= w - 13 ? 1 : 0;
            c.text(x + 9, y + 8, x + w - 4, param.name, muted, labelSize);
            const int size = Canvas::width(value, 2) <= w - 16 ? 2 : 1;
            c.text(x + 9, y + h / 2 - 8, x + w - 4, value, white, size);
            const int length = w - 22;
            const float t = std::clamp((v[i] - param.min) / (param.max - param.min), 0.f, 1.f);
            const int thumb = x + 10 + (int)std::lround(t * length), center = x + 10 + length / 2;
            const bool bipolar = param.min < 0 && param.max == -param.min;
            const int start = bipolar ? center : x + 10;
            c.box(x + 10, y + h - 15, length + 1, 2, border);
            c.box(std::min(start, thumb), y + h - 15, std::abs(thumb - start), 2, accent);
            c.box(thumb - 1, y + h - 18, 3, 7, accent);
            if (bipolar)
                c.box(center, y + h - 16, 1, 5, muted);
        }
    }

    bool paint(const sixteen_screen_surface_t &out, Screen &s, const std::atomic<float> *values, bool asked, bool force)
    {
        hdui::Host host;
        host.nowMs = out.now_ms;
        const int group = s.groupIndex();
        std::array<uint32_t, 6> ids;
        std::array<float, 6> v{};
        s.sixKnobs(ids.data());
        for (int i = 0; i < 6; ++i)
            if (const int n = paramIndex(ids[i]); n >= 0)
                v[i] = values[n].load(std::memory_order_relaxed);
        const bool layout = force || group != lastGroup;
        const bool controls = layout || dirty || ids != lastIds || v != lastValues;
        const bool news = s.newBlocks();
        if (layout || controls || asked || news)
            s.wake(host.nowMs);
        const bool graph = controls || asked || (s.due(host.nowMs) && (news || s.animating(host)));
        const int frame = s.titleFrame(host);
        if (!layout && !graph && frame == lastFrame)
            return false;
        Canvas c{out};
        if (layout)
        {
            c.box(0, 0, 780, 285, bg);
            navigation(c, s);
        }
        if (layout || frame != lastFrame)
            drawLogo(c, frame);
        if (graph)
        {
            s.update(host.nowMs);
            if (s.sequenceNow() != s.seqShown)
            {
                s.seqShown = s.sequenceNow();
                s.seqCursor = 0;
                s.editingStep = s.seqRestoreArmed = false;
            }
            s.seqCursor = std::clamp(s.seqCursor, 0, std::max(0, s.store.length(s.seqShown) + 1));
            if (s.morphView() && group == Screen::cubeGroup)
                s.updateMorphSlices();
            c.box(0, top, scopeW, 285 - top, face);
            c.box(cubeW, top + 7, 1, 285 - top - 14, border);
            if (group == Screen::cubeGroup)
                cube(c, s);
            else
                list(c, s);
            response(c, s);
            s.drew(host);
        }
        if (controls || asked)
            cards(c, ids, v);
        lastGroup = group;
        lastIds = ids;
        lastValues = v;
        lastFrame = frame;
        dirty = false;
        return true;
    }

    bool touch(int phase, int x, int y, Screen &s, const std::atomic<float> *values, hdui::Edits &edits)
    {
        const int group = s.groupIndex();
        if (phase == SIXTEEN_TOUCH_DOWN)
        {
            target = Target::none;
            moved = false;
            downX = x;
            downY = y;
            if (y < navH && x >= navX)
            {
                s.selectGroup(std::min(7, (x - navX) / tabW));
                dirty = true;
                return true;
            }
            if (y < top)
                return false;
            if (x >= controlsX)
            {
                uint32_t ids[6];
                s.sixKnobs(ids);
                card = (y - top) / controlHeight(ids) * 2 + (x - controlsX) / colW;
                if (card < 0 || card >= 6 || paramIndex(ids[card]) < 0)
                    return false;
                dragId = ids[card];
                initial = values[paramIndex(dragId)].load(std::memory_order_relaxed);
                target = Target::control;
            }
            else if (x >= scopeW)
                return false;
            else if (x >= cubeW)
                target = Target::plot;
            else if (group == Screen::cubeGroup)
            {
                initialMorph = s.value(ParamId::morph);
                initialFreq = s.value(ParamId::freq);
                target = Target::cube;
            }
            else if (y >= listTop && rowCount(s) > 0)
            {
                listOrigin = firstRow(s);
                startRow = cursor(s);
                target = Target::list;
            }
            else
                return false;
            dirty = true;
            return true;
        }
        if (target == Target::none)
            return false;
        moved |= std::abs(y - downY) + std::abs(x - downX) > 4;
        if (phase == SIXTEEN_TOUCH_MOVE && moved)
        {
            if (target == Target::cube)
            {
                const float morph = std::clamp(initialMorph + (x - downX) / (cubeW - 24.f), 0.f, 1.f);
                const float freq = std::clamp(initialFreq + (downY - y) / (285 - top - 50.f), 0.f, 1.f);
                if (morph != s.value(ParamId::morph))
                    edits.set(ParamId::morph, morph);
                if (freq != s.value(ParamId::freq))
                    edits.set(ParamId::freq, freq);
            }
            else if (target == Target::control)
            {
                const auto &p = paramTable[paramIndex(dragId)];
                edits.set(dragId, clampParam(p, initial + (downY - y) / 130.f * (p.max - p.min)));
            }
            else if (target == Target::list)
                selectRow(s, startRow + (downY - y) / listRowH);
        }
        if (phase == SIXTEEN_TOUCH_UP)
        {
            if (!moved)
            {
                if (target == Target::cube || (target == Target::plot && group == Screen::cubeGroup))
                    s.input(hdui::Key::push, 1, false, edits);
                else if (target == Target::control && dragId >= ParamId::trigInc && dragId <= ParamId::trigReset)
                    edits.press(dragId);
                else if (target == Target::list)
                {
                    const int row = listOrigin + (downY - listTop) / listRowH;
                    if (row < rowCount(s))
                    {
                        if (row == cursor(s))
                            s.input(hdui::Key::push, 1, false, edits);
                        else
                            selectRow(s, row);
                    }
                }
            }
            dirty = true;
            target = Target::none;
        }
        return true;
    }
};
} // namespace cortex::sp16
