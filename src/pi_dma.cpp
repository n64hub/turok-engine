// Cartridge DMA for the game's own DMA thread, and the libultra state the runtime's osInitialize leaves out.
//
// These games don't load through osPiStartDma and the PI manager. Their DMA thread takes requests from a queue, calls
// osPiRawStartDma(OS_READ, rom_offset, dram, size) itself, and waits for the PI interrupt message it registered with
// osSetEventMesg(OS_EVENT_PI, ...). The runtime only has a stub for osPiRawStartDma and doesn't track OS_EVENT_PI, so
// both are done here: the DMA happens at once, then the PI event message is posted, at once too. Taken from PaxDino's
// src/game/pi_dma.cpp (https://github.com/wootbeer/PaxDino, GPL-3.0).
//
// A game names its osPiRawStartDma turok_pi_raw_start_dma in its syms and lists it in its N64Recomp configuration's
// ignored functions, and hooks the osSetEventMesg(OS_EVENT_PI, ...) call in its DMA thread's setup with
// turok_set_pi_event(rdram, ctx->r5, <message>).
#include <atomic>
#include <cstdio>

#include "ultramodern/ultramodern.hpp"
#include "librecomp/game.hpp"
#include "librecomp/helpers.hpp"
#include "turok_engine.h"

namespace {
    std::atomic<int32_t> pi_event_mq{ 0 };
    std::atomic<uint32_t> pi_event_msg{ 0 };
}

extern "C" void turok_set_pi_event(uint8_t*, uint64_t mq, uint32_t msg) {
    pi_event_msg = msg;
    pi_event_mq = (int32_t)mq;
}

// osPiRawStartDma(s32 direction, u32 devAddr, void* dramAddr, u32 size)
extern "C" void turok_pi_raw_start_dma(uint8_t* rdram, recomp_context* ctx) {
    uint32_t direction = (uint32_t)ctx->r4;
    // libultra ORs in osRomBase (0xB0000000) and converts to physical.
    uint32_t physical_addr = ((uint32_t)ctx->r5 | 0xB0000000u) & 0x1FFFFFFFu;
    gpr dram_addr = ctx->r6;
    uint32_t size = (uint32_t)ctx->r7;

    if (direction != 0) {
        fprintf(stderr, "[turok-engine] Unexpected PI write to 0x%08X (0x%X bytes)\n", physical_addr, size);
    }
    else {
        recomp::do_rom_read(rdram, dram_addr, physical_addr, size);
    }

    // Posted at once, from this (the DMA) thread, as a game thread would. As an external message it came later, through
    // the runtime's own thread: the DMA thread blocked waiting for it, lower threads ran meanwhile, and a game could free
    // buffers whose requests were already queued behind it, whose DMA then wrote over memory given to something else.
    int32_t mq = pi_event_mq;
    if (mq != 0) {
        osSendMesg(rdram, mq, pi_event_msg, OS_MESG_NOBLOCK);
    }
    ctx->r2 = 0;
}

// osClockRate (a u64): libultra's osInitialize sets it to 62.5 MHz times 3/4, the CPU counter's rate, but N64Recomp
// skips the game's osInitialize and the runtime's doesn't set it, so it stays 0, and a game that divides osGetTime by
// it gets the same time for ever. A game hooks it right after its call to osInitialize, with osClockRate's address.
extern "C" void turok_set_clock_rate(uint8_t* rdram, uint32_t address) {
    constexpr uint64_t clock_rate = 62'500'000ull * 3 / 4;
    MEM_W(0, (gpr)(int32_t)address) = (int32_t)(clock_rate >> 32);
    MEM_W(4, (gpr)(int32_t)address) = (int32_t)(uint32_t)clock_rate;
}
