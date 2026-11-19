// A frame started as late as it can be and still be swapped in at the retrace it would have been.
//
// The games pace their frames by the retraces: a frame is swapped in once the one on the screen has been there the
// frame's count of retraces, and the game starts the next frame as soon as it has sent the last one, right after a
// swap. On the console a frame took most of that count to build. Recompiled, it takes a millisecond or two, then waits
// for its swap with the pad it read and the world it moved already old: at 30 frames a second, a retrace (17 ms) more
// from a press to the picture than the frame needs. turok_start_frame_late holds the frame's start until one retrace
// is left before its swap, so it reads the pad and moves the world that much later and is swapped in at the same
// retrace as before (docs/input_lag.md).
//
// The wait blocks the game's thread on a message queue, as libultra would, so the game's other threads run meanwhile;
// the game's retrace handler posts it (turok_retrace). A frame that took long to build from its start to its end
// (kSlowFrameUs) in the last second or so turns the wait off until they're quick again: a frame built in a retrace's
// time from a late start would miss its swap.
#include <atomic>
#include <chrono>

#include "ultramodern/ultramodern.hpp"
#include "librecomp/helpers.hpp"
#include "turok_engine.h"

namespace {
    using Clock = std::chrono::steady_clock;
    constexpr int64_t kSlowFrameUs = 8000;
    constexpr int kWindowFrames = 30;

    std::atomic<int32_t> waiter{ 0 };
    Clock::time_point frame_start{};
    int64_t slowest_us = 0;        // the slowest frame of the last whole window
    int64_t window_slowest_us = 0; // of the window so far
    int window_frames = 0;

    // Blocks the calling game thread until the next turok_retrace.
    void wait_retrace(uint8_t* rdram, recomp_context* ctx) {
        // The queue and its one message are below the waiting function's stack frame, which nothing uses while it waits.
        int32_t sp = (int32_t)ctx->r29;
        int32_t mq = (sp - 0x80) & ~7;
        int32_t buf = (sp - 0x40) & ~7;
        osCreateMesgQueue(rdram, mq, buf, 1);
        waiter = mq;
        osRecvMesg(rdram, mq, NULLPTR, OS_MESG_BLOCK);
    }
}

extern "C" void turok_retrace(uint8_t* rdram) {
    int32_t mq = waiter.exchange(0);
    if (mq != 0) {
        osSendMesg(rdram, mq, 0, OS_MESG_NOBLOCK);
    }
}

extern "C" void turok_start_frame_late(uint8_t* rdram, recomp_context* ctx, uint32_t per_frame, uint32_t since_swap) {
    if (slowest_us < kSlowFrameUs) {
        // A few at most: the count since the swap passes per_frame - 1 within that many retraces.
        for (int i = 0; i < 8; i++) {
            int32_t n = MEM_W(0, (gpr)(int32_t)per_frame);
            int32_t seen = MEM_W(0, (gpr)(int32_t)since_swap);
            if (n < 2 || seen >= n - 1) {
                break;
            }
            wait_retrace(rdram, ctx);
        }
    }
    frame_start = Clock::now();
}

extern "C" void turok_frame_sent(uint8_t*) {
    if (frame_start == Clock::time_point{}) {
        return;
    }
    int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - frame_start).count();
    if (us > window_slowest_us) {
        window_slowest_us = us;
    }
    if (us > slowest_us) {
        slowest_us = us;
    }
    if (++window_frames >= kWindowFrames) {
        slowest_us = window_slowest_us;
        window_slowest_us = 0;
        window_frames = 0;
    }
}
