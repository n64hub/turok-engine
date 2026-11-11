// libultra functions the games link that N64Recomp leaves out (its ignored_funcs) and the runtime doesn't implement,
// so each call to one in a game's RecompiledFuncs/ comes here.
#include <cstdint>

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include "recomp.h"

// The thread queues, called by the games' own interrupt-side message send. The runtime keeps its threads in the same
// guest OSThread lists, so these work on them as libultra's do. From PaxDino's src/game/stock_runtime_compat.cpp
// (https://github.com/wootbeer/PaxDino, GPL-3.0).
extern "C" void __osEnqueueThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    ultramodern::thread_queue_insert(rdram, (PTR(PTR(OSThread)))(int32_t)ctx->r4, (PTR(OSThread))(int32_t)ctx->r5);
}

extern "C" void __osDequeueThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    ultramodern::thread_queue_remove(rdram, (PTR(PTR(OSThread)))(int32_t)ctx->r4, (PTR(OSThread))(int32_t)ctx->r5);
}

extern "C" void __osPopThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = ultramodern::thread_queue_pop(rdram, (PTR(PTR(OSThread)))(int32_t)ctx->r4);
}

// Called by the games' VM pager when it evicts a page, which never happens here: every TLB section and ROM region is
// mapped for good (tlb_views.cpp).
extern "C" void osUnmapTLB_recomp(uint8_t*, recomp_context*) {
}

// The Controller Pak's internals, for the games' own copies of libultra's pak functions. The osPfs* entry points that
// call them are the engine's (controller_pak.cpp), and the Rumble Pak's are the runtime's, so the calls never come;
// were one to, it would find no pak.
constexpr int32_t PFS_ERR_NOPACK = 1;

extern "C" void __osPfsSelectBank_recomp(uint8_t*, recomp_context* ctx) {
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void __osContRamRead_recomp(uint8_t*, recomp_context* ctx) {
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void __osContRamWrite_recomp(uint8_t*, recomp_context* ctx) {
    ctx->r2 = PFS_ERR_NOPACK;
}

extern "C" void __osPfsRWInode_recomp(uint8_t*, recomp_context* ctx) {
    ctx->r2 = PFS_ERR_NOPACK;
}

// The SI's raw DMA, which the same pak code calls to talk to the controllers itself: no device answers it here.
extern "C" void __osSiRawStartDma_recomp(uint8_t*, recomp_context* ctx) {
    ctx->r2 = (gpr)-1;
}
