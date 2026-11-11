// Widescreen and frame interpolation: what RT64 needs from a Turok game's own display list (docs/widescreen.md).
//
// RT64 widens the 3D by itself. What it can't know is the game's 2D: the HUD, which it keeps in the middle 4:3 unless
// the display list anchors it to the window's sides, and the border the games draw round the screen, whose fill
// rectangles at the old 4:3 edges show up as lines down the widened picture. A game tells the engine three places in
// its frame (frame_begin, frame_end, and the HUD between hud_begin and hud_end), and the engine writes RT64's extended
// GBI into the game's display list there: around the HUD, a scissor as wide as the window and automatic anchoring
// (runtime-patches/rt64-3-auto-anchor-hud.patch), and at the end of the frame, the border's side rectangles taken out.
// Both only while the picture is wider than 4:3. After PaxDino's src/game/widescreen.cpp
// (https://github.com/wootbeer/PaxDino, GPL-3.0), made to work from the frame's own commands instead of Rage Wars'
// addresses.
#include <cstdint>

#include "recomp.h"
#include "ultramodern/config.hpp"
#include "turok_engine.h"

namespace {
    uint32_t head_var = 0;      // the game's display list head, a pointer in RDRAM
    uint32_t frame_start = 0;   // the head where this frame's display list starts
    bool hud_open = false;      // hud_begin wrote its commands, so hud_end closes them

    constexpr uint32_t kOriginLeft = 0x0;   // G_EX_ORIGIN_LEFT
    constexpr uint32_t kOriginRight = 0x400; // G_EX_ORIGIN_RIGHT

    // Display list commands, as the game's own code writes them.
    struct Command {
        uint32_t w0, w1;
    };

    Command read(uint8_t* rdram, uint32_t address) {
        return { (uint32_t)MEM_W(0, (gpr)(int32_t)address), (uint32_t)MEM_W(4, (gpr)(int32_t)address) };
    }

    // Commands written at the game's head, which then moves past them.
    struct Writer {
        uint8_t* rdram;
        uint32_t head;
        void cmd(uint32_t w0, uint32_t w1) {
            MEM_W(0, (gpr)(int32_t)head) = (int32_t)w0;
            MEM_W(4, (gpr)(int32_t)head) = (int32_t)w1;
            head += 8;
        }
        void enable() {
            cmd(0xE0525464, 0x10000064); // gEXEnable: the extended GBI on opcode 0x64 (a G_SPNOOP with RT64's magic)
        }
        void finish() {
            MEM_W(0, (gpr)(int32_t)head_var) = (int32_t)head;
        }
    };

    // A frame's display list is no longer than this: a head that moved further, or backwards, isn't this frame's.
    constexpr uint32_t kMaxFrameBytes = 0x40000;

    bool head_valid(uint8_t* rdram, uint32_t* head) {
        if (head_var == 0 || frame_start == 0) {
            return false;
        }
        *head = (uint32_t)MEM_W(0, (gpr)(int32_t)head_var);
        return *head >= frame_start && *head - frame_start <= kMaxFrameBytes && (*head & 7) == 0;
    }

    // The screen, from the widest G_SETSCISSOR the frame has set so far: the games set a full-screen one before they
    // draw. Its lower right corner, in 10.2 fixed point, is the screen's last pixel.
    bool screen_scissor(uint8_t* rdram, uint32_t end, uint32_t* lrx, uint32_t* lry) {
        *lrx = 0;
        *lry = 0;
        for (uint32_t a = frame_start; a < end; a += 8) {
            Command c = read(rdram, a);
            if ((c.w0 >> 24) != 0xED) {
                continue;
            }
            uint32_t x = (c.w1 >> 12) & 0xFFF, y = c.w1 & 0xFFF;
            if (x > *lrx) {
                *lrx = x;
                *lry = y;
            }
        }
        return *lrx > 0 && *lry > 0;
    }
}

bool turok::widescreen_active() {
    return ultramodern::renderer::get_graphics_config().ar_option == ultramodern::renderer::AspectRatio::Expand;
}

void turok::set_display_list_head(uint32_t address) {
    head_var = address;
}

