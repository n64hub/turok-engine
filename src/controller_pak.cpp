// Which controller ports hold a Controller Pak, as the games ask it outside the pak itself, and the Rumble Pak their
// rumble code finds in the same port.
//
// The pak is N64ModernRuntime's (librecomp/src/pak.cpp, from N64Recomp-browser's v0.255.0): a file-backed pak in each
// port a game's GameEntry sets in controller_pak_ports, saves/controllerpak_<port>.mpk beside the game's own save. A
// Turok game sets it to turok::controller_pak_ports, and the two answers here read the same ports:
// - osPfsIsPlug, which the runtime lacks: Turok: Dinosaur Hunter asks it before it opens its Controller Pak save.
// - osMotorInit: Turok 2 and Turok 3 probe a port with it first and try osPfsInitPak only when it fails, and the
//   runtime's answers by the port's connected_pak, which has no Controller Pak (ultramodern's input.hpp: a game that
//   reports a pak in port 1 for the controller status reports a Rumble Pak). So a game names its osMotorInit
//   turok_motor_init in its syms and lists it in its N64Recomp configuration's ignored functions.
//
// On a console a port holds one pak or the other. Turok 2 and Turok 3 ask osMotorInit from two places, each with an
// OSPfs of its own: the save screens, which want the Controller Pak, and the rumble code, which keeps an OSPfs in each
// controller's struct, sets the struct's flag when osMotorInit finds a Rumble Pak and only then drives the motor
// (osMotorStart, osMotorStop). Here the port holds both: the rumble code's OSPfs (set_rumble_pfs) gets the runtime's
// osMotorInit, a Rumble Pak where the port's connected_pak has one, and every other gets the Controller Pak's answer.

#include "ultramodern/ultra64.h"
#include "recomp.h"
#include "turok_engine.h"

namespace {

constexpr int PFS_ERR_NOPACK = 1;
constexpr int PFS_ERR_DEVICE = 11;

uint32_t rumble_pfs_first = 0;
uint32_t rumble_pfs_stride = 0;

bool has_pak(int port) {
    return port >= 0 && port < 4 && (turok::controller_pak_ports >> port) & 1;
}

bool is_rumble_pfs(uint32_t pfs, int port) {
    return rumble_pfs_first != 0 && port >= 0 && port < 4 &&
        pfs == rumble_pfs_first + (uint32_t)port * rumble_pfs_stride;
}

} // namespace

void turok::set_rumble_pfs(uint32_t first, uint32_t stride) {
    rumble_pfs_first = first;
    rumble_pfs_stride = stride;
}

// s32 osMotorInit(OSMesgQueue* mq, OSPfs* pfs, int channel)
extern "C" void turok_motor_init(uint8_t* rdram, recomp_context* ctx) {
    int port = (int)ctx->r6;
    if (is_rumble_pfs((uint32_t)ctx->r5, port)) {
        ctx->r2 = osMotorInit(PASS_RDRAM (PTR(OSMesgQueue))(int32_t)ctx->r4, (PTR(OSPfs))(int32_t)ctx->r5, port);
        return;
    }
    // What a Controller Pak answers: the bank select reads back as no motor. A port without one holds nothing.
    ctx->r2 = has_pak(port) ? PFS_ERR_DEVICE : PFS_ERR_NOPACK;
}

// s32 osPfsIsPlug(OSMesgQueue* mq, u8* pattern): a bit for each port with a pak in it, as the controllers' status
// reports one.
extern "C" void osPfsIsPlug_recomp(uint8_t* rdram, recomp_context* ctx) {
    MEM_B(0, ctx->r5) = (int8_t)turok::controller_pak_ports;
    ctx->r2 = 0;
}
