// Mouse look: the mouse turns the player's view itself, as a mouse does in a PC shooter, instead of being added to the
// control stick (docs/mouse_look.md).
//
// The games turn and look with the stick at a rate, and look up and down with it by an amount that springs back when
// it's let go, all three. A mouse made into a stick inherits both: each poll's movement is a deflection, so small
// movements fall into the game's dead zone and a turn depends on how long the mouse moved, not how far. Here a game
// hands the engine the mouse's movement at each poll (add_mouse_motion), and its look code, which runs once a frame
// while the player has control, calls turok_spring_look with where the player's angles are: the movement is added to
// them there, so it turns the view by as much as the mouse moved, at once. The stick's look up and down is made a rate
// there too, so the view stays where the stick leaves it.
//
// The stick keeps the game's own turn, at its own speed, scaled by the General tab's Stick Sensitivity
// (add_stick_sensitivity_option): a game's hooks multiply the stick's deflection by turok_stick_sensitivity where its
// look code reads it to turn and to look up and down, so the view turns faster or slower for the same push, and faster
// than the game's own at a full push, while the menus, which read the stick apart from the look, move as they did.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

#include "recomp.h"
#include "recompui/config.h"
#include "turok_engine.h"

namespace {
    using Clock = std::chrono::steady_clock;

    // A count's turn, at 100% Mouse Sensitivity (the frontend scales the counts by it): 0.12 degrees, so the default
    // 50% turns a whole circle in 6000 counts, 15 cm at 1000 counts an inch.
    constexpr float kRadiansPerCount = 0.12f * 3.14159265f / 180.0f;

    // Movement the look hasn't taken yet is dropped once the look hasn't run for this long: the game's pause menu,
    // a cinematic and the menus between levels don't call it, and the mouse moved there isn't a turn.
    constexpr auto kStale = std::chrono::milliseconds(150);
    // The look hasn't run for this long: the player has been away from it (a cinematic, a level's load), and what the
    // mouse held the view at is let go.
    constexpr auto kAway = std::chrono::milliseconds(500);

    // How fast the stick tilts a held angle (the look up or down, or a head turned on a ladder), pushed all the way:
    // in radians a second, about 140 degrees, two thirds of how fast Turok 2 turns with it (0.132 radians a frame at
    // 30 a second).
    constexpr float kStickRate = 2.4f;
    // The longest a frame of the look counts for at that rate: a hitch doesn't throw the view.
    constexpr float kLongestFrame = 0.1f;

    const std::string stick_sensitivity_option = "stick_sensitivity";
    // The setting, as a factor: read from the config at each change, so the look reads a float a frame.
    std::atomic<float> stick_sensitivity{ 1.0f };

    // An angle the game springs back to 0 when the stick is let go, held where the stick and the mouse leave it, and
    // when its hook last ran. Each is the game thread's alone.
    struct Spring {
        float held = 0.0f;
        Clock::time_point last{};
    };
    Spring pitch_spring;   // the look up and down, on foot and on Turok 3's ladders alike
    Spring turn_spring;    // the head turned apart from the body, on Turok 3's ladders

    std::mutex mutex;
    float pending_x = 0.0f;    // counts to the right since the look last took them
    float pending_y = 0.0f;    // counts down
    Clock::time_point last_look{};

    // The mouse's movement on the axes asked for (nullptr for the other, left for the hook that takes it later in the
    // same frame), and whether the spring's hook has been away and for how long it hasn't run.
    void take(Spring& spring, float* dx, float* dy, bool& away, float& seconds) {
        std::lock_guard lock{ mutex };
        Clock::time_point now = Clock::now();
        away = now - spring.last > kAway;
        seconds = away ? 0.0f : std::min(std::chrono::duration<float>(now - spring.last).count(), kLongestFrame);
        spring.last = now;
        last_look = now;
        if (dx != nullptr) {
            *dx = pending_x;
            pending_x = 0.0f;
        }
        if (dy != nullptr) {
            *dy = pending_y;
            pending_y = 0.0f;
        }
    }

