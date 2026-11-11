// What a Turok game's own code calls in the engine (README.md, "How a game uses it").
#ifndef TUROK_ENGINE_H
#define TUROK_ENGINE_H

#include <cstddef>
#include <cstdint>

#include "recomp.h"
#include "librecomp/sections.h"

namespace turok {
    // tlb_views.cpp: makes every section below kseg0 in the game's section table accessible, fills it from the ROM and
    // registers its functions there. From the GameEntry's on_init_callback, with N64Recomp's section_table.
    void map_tlb_sections(uint8_t* rdram, const SectionTableEntry* sections, size_t count);

    // Makes [vaddr, vaddr + size), rounded out to whole pages, readable and writable in rdram's reservation.
    bool make_accessible(uint8_t* rdram, uint32_t vaddr, uint32_t size);

    // The FPU in FR mode, as the console's boot code leaves it: on_init_callback's context and, as the GameEntry's
    // thread_create_callback, every thread's.
    void set_fpu_mode(recomp_context* ctx);
    void on_thread_create(uint8_t* rdram, recomp_context* ctx);
}

#endif
