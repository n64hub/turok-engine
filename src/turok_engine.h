// What a Turok game's own code calls in the engine (README.md, "How a game uses it").
#ifndef TUROK_ENGINE_H
#define TUROK_ENGINE_H

#include <cstddef>
#include <cstdint>

#include "recomp.h"
#include "librecomp/sections.h"

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

    // controller_pak.cpp: what each controller port (0-3) holds. A Controller Pak in port 1 unless a game sets its own.
    enum class Accessory { None, ControllerPak, RumblePak };
    void set_accessories(Accessory (*accessory)(int port));

    // widescreen.cpp: true while RT64 shows the picture wider than 4:3 (Expand). The game's display list head, the
    // address of the pointer its code writes commands through, before the game runs (or each frame, from a hook, for
    // a game whose head moves), and the microcode the display list is for: Turok 2's engine family has an F3DEX2,
    // Turok 1 an F3DEX, whose commands' opcodes differ.
    enum class Microcode { F3DEX, F3DEX2 };
    bool widescreen_active();
    void set_display_list_head(uint32_t address, Microcode microcode = Microcode::F3DEX2);
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
    // controller_pak.cpp: osMotorInit, answered as the port's accessory would.
    void turok_motor_init(uint8_t* rdram, recomp_context* ctx);
    // widescreen.cpp, from a game's hooks: once its frame's display list has started (frame_begin) and before it's
    // sent (frame_end), and before and after its HUD (hud_begin, hud_end). Each does nothing outside Expand.
    void turok_frame_begin(uint8_t* rdram);
    void turok_frame_end(uint8_t* rdram);
    void turok_hud_begin(uint8_t* rdram);
    void turok_hud_end(uint8_t* rdram);
    // widescreen.cpp: the camera's float view-projection matrix (at matrix), widened for its culling once the display
    // list's copy is made.
    void turok_widen_culling(uint8_t* rdram, uint32_t matrix);
    // widescreen.cpp: the camera's view pyramid in its own space (count points of three floats, at points), widened as
    // the matrix is before it's moved into the world.
    void turok_widen_frustum(uint8_t* rdram, uint32_t points, uint32_t count);
}

#endif
