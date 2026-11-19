// Mouse look: the mouse turns the player's view itself, as a mouse does in a PC shooter, instead of being added to the
// control stick (docs/mouse_look.md).
//
// The games turn and look with the stick at a rate, and look up and down with it by an amount that springs back when
// it's let go (Turok 2 and 3) or that stays (Turok 1). A mouse made into a stick inherits both: each poll's movement is
// a deflection, so small movements fall into the game's dead zone and a turn depends on how long the mouse moved, not
// how far. Here a game hands the engine the mouse's movement at each poll (add_mouse_motion), and its look code, which
// runs once a frame while the player has control, calls turok_mouse_look with where the player's angles are: the
// movement is added to them there, so it turns the view by as much as the mouse moved, at once.
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

    // How fast the stick tilts a springing look up or down, pushed all the way (turok_spring_look): in radians a
    // second, about 140 degrees, two thirds of how fast Turok 2 turns with it (0.132 radians a frame at 30 a second).
    constexpr float kStickPitchRate = 2.4f;
    // The longest a frame of the look counts for at that rate: a hitch doesn't throw the view up or down.
    constexpr float kLongestFrame = 0.1f;

    const std::string stick_sensitivity_option = "stick_sensitivity";
    // The setting, as a factor: read from the config at each change, so the look reads a float a frame.
    std::atomic<float> stick_sensitivity{ 1.0f };

    std::mutex mutex;
    float pending_x = 0.0f;    // counts to the right since the look last ran
    float pending_y = 0.0f;    // counts down
    Clock::time_point last_look{};
    float held_pitch = 0.0f;   // for a game whose look springs back: how far the mouse holds it from where the stick would

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

// Both hooks: stick_is_rate for a look that springs back, whose target the stick is made to tilt instead.
static void look(uint8_t* rdram, uint32_t yaw, float yaw_scale, uint32_t pitch, float pitch_scale, gpr* pitch_target,
    float pitch_min, float pitch_max, bool stick_is_rate) {
    float dx, dy;
    bool away;
    float seconds;
    {
        std::lock_guard lock{ mutex };
        Clock::time_point now = Clock::now();
        away = now - last_look > kAway;
        seconds = away ? 0.0f : std::min(std::chrono::duration<float>(now - last_look).count(), kLongestFrame);
        last_look = now;
        dx = pending_x;
        dy = pending_y;
        pending_x = 0.0f;
        pending_y = 0.0f;
    }

    if (dx != 0.0f) {
        write_float(rdram, yaw, read_float(rdram, yaw) + dx * kRadiansPerCount * yaw_scale);
    }

    // The mouse moved up (dy below 0) looks up.
    float turn = -dy * kRadiansPerCount * pitch_scale;
    float angle = read_float(rdram, pitch);
    if (pitch_target == nullptr) {
        if (turn != 0.0f) {
            write_float(rdram, pitch, std::clamp(angle + turn, pitch_min, pitch_max));
        }
        return;
    }

    // A look that springs back: the game moves the pitch towards a target it makes of the stick, so the mouse holds
    // the target that much further, within the game's own limits, and moves the pitch with it, at once. What it held
    // is let go when the game itself set the pitch level (a respawn, a level's start) or the player was away.
    if (away || (angle == 0.0f && held_pitch != 0.0f)) {
        held_pitch = 0.0f;
    }
    float target = register_float(pitch_target);
    if (stick_is_rate) {
        // The stick's target, as a share of the way to the limit it points at, is how fast it tilts the view instead:
        // held_pitch is then the whole target, the stick's tilt and the mouse's together, and stays where they leave it.
        float tilt = target > 0.0f ? target / pitch_max : target < 0.0f ? target / -pitch_min : 0.0f;
        turn += tilt * kStickPitchRate * pitch_scale * seconds;
        target = 0.0f;
    }
    float held = std::clamp(held_pitch + turn, pitch_min - target, pitch_max - target);
    float moved = held - held_pitch;
    held_pitch = held;
    if (moved != 0.0f) {
        write_float(rdram, pitch, std::clamp(angle + moved, pitch_min, pitch_max));
    }
    set_register_float(pitch_target, std::clamp(target + held_pitch, pitch_min, pitch_max));
}

extern "C" void turok_mouse_look(uint8_t* rdram, uint32_t yaw, float yaw_scale, uint32_t pitch, float pitch_scale,
    gpr* pitch_target, float pitch_min, float pitch_max) {
    look(rdram, yaw, yaw_scale, pitch, pitch_scale, pitch_target, pitch_min, pitch_max, false);
}

extern "C" void turok_spring_look(uint8_t* rdram, uint32_t yaw, float yaw_scale, uint32_t pitch, float pitch_scale,
    gpr* pitch_target, float pitch_min, float pitch_max) {
    look(rdram, yaw, yaw_scale, pitch, pitch_scale, pitch_target, pitch_min, pitch_max, true);
}
