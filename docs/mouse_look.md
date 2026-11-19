# Mouse look

The games turn and look with the control stick. Turning is a rate: the stick's deflection, past a dead zone, turns
the player so many degrees a second. Looking up and down is an amount: the stick held up or down moves the view
towards an angle its deflection sets, and the view springs back level once the stick is let go. All three games do
this. A mouse made into a stick, which is what the frontend does by default, keeps both: each poll's movement becomes
a deflection, so a small movement is lost in the dead zone, a turn depends on how long the mouse moved rather than how
far, and a look up or down drifts back as soon as the mouse stops.

`src/mouse_look.cpp` makes the mouse turn the view directly, as it does in a PC shooter. The game passes the mouse's
movement to the engine at each poll, instead of adding it to the stick. A hook in the game's look code then adds that
movement to the player's angles, in the same frame, by as much as the mouse moved.

A pad's stick has the other half of the same trouble: the stick sets the angle the view looks up or down at, the view
eases towards it, a third of the way in a quarter of a second and nine tenths in about 1.4 s, and it eases back level
as soon as the stick is let go. Aiming up or down lags behind the stick, and an aim can't be held. The hook,
`turok_spring_look`, makes the stick tilt the view at a rate instead, at once, and leaves the view where the stick
lets go of it, as a twin-stick shooter does, in all three games.

## What a game does

- Its `get_input`, for player 1 while mouse look is on, hands the frontend's mouse deltas (already scaled by the Mouse
  Sensitivity setting) to `turok::add_mouse_motion` and leaves the stick to the controller and the keyboard.
- It hooks its look code, which runs once a frame while the player has control, with `turok_spring_look`, passing:
  - the address of the yaw, a float in radians that grows to the right;
  - the address of the pitch, a float in radians that grows upwards;
  - the limits of the pitch;
  - the target the game is about to move the pitch towards (a pointer to a register, or to a local copy the hook
    writes back).

  `yaw_scale` and `pitch_scale` are for a game whose angles are not in radians. They are 1 in all three games.

## Holding the look

The look springs back because the game rebuilds its target from the stick every frame. `turok_spring_look` reads the
stick from that target: the target as a share of the limit it points towards is the stick's deflection, which tilts
the view by up to 2.4 radians a second (about 140 degrees, two thirds of how fast Turok 2 turns with the stick), times
the time since the look last ran, at most a tenth of a second so a hitch doesn't throw the view. The mouse's movement
in that frame is added to it. The engine keeps the sum (`held_pitch`), the stick's tilt and the mouse's together,
within the game's own limits: the game's own target is replaced by it, the pitch is moved with it at once instead of
easing towards it at the game's rate, and it stays where the two leave it. Turning left and right is the game's own,
which already turns at a rate and at once.

The held pitch is dropped, and the view goes back level, when the game sets the pitch level itself (a respawn, the
start of a level), or when the look has not run for half a second (a cinematic, a level loading). Movement made while
the look is not running (the pause menu, the menus between levels) is dropped too, so it does not turn the view when
the player comes back.

## The games' hooks

| Game | Look code | Hook before | Yaw | Pitch | Target |
|---|---|---|---|---|---|
| Turok 2 | func_0024EF70 | 0x0024F14C | s2 + 0x50 | s1 + 0xAC8 | a1; limits at s0 + 0x10, + 0x14 |
| Turok 3 | func_002CF978 | 0x002CFBF4 | s2 + 0x6C | s0 + 0xDD0 | a1; limits at s1 + 0x10, + 0x14 |
| Turok 1 | func_80052B1C | 0x80053688 | *(sp + 0xB4) + 0x64 | s0 + 0x70 | s0 + 0x68; limits -1.3614, pi/2 |

These are the on-foot looks. Swimming and the vehicles have their own, which aren't hooked, so the mouse does not
turn the view there. Turok 1 has already read its pitch into f12 when the hook runs, so its hook reads it again after
the call.

A count turns the view 0.12 degrees at 100% Mouse Sensitivity, so the default of 50% turns a full circle in 6000
counts.
