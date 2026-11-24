#pragma once

#include "hdslave_tiles.h"
#include "screen.h"

namespace cortex
{

struct TilesAccess
{
    static constexpr int kTileCount = 9; // 9 tiles (3x3 grid: FILTER's 17 rows split across 2 tiles)

    struct TileSpec { int group; int top; const char* title; };
    static constexpr TileSpec kSpecs[kTileCount] = {
        { 0,  0, "01 // 3D CUBE & RESPONSE" },
        { 1,  0, "02 // FILTER (CUBE & REACH)" },
        { 1,  8, "03 // FILTER (FADE / GAIN / DIST)" },
        { 2,  0, "04 // FILTER SEQUENCER" },
        { 3,  0, "05 // TRIGGERS & CLOCK" },
        { 4,  0, "06 // MORPH / FREQ / XFORM" },
        { 5,  0, "07 // LFO MODULATOR" },
        { 6,  0, "08 // ENVELOPE FOLLOWER" },
        { 7,  0, "09 // OUTPUT & MIX" },
    };

    static uint32_t CLAP_ABI count (const clap_plugin_t*, int* out_cols, int* out_rows)
    {
        if (out_cols) *out_cols = 3;
        if (out_rows) *out_rows = 3;
        return kTileCount;
    }

    static const char* CLAP_ABI title (const clap_plugin_t*, uint32_t tile)
    {
        return tile < kTileCount ? kSpecs[tile].title : "";
    }

    static bool CLAP_ABI paint (const clap_plugin_t* plugin, uint32_t tile, const melodee_screen_frame_t* canvas, clap_id knobs_out[3])
    {
        if (tile >= kTileCount || !canvas || !canvas->pixels) return false;
        auto& p = of (plugin);
        const int g = kSpecs[tile].group;
        const int top = kSpecs[tile].top;
        const int savedGroup = p.screen.current;
        const int savedCursor = p.screen.cursor[g];
        const int savedPin = p.screen.pinnedTop;

        p.screen.current = g;
        p.screen.pinnedTop = top;
        const auto& grp = p.screen.groupAt (g);
        if (grp.count > 0)
        {
            const int vis = grp.count >= 9 ? 9 : Screen::rows;
            if (p.screen.cursor[g] < top || p.screen.cursor[g] >= top + vis)
                p.screen.cursor[g] = std::min (top, std::max (0, grp.count - 1));
        }

        melodee_screen_frame_t c = *canvas;
        c.redraw_all = true;
        const hdui::Host h = hostOf (&c);
        p.screen.update (h.nowMs);
        const bool drew = p.screen.paint (h, c.pixels, (int) c.stride, true);
        p.screen.drew (h);
        if (knobs_out)
            screenControls (plugin, knobs_out);

        p.screen.pinnedTop = savedPin;
        p.screen.cursor[g] = savedCursor;
        p.screen.current = savedGroup;
        return drew;
    }

