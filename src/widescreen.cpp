// Widescreen: what RT64 needs from a Turok game's own display list and camera (docs/widescreen.md).
//
// RT64 widens the 3D by itself. What it can't know is the game's 2D: the HUD, which it keeps in the middle 4:3 unless
// the display list anchors it to the window's sides, and the border the games draw round the screen, whose fill
// rectangles at the old 4:3 edges show up as lines down the widened picture. A game tells the engine three places in
// its frame (frame_begin, frame_end, and the HUD between hud_begin and hud_end), and the engine writes RT64's extended
// GBI into the game's display list there: around the HUD, a scissor as wide as the window and automatic anchoring
// (runtime-patches/rt64-3-auto-anchor-hud.patch), and at the end of the frame, the border's side rectangles taken out.
// From its camera, the matrix its culling reads, which the engine widens. All only while the picture is wider than
// 4:3. After PaxDino's src/game/widescreen.cpp (https://github.com/wootbeer/PaxDino, GPL-3.0), made to work from the
// frame's own commands instead of Rage Wars' addresses.
#include <cstdint>
#include <cstring>

#include "recomp.h"
#include "ultramodern/config.hpp"
#include "turok_engine.h"

namespace {
    uint32_t head_var = 0;      // the game's display list head, a pointer in RDRAM
    uint32_t hook_opcode = 0xE0; // the microcode's no-op, which RT64's extended GBI is switched on with
    uint32_t end_opcode = 0xDF;  // the microcode's G_ENDDL
    uint32_t dl_opcode = 0xDE;   // the microcode's G_DL
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
            cmd((hook_opcode << 24) | 0x525464, 0x10000064); // gEXEnable: the extended GBI on opcode 0x64 (a G_SPNOOP
                                                              // with RT64's magic)
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

