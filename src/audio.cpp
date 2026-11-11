// The pace of the games' audio thread. It makes audio frames for as long as osAiGetStatus doesn't report
// AI_STATUS_FIFO_FULL, as the console's two-buffer AI FIFO paces it, and the runtime's osAiGetStatus always reports 0:
// the thread would spin at its high priority and starve the rest. So a game names its osAiGetStatus
// turok_ai_get_status in its syms and lists it in its N64Recomp configuration's ignored functions, and the FIFO counts
// as full once about two retraces of audio are queued. After PaxDino's trw_osAiGetStatus (src/main/main.cpp,
// https://github.com/wootbeer/PaxDino, GPL-3.0).
//
// The game's audio callbacks tell the engine the rate (turok::on_audio_frequency, from set_frequency) and each buffer
// queued (turok::on_audio_queued, from queue_samples).
#include <atomic>

#include "ultramodern/ultramodern.hpp"
#include "turok_engine.h"

namespace {
    std::atomic<uint32_t> audio_frequency{ 48000 };
    // Polls answered "not full" since audio was last queued. A frame can come out empty (no sound playing yet, as
    // while the game is still setting up its players); then nothing is queued and the thread would spin for good. After
    // two such polls the FIFO counts as full, and the thread waits for the next retrace instead.
    std::atomic<int> polls_without_audio{ 0 };
}

void turok::on_audio_frequency(uint32_t frequency) {
    audio_frequency = frequency;
}

void turok::on_audio_queued() {
    polls_without_audio = 0;
}

extern "C" void turok_ai_get_status(uint8_t*, recomp_context* ctx) {
    constexpr uint32_t AI_STATUS_FIFO_FULL = 0x80000000u;
    constexpr uint32_t target_retraces = 2;
    // The runtime's count (osAiGetLength's), in stereo 16-bit frames at the game's rate.
    uint32_t queued_frames = ultramodern::get_remaining_audio_bytes() / (2 * sizeof(int16_t));
    bool full = queued_frames >= audio_frequency * target_retraces / 60 || polls_without_audio >= 2;
    if (!full) {
        polls_without_audio++;
    }
    ctx->r2 = full ? AI_STATUS_FIFO_FULL : 0;
}
