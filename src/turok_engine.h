// What a Turok game's own code calls in the engine (README.md, "How a game uses it").
#ifndef TUROK_ENGINE_H
#define TUROK_ENGINE_H

#include <cstddef>
#include <cstdint>

#include "recomp.h"
#include "librecomp/sections.h"

namespace recomp::config {
    class Config;
}

namespace turok {
    // The GameEntry's entrypoint_address: where the console's boot code loads the first MB, the header's entry point in
    // kseg0. The runtime loads it there before on_init_callback, which is when the TLB views open; the code itself
    // starts at its TLB address (N64Recomp's entrypoint).
    constexpr gpr boot_load_address = (gpr)(int32_t)0x80000400u;

    // tlb_views.cpp: makes every section below kseg0 in the game's section table accessible, fills it from the ROM and
    // registers its functions there. From the GameEntry's on_init_callback, with N64Recomp's section_table.
    void map_tlb_sections(uint8_t* rdram, const SectionTableEntry* sections, size_t count);

    // Makes [vaddr, vaddr + size), rounded out to whole pages, readable and writable in rdram's reservation (with
    // RECOMP_KUSEG_WINDOW defined, through the runtime's recomp::make_kuseg_accessible, which the web build needs).
    bool make_accessible(uint8_t* rdram, uint32_t vaddr, uint32_t size);

    // The FPU in FR mode, as the console's boot code leaves it: on_init_callback's context and, as the GameEntry's
    // thread_create_callback, every thread's.
    void set_fpu_mode(recomp_context* ctx);
    void on_thread_create(uint8_t* rdram, recomp_context* ctx);

    // tlb_views.cpp: the first virtual address the game's VM server hands a ROM region out at, past what the game maps
    // at boot (turok_vm_map_rom_region). Before the game runs.
    void set_vm_base(uint32_t vaddr);

    // audio.cpp: from the game's audio callbacks, the game's rate (set_frequency) and each buffer queued
    // (queue_samples), which turok_ai_get_status paces the audio thread by.
    void on_audio_frequency(uint32_t frequency);
    void on_audio_queued();

    // controller_pak.cpp: the ports holding a Controller Pak, bit 0 for port 1, as the GameEntry's
    // controller_pak_ports (the runtime's pak) has them: the games save to one in port 1.
    constexpr uint8_t controller_pak_ports = 0b0001;
    // controller_pak.cpp: the GameEntry's controller_pak_banks, the pak in port 1 with Banks banks (32 KiB each) and 1
    // in the others. A pak of n banks holds 128 - 3 - 2n pages in its first bank (its id, two inode pages per bank and
    // the notes' directory take the rest) and 127 in each after it: 123 for one bank, 373 for three. A game asks for
    // enough for a save in each of the pak's 16 notes: Turok 1's and Turok 3's saves take 16 pages (3 banks). The
    // runtime grows an older, smaller pak to it at the game's first osPfsInitPak and keeps its saves.
    template <int Banks> int controller_pak_banks(int port) { return port == 0 ? Banks : 1; }

    // widescreen.cpp: true while RT64 shows the picture wider than 4:3 (Expand). The game's display list head, the
    // address of the pointer its code writes commands through, before the game runs (or each frame, from a hook, for
    // a game whose head moves), and the microcode the display list is for: Turok 2's engine family has an F3DEX2,
    // Turok 1 an F3DEX, whose commands' opcodes differ.
    enum class Microcode { F3DEX, F3DEX2 };
    bool widescreen_active();
    void set_display_list_head(uint32_t address, Microcode microcode = Microcode::F3DEX2);

    // render_to_ram.cpp: RT64's copy of each frame into RDRAM off, and the Graphics tab's Copy Frames to RAM option
    // (Off by default) that turns it back on at the next start. With the Graphics tab, before the config is finalized.
    void add_copy_frames_option(recomp::config::Config& graphics);

    // mouse_look.cpp: the mouse's movement since the last poll, in counts scaled by the Mouse Sensitivity setting (the
    // frontend's get_mouse_deltas), from the game's get_input for player 1 while mouse look is on, in place of adding it
    // to the stick. turok_spring_look turns the view by it.
    void add_mouse_motion(float dx, float dy);