    static bool CLAP_ABI hit_test (const clap_plugin_t* plugin, uint32_t tile, int x, int y, hdslave_tile_hotspot_t* out_spot)
    {
        if (!out_spot || tile >= kTileCount) return false;
        *out_spot = {};
        out_spot->param_id = CLAP_INVALID_ID;
        out_spot->param_id_y = CLAP_INVALID_ID;

        auto& p = of (plugin);
        const int g = kSpecs[tile].group;
        const int top = kSpecs[tile].top;
        const auto& grp = p.screen.groupAt (g);

        const int bodyBottom = Screen::bodyY + Screen::tallH;
        if (y < Screen::bodyY || y >= bodyBottom)
            return false;

        // 2. CUBE screen (tile 0)
        if (g == Screen::cubeGroup)
        {
            if (x < 158)
            {
                // 3D Morphing Cube: 2D drag for Morph (X) & Freq (Y)
                out_spot->kind = HDSLAVE_HOTSPOT_PARAM;
                out_spot->param_id = ParamId::morph;
                out_spot->param_id_y = ParamId::freq;
                out_spot->x = 4;
                out_spot->y = (int16_t) Screen::bodyY;
                out_spot->w = 152;
                out_spot->h = (int16_t) Screen::tallH;
                out_spot->invert_y = true;
                return true;
            }
            else
            {
                // 3D Response Waterfall / Morph Landscape: click toggles TIME / MORPH view, wheel cycles filter
                out_spot->kind = HDSLAVE_HOTSPOT_ACTION;
                out_spot->custom_id = 2000;
                out_spot->x = 160;
                out_spot->y = (int16_t) Screen::bodyY;
                out_spot->w = 156;
                out_spot->h = (int16_t) Screen::tallH;
                return true;
            }
        }

        // 3. SEQ screen (tile 3)
        if (g == Screen::seqGroup)
        {
            if (p.screen.sequenceNow() == 0 || y < Screen::bodyY + 17)
            {
                out_spot->kind = HDSLAVE_HOTSPOT_PARAM;
                out_spot->param_id = ParamId::sequence;
                out_spot->x = 4;
                out_spot->y = (int16_t) Screen::bodyY;
                out_spot->w = (int16_t) (p.screen.sequenceNow() == 0 ? 312 : 180);
                out_spot->h = (int16_t) (p.screen.sequenceNow() == 0 ? Screen::tallH : 16);
                return true;
            }
            // Sequence steps list on left (x: 0..186, y: 62..195)
            if (x < 186)
            {
                const int stepRow = (y - (Screen::bodyY + 18)) / 13;
                out_spot->kind = HDSLAVE_HOTSPOT_ACTION;
                out_spot->custom_id = 3000 + std::max (0, stepRow);
                out_spot->x = 4;
                out_spot->y = (int16_t) (Screen::bodyY + 18 + std::max (0, stepRow) * 13);
                out_spot->w = 180;
                out_spot->h = 13;
                return true;
            }
            // Right mini cube on SEQ (x: 190..318)
            out_spot->kind = HDSLAVE_HOTSPOT_PARAM;
            out_spot->param_id = ParamId::morph;
            out_spot->param_id_y = ParamId::freq;
            out_spot->x = 190;
            out_spot->y = (int16_t) Screen::bodyY;
            out_spot->w = 126;
            out_spot->h = (int16_t) Screen::tallH;
            out_spot->invert_y = true;
            return true;
        }

        // 4. List screens (FILTER, TRIG, KNOBS, LFO, ENV, OUT)
        const int visRows = grp.count >= 9 ? 9 : Screen::rows;
        const int rh = grp.count >= 9 ? 16 : 17;
        const int effTop = std::clamp (top, 0, std::max (0, grp.count - visRows));
        if (x < Screen::listW)
        {
            const int relRow = (y - Screen::bodyY) / rh;
            const int rowIdx = effTop + relRow;
            if (relRow >= 0 && relRow < visRows && rowIdx >= 0 && rowIdx < grp.count)
            {
                const uint32_t id = grp.fields[rowIdx].id;
                out_spot->x = 2;
                out_spot->y = (int16_t) (Screen::bodyY + relRow * rh);
                out_spot->w = (int16_t) (Screen::listW - 2);
                out_spot->h = (int16_t) rh;
                if (id == Screen::copyButton || id == Screen::restoreButton)
                {
                    out_spot->kind = HDSLAVE_HOTSPOT_ACTION;
                    out_spot->custom_id = (int32_t) id;
                    return true;
                }
                hdui::Value v;
                p.screen.read (id, v);
                out_spot->kind = v.kind == hdui::Kind::button ? HDSLAVE_HOTSPOT_BUTTON : HDSLAVE_HOTSPOT_PARAM;
                out_spot->param_id = id;
                return true;
            }
        }
        else
        {
            // Right mini 3D cube beside list: 2D drag for Morph & Freq (or Morph Offset & Freq Offset on FILTER)
            out_spot->kind = HDSLAVE_HOTSPOT_PARAM;
            out_spot->param_id = g == Screen::filterGroup ? ParamId::morphOffset : ParamId::morph;
            out_spot->param_id_y = g == Screen::filterGroup ? ParamId::freqOffset : ParamId::freq;
            out_spot->x = (int16_t) (Screen::listW + 6);
            out_spot->y = (int16_t) Screen::bodyY;
            out_spot->w = (int16_t) (hdui::width - Screen::listW - 8);
            out_spot->h = (int16_t) (bodyBottom - Screen::bodyY);
            out_spot->invert_y = true;
            return true;
        }
        return false;
    }