    float read_float(uint8_t* rdram, uint32_t address) {
        uint32_t bits = (uint32_t)MEM_W(0, (gpr)(int32_t)address);
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    void write_float(uint8_t* rdram, uint32_t address, float value) {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        MEM_W(0, (gpr)(int32_t)address) = (int32_t)bits;
    }

    float register_float(const gpr* reg) {
        uint32_t bits = (uint32_t)*reg;
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    void set_register_float(gpr* reg, float value) {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        *reg = (gpr)(int32_t)bits;
    }
}

void turok::add_mouse_motion(float dx, float dy) {
    std::lock_guard lock{ mutex };
    if (Clock::now() - last_look > kStale) {
        pending_x = 0.0f;
        pending_y = 0.0f;
    }
    pending_x += dx;
    pending_y += dy;
}

void turok::add_stick_sensitivity_option(recomp::config::Config& general) {
    general.add_number_option(
        stick_sensitivity_option,
        "Stick Sensitivity",
        "How fast the control stick turns the view and looks up and down, pushed the same way: 100% is the game's own, "
        "and more turns faster than the game does with the stick pushed all the way. The <b>Joystick Deadzone</b> "
        "above sets how far the stick goes before it turns at all. It doesn't change the mouse, which has its own "
        "sensitivity, nor the menus.",
        10, 300, 5, 0, true, 100);
    // Called on load too, with the saved value, and at each change.
    general.add_option_change_callback(stick_sensitivity_option,
        [](recomp::config::ConfigValueVariant cur_value, recomp::config::ConfigValueVariant,
           recomp::config::OptionChangeContext) {
            if (const double* percent = std::get_if<double>(&cur_value)) {
                stick_sensitivity.store((float)(*percent / 100.0));
            }
        });
}

extern "C" float turok_stick_sensitivity(void) {
    return stick_sensitivity.load(std::memory_order_relaxed);
}

namespace {
    // One frame of a held angle: the mouse's turn (already in the angle's units) and the stick's tilt, read from the
    // target the game is about to ease the angle towards, which it makes of the stick's deflection and which springs
    // back to 0 when the stick is let go. That target, as a share of the way to the limit it points at, is how fast
    // the stick tilts the angle instead: held is the whole target, the stick's tilt and the mouse's together, within
    // the game's own limits, and the angle moves with it, at once, and stays where they leave it. It's let go when
    // the game itself set the angle to 0 (a respawn, a level's start, the head turned back into the body as the
    // player leaves a ladder) or the player was away.
    void hold(uint8_t* rdram, Spring& spring, bool away, float seconds, uint32_t address, float scale, float turn,
        gpr* target_reg, float min, float max) {
        float angle = read_float(rdram, address);
        if (away || (angle == 0.0f && spring.held != 0.0f)) {
            spring.held = 0.0f;
        }
        float target = register_float(target_reg);
        float tilt = target > 0.0f ? target / max : target < 0.0f ? target / -min : 0.0f;
        turn += tilt * kStickRate * scale * seconds;
        float held = std::clamp(spring.held + turn, min, max);
        float moved = held - spring.held;
        spring.held = held;
        if (moved != 0.0f) {
            write_float(rdram, address, std::clamp(angle + moved, min, max));
        }
        set_register_float(target_reg, spring.held);
    }
}

extern "C" void turok_spring_look(uint8_t* rdram, uint32_t yaw, float yaw_scale, uint32_t pitch, float pitch_scale,
    gpr* pitch_target, float pitch_min, float pitch_max) {
    float dx, dy, seconds;
    bool away;
    take(pitch_spring, &dx, &dy, away, seconds);
    if (dx != 0.0f) {
        write_float(rdram, yaw, read_float(rdram, yaw) + dx * kRadiansPerCount * yaw_scale);
    }
    // The mouse moved up (dy below 0) looks up.
    hold(rdram, pitch_spring, away, seconds, pitch, pitch_scale, -dy * kRadiansPerCount * pitch_scale, pitch_target,
        pitch_min, pitch_max);
}

extern "C" void turok_spring_turn(uint8_t* rdram, uint32_t angle, float scale, gpr* target, float min, float max) {
    float dx, seconds;
    bool away;
    take(turn_spring, &dx, nullptr, away, seconds);
    hold(rdram, turn_spring, away, seconds, angle, scale, dx * kRadiansPerCount * scale, target, min, max);
}

extern "C" void turok_spring_pitch(uint8_t* rdram, uint32_t pitch, float scale, gpr* target, float min, float max) {
    float dy, seconds;
    bool away;
    take(pitch_spring, nullptr, &dy, away, seconds);
    hold(rdram, pitch_spring, away, seconds, pitch, scale, -dy * kRadiansPerCount * scale, target, min, max);
}