    // mouse_look.cpp: the General tab's Stick Sensitivity option (100%, the game's own, by default), which
    // turok_stick_sensitivity reads. With the General tab, before the config is finalized.
    void add_stick_sensitivity_option(recomp::config::Config& general);
}

// The natives a game names in its syms or calls from its N64Recomp hooks.
extern "C" {
    // tlb_views.cpp: VmMapRomRegion(size, unused, rom) -> virtual base.
    void turok_vm_map_rom_region(uint8_t* rdram, recomp_context* ctx);
    // pi_dma.cpp: osPiRawStartDma, the message the game's DMA thread waits for, and osClockRate.
    void turok_pi_raw_start_dma(uint8_t* rdram, recomp_context* ctx);
    void turok_set_pi_event(uint8_t* rdram, uint64_t mq, uint32_t msg);
    void turok_set_clock_rate(uint8_t* rdram, uint32_t address);
    // audio.cpp: osAiGetStatus, full once about two retraces of audio are queued.
    void turok_ai_get_status(uint8_t* rdram, recomp_context* ctx);
    // controller_pak.cpp: osMotorInit, answered as a Controller Pak in controller_pak_ports' ports, nothing in the others.
    void turok_motor_init(uint8_t* rdram, recomp_context* ctx);
    // widescreen.cpp, from a game's hooks: once its frame's display list has started (frame_begin) and before it's
    // sent (frame_end), and before and after its HUD (hud_begin, hud_end). Each does nothing outside Expand, but for a
    // camera cut's frame while RT64 interpolates (frame_begin, frame_end).
    void turok_frame_begin(uint8_t* rdram);
    void turok_frame_end(uint8_t* rdram);
    void turok_hud_begin(uint8_t* rdram);
    void turok_hud_end(uint8_t* rdram);
    // widescreen.cpp: the camera's float view-projection matrix (at matrix), once the display list's copy is made:
    // held up to the last frame's for a camera cut, and widened for its culling.
    void turok_widen_culling(uint8_t* rdram, uint32_t matrix);
    // widescreen.cpp: the camera's view pyramid in its own space (count points of three floats, at points), widened as
    // the matrix is before it's moved into the world.
    void turok_widen_frustum(uint8_t* rdram, uint32_t points, uint32_t count);
    // mouse_look.cpp, from a game's hook in its look code, which runs once a frame while the player has control: the
    // mouse's movement since then added to the player's yaw (a float at yaw, in radians times yaw_scale, growing to
    // the right) and pitch (at pitch, radians times pitch_scale, growing upwards), the pitch kept within [pitch_min,
    // pitch_max]. pitch_target is the register holding the float target the game is about to ease the pitch towards,
    // which it makes of the stick's deflection and which springs back level when the stick is let go: that
    // deflection tilts the view up or down at a rate instead, and the view stays where the stick and the mouse leave it.
    void turok_spring_look(uint8_t* rdram, uint32_t yaw, float yaw_scale, uint32_t pitch, float pitch_scale,
        gpr* pitch_target, float pitch_min, float pitch_max);
    // mouse_look.cpp, from a game's hooks in its look code where it reads the stick's deflection to turn or look by:
    // what the deflection is multiplied by, the Stick Sensitivity setting (1 at 100%, and 1 in a game without it).
    float turok_stick_sensitivity(void);
    // frame_pacing.cpp, from a game's hooks: at its frame's start, before it reads the pad, the frame held until one
    // retrace is left before its swap (per_frame is the address of the game's retraces a frame is shown for, a word,
    // and since_swap of its count of retraces since the frame on the screen was swapped in); once the frame is sent
    // (frame_sent); and at each retrace, in the game's retrace handler (retrace). The frame's start also sets RT64 to
    // present early while the game shows a frame for two retraces, and with Skip Buffering at any other count.
    void turok_start_frame_late(uint8_t* rdram, recomp_context* ctx, uint32_t per_frame, uint32_t since_swap);
    void turok_frame_sent(uint8_t* rdram);
    // frame_pacing.cpp, from a game's hook where its frame sends its tasks: how many it sends. A frame of more than one
    // task is presented with Skip Buffering, and the next starts at once.
    void turok_frame_tasks(uint8_t* rdram, uint32_t tasks);
    void turok_retrace(uint8_t* rdram);
}

#endif
