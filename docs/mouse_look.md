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

## The stick's sensitivity

The stick turns the view as fast as each game's own look code turns it, and some pads' sticks feel too slow or too
quick for it. The General tab's Stick Sensitivity (`turok::add_stick_sensitivity_option`, 100% by default, 10-300%)
scales that: a game hooks its look code where it reads the stick's deflection to turn and to look up and down, just
before it multiplies it into the turn, and the hook multiplies it by `turok_stick_sensitivity()` too. The same push
then turns the view that much faster or slower, and at more than 100% a full push turns faster than the game's own.
The deflection is the game's, after its own dead zone and the frontend's Joystick Deadzone, so those still say where
the stick starts to turn. The mouse has its own sensitivity and never goes through the stick, and the menus read the
stick apart from the look code, so neither changes.

| Game | Turn | Look up and down |
|---|---|---|
| Turok 2 | f0 before 0x0024F0B4 (the stick's x, from +0xA54 of s1) | f0 before 0x0024F0F0 (its y, from +0xA58) |

## The games' hooks

| Game | Look code | Hook before | Yaw | Pitch | Target |
|---|---|---|---|---|---|
| Turok 2 | func_0024EF70 | 0x0024F14C | s2 + 0x50 | s1 + 0xAC8 | a1; limits at s0 + 0x10, + 0x14 |
| Turok 3 | func_002CF978 | 0x002CFBF4 | s2 + 0x6C | s0 + 0xDD0 | a1; limits at s1 + 0x10, + 0x14 |
| Turok 1 | func_80052B1C | 0x80053688 | *(sp + 0xB4) + 0x64 | s0 + 0x70 | s0 + 0x68; limits -1.3614, pi/2 |
| Turok 1, in water | func_80052B1C | 0x800533E4, 0x80053478, 0x80053530 | *(sp + 0xB4) + 0x64 | s0 + 0x70 | s0 + 0x68; limits ±pi/2, ±80°, ±80° |

These are the on-foot looks, and Turok 1's in water. Turok 1's look code picks its look by where the player is
against the water, and each of its three water looks makes its own target from the stick and goes past the on-foot
hook, so each has a hook of its own. Turok 2's and Turok 3's swimming and the vehicles have their own looks, which
aren't hooked, so the mouse does not turn the view there, but for Turok 3's ladders (below). Turok 1 has already read its pitch into f12 when its hooks
run, and its water hooks the target into f14, so they read them again after the call.

A count turns the view 0.12 degrees at 100% Mouse Sensitivity, so the default of 50% turns a full circle in 6000
counts.

## Turok 3's ladders

On a ladder Turok 3 doesn't turn the body: its look there (func_002D26F4) eases a head turn at the struct's +0xDD4,
which the camera adds to the body's yaw, towards the stick's left and right times 200 degrees (a quarter of what is left at a
step), and the pitch towards its up and down within 90 degrees (half), and both spring back once the stick is
let go. Leaving the ladder turns the body by the head's turn and sets the turn back to 0. Its two eases are hooked
apart, in the same frame, before the calls that make them: `turok_spring_turn` holds the head's turn as
`turok_spring_look` holds the pitch, with the mouse's left and right movement, and `turok_spring_pitch` holds the
pitch with its up and down. The pitch is the one held on foot, so the view keeps its angle getting on and off a
ladder.

| Game | Look code | Hook before | Angle | Target | Limits |
|---|---|---|---|---|---|
| Turok 3 | func_002D26F4 | 0x002D280C | s0 + 0xDD4 (turn) | a1 | -3.4907, 3.4907 |
| Turok 3 | func_002D26F4 | 0x002D286C | s0 + 0xDD0 (pitch) | a1 | -pi/2, pi/2 |
