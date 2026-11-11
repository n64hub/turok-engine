// The TLB-mapped code of a Turok game, made something the recompiled code can run and read (docs/rom_layout.md).
//
// These games don't run their code from kseg0 as most N64 games do. The entry point sets TLB entry 31 so that
// 0x00200000-0x003FFFFF maps onto physical 0-2 MB, and the code is linked there: the first MB of ROM after the header
// is code at 0x00200400. Some games link more code elsewhere in TLB space and map it themselves. Data is reached
// through kseg0 as usual. Taken from PaxDino's src/game/tlb_views.cpp (https://github.com/wootbeer/PaxDino, GPL-3.0),
// made to work from a game's section table instead of Rage Wars' addresses.
//
// The recompiled code needs two things from every section N64Recomp has below kseg0 (the game's syms/, from
// tools/gen_syms.py and its layout file):
//
// - Function lookups. Calls through function pointers use the TLB addresses, so every function is registered there
//   (load_overlays). librecomp's own init registers the first MB at its load address, 0x80000400, which the code never
//   uses but which does no harm.
//
// - Data reads. Strings, tables and constants in those sections are read through their TLB addresses. Generated code
//   turns an address into rdram + (address - 0xFFFFFFFF80000000), so 0x00200400 lands 0x80200400 bytes into the
//   runtime's 4 GB rdram reservation, which is otherwise inaccessible. Those pages are made readable and writable and
//   filled from the ROM. They are copies, not aliases of physical memory: nothing the game writes to physical memory
//   shows up here. That is what the games rely on: on a 4 MB console the game's TLB-miss handler re-reads a code page
//   from ROM whenever one is evicted, so it can't depend on writes there, and no code reads or writes data through
//   the TLB view of physical memory outside the exception code and osInitialize, which don't run. If that turns out
//   wrong for a game, the fix is to back rdram with shared memory and map these ranges as real aliases.
//
// On the web rdram is a linear memory: a game on a runtime with KUSEG support defines RECOMP_KUSEG_WINDOW, and these
// ranges are opened by the runtime (recomp::make_kuseg_accessible), in a 16 MB window after RDRAM on the web. Every TLB
// section and VM region must then lie below 0x01000000.
//
// A game calls map_tlb_sections from its GameEntry's on_init_callback, and on_thread_create is its
// thread_create_callback.
#include <algorithm>
#include <cstdio>
#include <mutex>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <sys/mman.h>
#endif

#include "librecomp/addresses.hpp"
#include "librecomp/game.hpp"
#include "librecomp/helpers.hpp"
#include "librecomp/overlays.hpp"
#include "turok_engine.h"

namespace {
    // The offset of a 32-bit guest address from rdram, as the generated code computes it.
    constexpr uint64_t rdram_offset(uint32_t vaddr) {
        return (uint64_t)(int64_t)(int32_t)vaddr - 0xFFFFFFFF80000000ULL;
    }
}

bool turok::make_accessible(uint8_t* rdram, uint32_t vaddr, uint32_t size) {
#ifdef RECOMP_KUSEG_WINDOW
    // A game on a runtime with KUSEG support (N64Recomp-browser v0.183.0 and later) defines this for every target: the
    // runtime opens the range, natively by committing its reserved pages and on the web in the window after RDRAM.
    return recomp::make_kuseg_accessible(rdram, vaddr, size);
#else
    constexpr uint32_t page = 0x1000;
    uint32_t start = vaddr & ~(page - 1);
    uint32_t end = (vaddr + size + page - 1) & ~(page - 1);
    uint8_t* host = rdram + rdram_offset(start);
#ifdef _WIN32
    // librecomp reserves and commits the whole 4 GB as PAGE_NOACCESS.
    DWORD old_protect = 0;
    return VirtualProtect(host, end - start, PAGE_READWRITE, &old_protect) != 0;
#else
    return mprotect(host, end - start, PROT_READ | PROT_WRITE) == 0;
#endif
#endif
}

void turok::map_tlb_sections(uint8_t* rdram, const SectionTableEntry* sections, size_t count) {
    for (size_t i = 0; i < count; i++) {
        const SectionTableEntry& s = sections[i];
        if (s.ram_addr >= 0x80000000u) {
            continue;
        }
        if (!make_accessible(rdram, s.ram_addr, s.size)) {
            fprintf(stderr, "Couldn't make the TLB section at 0x%08X accessible\n", s.ram_addr);
            continue;
        }
        recomp::do_rom_read(rdram, (gpr)(int32_t)s.ram_addr, 0x10000000u + s.rom_addr, s.size);
        load_overlays(s.rom_addr, (int32_t)s.ram_addr, s.size);
    }
}

// The N64's boot code sets Status.FR, so the odd FPU registers are registers of their own, and these games'
// GCC-compiled code does single-precision math in them (f1, f21, ...). The runtime starts every context with FR off,
// where the generated code's odd-register loads and its arithmetic disagree, so every thread is switched to FR on.
void turok::set_fpu_mode(recomp_context* ctx) {
    ctx->f_odd = &ctx->f1.u32l;
    ctx->mips3_float_mode = true;
}

void turok::on_thread_create(uint8_t*, recomp_context* ctx) {
    set_fpu_mode(ctx);
}

// ROM-backed regions of virtual memory. The games also map data straight from ROM into TLB space and let their
// TLB-miss pager page it in: the audio banks, through their VmMapRomRegion, which asks the VM server thread for a region
// and gets back its virtual base. No TLB miss ever happens here, so a game names that function turok_vm_map_rom_region
// in its syms and lists it in its N64Recomp configuration's ignored functions: each region gets the address the game's
// server would give it, made accessible and filled from ROM at once. The game can't depend on writing to these either
// (evicted pages are re-read from ROM). Regions are never freed: the callers are the audio-bank loads at startup.
namespace {
    std::mutex vm_mutex;
    // The server hands out pages from the first one past the regions the game maps at boot, each region starting on an
    // even page (a TLB entry maps an even/odd pair). The ROM data is linked for those addresses (an audio bank holds
    // pointers into its own region), so regions must come in the game's order.
    uint32_t vm_next_page = 0;
}

void turok::set_vm_base(uint32_t vaddr) {
    std::lock_guard lock{ vm_mutex };
    vm_next_page = vaddr >> 12;
}

// VmMapRomRegion(u32 size, unused, u32 rom_address) -> the region's virtual base
extern "C" void turok_vm_map_rom_region(uint8_t* rdram, recomp_context* ctx) {
    uint32_t size = (uint32_t)ctx->r4;
    uint32_t rom = (uint32_t)ctx->r6 & 0x0FFFFFFF;
    uint32_t rom_size = (uint32_t)recomp::get_rom().size();

    std::lock_guard lock{ vm_mutex };
    uint32_t pages = (size + 0xFFF) >> 12;
    uint32_t base_page = (vm_next_page + 1) & ~1u;
    uint32_t base = base_page << 12;
    uint32_t bytes = pages << 12;
    if (vm_next_page == 0 || pages == 0 || rom >= rom_size || base + bytes >= 0x7F000000 ||
        !turok::make_accessible(rdram, base, bytes)) {
        fprintf(stderr, "[turok-engine] Couldn't map ROM 0x%08X (0x%X bytes) into virtual memory\n", rom, size);
        ctx->r2 = 0;
        return;
    }
    uint32_t copy = std::min(bytes, rom_size - rom) & ~1u;
    recomp::do_rom_read(rdram, (gpr)(int32_t)base, 0x10000000u + rom, copy);
    vm_next_page = base_page + pages;
    ctx->r2 = (gpr)(int32_t)base;
}