    // Where the frame's own display list ends: at its G_ENDDL, before the head if the head is still in it. A game can go
    // on to write a second list somewhere else before the frame is done, and the head is then in that one: Turok 2's
    // map, drawn over the frame as lines in a list of its own. A G_ENDDL the command before it branches past (a
    // G_DL that doesn't push, to the command after the G_ENDDL) is never reached, and the list goes on after it: Turok
    // 3's cutscenes end each piece of their list so, and draw their letterbox's border after them.
    bool frame_list_end(uint8_t* rdram, uint32_t* end) {
        uint32_t head;
        bool in_frame = head_valid(rdram, &head);
        if (frame_start == 0) {
            return false;
        }
        uint32_t limit = in_frame ? head : frame_start + kMaxFrameBytes;
        for (uint32_t a = frame_start; a < limit; a += 8) {
            if ((read(rdram, a).w0 >> 24) == end_opcode) {
                Command before = a > frame_start ? read(rdram, a - 8) : Command{ 0, 0 };
                if ((before.w0 >> 24) == dl_opcode && ((before.w0 >> 16) & 0xFF) == 1 && before.w1 == a + 8) {
                    continue;
                }
                *end = a + 8;
                return true;
            }
        }
        *end = limit;
        return in_frame;
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

    // The screen's last column and row, which a game's full-screen scissor can stop short of: Turok 2's is
    // (0,0)-(479,359) on a 480x360 screen, its last pixel where the RDP wants its size, so it never draws column 479 or
    // row 359. At 4:3 they are black beside the window's black bars; in a widened picture RT64 scales the scissor with
    // the screen, and they become black strips along the window's right and bottom edges. Each G_SETSCISSOR from the
    // top left corner to one pixel short of the colour image's width is opened a column and a row: the colour image
    // doesn't say its height, and a scissor a column short is a row short too. RT64 then draws, and writes back, the
    // screen's whole height, which is the framebuffer's. A scissor set before the frame's first G_SETCIMG (Turok 2 sets
    // it first) goes with that one. Opened once, a scissor no longer matches, so a second pass leaves it alone.
    void open_scissor_last_pixel(uint8_t* rdram, uint32_t end) {
        uint32_t width = 0; // the colour image's, in 10.2
        for (uint32_t a = frame_start; a < end && width == 0; a += 8) {
            Command c = read(rdram, a);
            if ((c.w0 >> 24) == 0xFF) {
                width = ((c.w0 & 0xFFF) + 1) * 4;
            }
        }
        for (uint32_t a = frame_start; a < end; a += 8) {
            Command c = read(rdram, a);
            uint32_t op = c.w0 >> 24;
            if (op == 0xFF) { // G_SETCIMG
                width = ((c.w0 & 0xFFF) + 1) * 4;
                continue;
            }
            if (op != 0xED) {
                continue;
            }
            uint32_t ulx = (c.w0 >> 12) & 0xFFF, uly = c.w0 & 0xFFF;
            uint32_t lrx = (c.w1 >> 12) & 0xFFF, lry = c.w1 & 0xFFF;
            if (ulx == 0 && uly == 0 && width > 4 && lrx == width - 4 && lry > 0 && lry + 4 <= 0xFFF) {
                MEM_W(4, (gpr)(int32_t)a) = (int32_t)((c.w1 & ~0xFFFFFFu) | (width << 12) | (lry + 4));
            }
        }
    }
}

bool turok::widescreen_active() {
    return ultramodern::renderer::get_graphics_config().ar_option == ultramodern::renderer::AspectRatio::Expand;
}

void turok::set_display_list_head(uint32_t address, Microcode microcode) {
    head_var = address;
    hook_opcode = microcode == Microcode::F3DEX ? 0x00 : 0xE0; // G_SPNOOP in each
    end_opcode = microcode == Microcode::F3DEX ? 0xB8 : 0xDF;  // G_ENDDL in each
    dl_opcode = microcode == Microcode::F3DEX ? 0x06 : 0xDE;   // G_DL in each
}

// Where the frame's display list starts, which the border and the HUD look back to. Frame interpolation is RT64's own
// matching, unchanged: forcing interpolation on with a matrix group at the frame's base tears the level's geometry
// while the camera turns (docs/widescreen.md).
extern "C" void turok_frame_begin(uint8_t* rdram) {
    (void)rdram;
    frame_start = head_var != 0 ? (uint32_t)MEM_W(0, (gpr)(int32_t)head_var) : 0;
    hud_open = false;
}

// The camera's culling. The games test what to draw against their own view-projection matrix (a float one, the
// camera's, which it also converts for the display list), so whatever is outside the 4:3 view isn't drawn, and the
// sides of a widened picture are missing it. Once the display list's copy is made, the float matrix's x column is
// scaled so its view is as wide as 21:9: the picture is unchanged and the culling lets through what a window up to that
// wide shows.
extern "C" void turok_widen_culling(uint8_t* rdram, uint32_t matrix) {
    if (!turok::widescreen_active() || matrix == 0) {
        return;
    }
    constexpr float kScale = (4.0f / 3.0f) / (21.0f / 9.0f);
    for (uint32_t row = 0; row < 4; row++) {
        uint32_t a = matrix + row * 16; // [row][0]: clip x = sum of v[row] * m[row][0]
        float f;
        uint32_t bits = (uint32_t)MEM_W(0, (gpr)(int32_t)a);
        std::memcpy(&f, &bits, sizeof(f));
        f *= kScale;
        std::memcpy(&bits, &f, sizeof(f));
        MEM_W(0, (gpr)(int32_t)a) = (int32_t)bits;
    }
}

// The border: a vertical G_FILLRECT at most 4 pixels wide, a quarter of the screen high or more, at its left or right
// edge, or a horizontal one at most 4 pixels high, a quarter of the screen wide or more, at its top or bottom edge,
// becomes a no-op, in the frame's own display list. Turok 2's are a pixel each, its rows 0 and 359 drawn after the HUD,
// and in a widened picture, with no black bars above and below it, they are black strips along the window's top and
// bottom edges. Then the screen's last column and row, which the games' scissor can leave out: open_scissor_last_pixel.
extern "C" void turok_frame_end(uint8_t* rdram) {
    uint32_t end;
    if (!turok::widescreen_active() || !frame_list_end(rdram, &end)) {
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
        bool horizontal = (r_lry - r_uly) <= kEdge && (r_lrx - r_ulx) * 4 >= lrx;
        bool at_end = r_uly <= kEdge || r_lry + kEdge >= lry;
        if ((vertical && at_side) || (horizontal && at_end)) {
            MEM_W(0, (gpr)(int32_t)a) = 0; // G_SPNOOP
            MEM_W(4, (gpr)(int32_t)a) = 0;
        }
    }
    open_scissor_last_pixel(rdram, end);
}

// Before the HUD: the frame's scissors so far opened to the screen's last column and row (open_scissor_last_pixel),
// then the scissor pushed and set to the whole window (down to the game's own full-screen scissor's bottom: RT64
// writes back as many rows as were drawn to), and automatic anchoring on, which puts each HUD rectangle at the side
// it's nearest by where it sits on the screen.
extern "C" void turok_hud_begin(uint8_t* rdram) {
    hud_open = false;
    uint32_t head;
    if (!turok::widescreen_active() || !head_valid(rdram, &head)) {
        return;
    }
    open_scissor_last_pixel(rdram, head);
    uint32_t lrx, lry;
    if (!screen_scissor(rdram, head, &lrx, &lry)) {
        return;
    }
    Writer dl{ rdram, head };
    dl.enable();
    dl.cmd(0x64000017, 0); // gEXPushScissor
    dl.cmd(0x64000005, (kOriginLeft << 2) | (kOriginRight << 14)); // gEXSetScissor(G_SC_NON_INTERLACE, LEFT, RIGHT,
    dl.cmd(0, lry & 0xFFFF);                                         //   0, 0, 0, lry)
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