// RT64's frame interpolation (Framerate above the game's own rate). By default (automatic components) RT64 stops
// interpolating a matrix's position on any frame where its movement jumps tenfold over the last, so things moving
// unevenly flip between smoothed and snapped frames. At the start of each frame the base of RT64's matrix-group stack
// (reset every frame) gets decomposed components with automatic matching: position, rotation, scale, skew,
// perspective and tiles always interpolated, vertices and texture coordinates not, look-at automatic.
static uint32_t base_group_params() {
    const uint32_t interpolate = 1, autos = 2;
    return (1u << 2) /* decompose */ | (interpolate << 3) | (interpolate << 5) | (interpolate << 7) |
        (interpolate << 9) | (interpolate << 11) | (interpolate << 15) /* tiles */ | (1u << 17) /* order auto */ |
        (autos << 24);
}

extern "C" void turok_frame_begin(uint8_t* rdram) {
    frame_start = head_var != 0 ? (uint32_t)MEM_W(0, (gpr)(int32_t)head_var) : 0;
    hud_open = false;
    if (frame_start == 0) {
        return;
    }
    Writer dl{ rdram, frame_start };
    dl.enable();
    dl.cmd(0x6400000C, 0xFFFFFFFF); // gEXMatrixGroup(G_EX_ID_AUTO, no push): replaces the base entry
    dl.cmd(base_group_params(), 0);
    dl.finish();
}

// The border: a vertical G_FILLRECT at most 4 pixels wide, a quarter of the screen high or more, at its left or right
// edge, becomes a no-op.
extern "C" void turok_frame_end(uint8_t* rdram) {
    uint32_t end;
    if (!turok::widescreen_active() || !head_valid(rdram, &end)) {
        return;
    }
    uint32_t lrx, lry;
    if (!screen_scissor(rdram, end, &lrx, &lry)) {
        return;
    }
    constexpr uint32_t kEdge = 4 * 4; // 10.2
    for (uint32_t a = frame_start; a < end; a += 8) {
        Command c = read(rdram, a);
        if ((c.w0 >> 24) != 0xF6) { // G_FILLRECT
            continue;
        }
        uint32_t r_lrx = (c.w0 >> 12) & 0xFFF, r_lry = c.w0 & 0xFFF;
        uint32_t r_ulx = (c.w1 >> 12) & 0xFFF, r_uly = c.w1 & 0xFFF;
        if (r_lrx < r_ulx || r_lry < r_uly) {
            continue;
        }
        bool vertical = (r_lrx - r_ulx) <= kEdge && (r_lry - r_uly) * 4 >= lry;
        bool at_side = r_ulx <= kEdge || r_lrx + kEdge >= lrx;
        if (vertical && at_side) {
            MEM_W(0, (gpr)(int32_t)a) = 0; // G_SPNOOP
            MEM_W(4, (gpr)(int32_t)a) = 0;
        }
    }
}

// Before the HUD: the scissor pushed and set to the whole window (to the game's own full-screen scissor's last row,
// and a pixel short of the right edge as it is: RT64 writes back as many rows as were drawn to), and automatic
// anchoring on, which puts each HUD rectangle at the side it's nearest by where it sits on the screen.
extern "C" void turok_hud_begin(uint8_t* rdram) {
    hud_open = false;
    uint32_t head;
    if (!turok::widescreen_active() || !head_valid(rdram, &head)) {
        return;
    }
    uint32_t lrx, lry;
    if (!screen_scissor(rdram, head, &lrx, &lry)) {
        return;
    }
    uint32_t width = (lrx + 4) & ~3u; // the screen's width in 10.2: its last pixel's left edge plus one
    int32_t right = (int32_t)lrx - (int32_t)width;
    Writer dl{ rdram, head };
    dl.enable();
    dl.cmd(0x64000017, 0); // gEXPushScissor
    dl.cmd(0x64000005, (kOriginLeft << 2) | (kOriginRight << 14)); // gEXSetScissor(G_SC_NON_INTERLACE, LEFT, RIGHT,
    dl.cmd(0, (((uint32_t)right & 0xFFFF) << 16) | (lry & 0xFFFF)); //   0, 0, right, lry)
    dl.cmd(0x64000034, 1); // gEXSetAutoAnchor(on)
    dl.finish();
    hud_open = true;
}

extern "C" void turok_hud_end(uint8_t* rdram) {
    if (!hud_open) {
        return;
    }
    hud_open = false;
    uint32_t head;
    if (!head_valid(rdram, &head)) {
        return;
    }
    Writer dl{ rdram, head };
    dl.enable();
    dl.cmd(0x64000034, 0); // gEXSetAutoAnchor(off)
    dl.cmd(0x64000018, 0); // gEXPopScissor
    dl.finish();
}