    static void CLAP_ABI focus (const clap_plugin_t* plugin, uint32_t tile, int x, int y, bool)
    {
        if (tile >= kTileCount) return;
        auto& p = of (plugin);
        const int g = kSpecs[tile].group;
        const int top = kSpecs[tile].top;
        p.screen.current = g;
        const auto& grp = p.screen.groupAt (g);
        if (grp.count > 0 && x < Screen::listW && y >= Screen::bodyY)
        {
            const int visRows = grp.count >= 9 ? 9 : Screen::rows;
            const int rh = grp.count >= 9 ? 16 : 17;
            const int effTop = std::clamp (top, 0, std::max (0, grp.count - visRows));
            const int relRow = (y - Screen::bodyY) / rh;
            const int rowIdx = effTop + relRow;
            if (relRow >= 0 && relRow < visRows && rowIdx >= 0 && rowIdx < grp.count)
                p.screen.cursor[g] = rowIdx;
        }
    }

    static void CLAP_ABI action (const clap_plugin_t* plugin, uint32_t tile, int32_t custom_id, int32_t wheel_delta, bool shift)
    {
        if (tile >= kTileCount) return;
        auto& p = of (plugin);
        p.screen.current = kSpecs[tile].group;
        if (custom_id == (int32_t) Screen::copyButton || custom_id == (int32_t) Screen::restoreButton)
        {
            const auto& grp = p.screen.groupAt (Screen::filterGroup);
            for (int i = 0; i < grp.count; ++i)
                if (grp.fields[i].id == (uint32_t) custom_id)
                    p.screen.cursor[Screen::filterGroup] = i;
            melodee_screen_input_t in { MELODEE_SCREEN_PUSH, 1, shift };
            screenInput (plugin, &in);
        }
        else if (custom_id == 2000)
        {
            p.screen.current = Screen::cubeGroup;
            if (wheel_delta != 0)
            {
                melodee_screen_input_t in { MELODEE_SCREEN_WHEEL, wheel_delta > 0 ? 1 : -1, shift };
                screenInput (plugin, &in);
            }
            else
            {
                melodee_screen_input_t in { MELODEE_SCREEN_PUSH, 1, shift };
                screenInput (plugin, &in);
            }
        }
        else if (custom_id >= 3000)
        {
            p.screen.current = Screen::seqGroup;
            if (wheel_delta != 0)
            {
                if (!p.screen.editingStep)
                {
                    melodee_screen_input_t pushIn { MELODEE_SCREEN_PUSH, 1, false };
                    screenInput (plugin, &pushIn);
                }
                melodee_screen_input_t wheelIn { MELODEE_SCREEN_WHEEL, wheel_delta > 0 ? 1 : -1, shift };
                screenInput (plugin, &wheelIn);
            }
            else
            {
                const int targetRow = custom_id - 3000;
                const int len = p.engine.bank().length (p.screen.sequenceNow());
                const int top = std::clamp (p.screen.seqCursor - 4, 0, std::max (0, len + 2 - 10));
                const int picked = std::clamp (top + targetRow, 0, len + 1);
                p.screen.seqCursor = picked;
                melodee_screen_input_t pushIn { MELODEE_SCREEN_PUSH, 1, shift };
                screenInput (plugin, &pushIn);
            }
        }
    }
};

const clap_plugin_hdslave_tiles_t tilesExtension {
    TilesAccess::count,
    TilesAccess::title,
    TilesAccess::paint,
    TilesAccess::hit_test,
    TilesAccess::focus,
    TilesAccess::action,
    197,
};

} // namespace cortex
