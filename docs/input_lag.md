# From a press to the picture

A turn of the stick reaches the picture after the game reads the pad, runs a frame with it and draws it, and after
the renderer presents that frame. Turok 2 runs at 30 frames a second, so each frame its own order adds is 33 ms. The
renderer's part (RT64's queue of frames, its interpolation, the present) is every recomp's, and N64Recomp-browser's
to cut; what is here is the games' own, and the presentation mode a game picks.

## Turok 2

### The frame's order

A frame (func_00287248) runs in this order:

1. The frame's display list starts (`turok_frame_begin`).
2. The world is drawn (func_0027ECF0), with the camera where the last update left it.
3. The pads are latched (func_002857C8: each pad's stick and buttons, as the retrace handler last read them, become
   the frame's).
4. The game mode's update runs. On foot, the player update (func_00251FC4) moves and turns each player, and then
   fills in each player's stick and buttons from the latched pads (func_00246BC0, into +0xA40 of the player: the
   stick's x at +0xA54, its y at +0xA58).
5. The HUD and the rest are drawn, and the frame's task is sent (func_00286B58).

A press read just before frame N's latch was used by no move or turn of frame N, whose update ran before the fill:
frame N + 1's update turned with it, and frame N + 2's world showed the turn. A trace of the poll, the fill, the look
and the camera, with the stick pushed right at the start of the Port of Adia, had the view's yaw move 51 ms after the
poll and the camera 84 ms after it, two frames after the frame that latched it. The yaw itself turns at a rate with no
easing (`docs/mouse_look.md`), so it also stops one frame after the stick is let go, and the picture two frames after
that: what feels like the view carrying on after the stick is let go is the same delay as the one before it starts.

Turok 2's `us.toml` changes the order in two places:

- The player update fills in the players' input before it moves them instead of after, from the same latched pads
  (the fill is called for the list at +0x20 of its argument at its start, and its own call is taken out).
- The world is drawn just after the update instead of before it, so the camera it draws with is the one this frame's
  update turned.

The game mode's update writes into the frame's display list as well (about 18 KB a frame on foot), now before the
world rather than after it. With both, the trace's camera moves in the first frame after the poll.

### When the frame starts

The game paces its frames by the retraces. A frame is swapped in once the one on the screen has been there the
frame's count of retraces (the word at 0x8011AD10: 2 on foot, 30 frames a second): when its task is done
(func_0028A914), if the retraces since the last swap (the word at 0x8011AD88) are that many less one, or else by the
retrace handler (func_0028A17C), from a list of frames ready. The game starts the next frame as soon as it has sent
the last one, right after a swap. On the console a frame took most of its two retraces to build. Recompiled, it takes
a millisecond or two and then waits more than a retrace for its swap, with the pad it latched and the world it moved
already that old.

`src/frame_pacing.cpp` holds the frame's start until one retrace is left before its swap (`turok_start_frame_late`,
from a hook at the start of func_00287248): it blocks the game's thread on a message queue that the retrace handler
posts (`turok_retrace`, hooked after the handler reads the pads), so the frame latches the read of the retrace it
starts at. A trace of the frame's start and its swap has each frame start at the retrace before its swap and swapped
in at the same retrace as before, every other retrace on foot. A frame that has taken over 8 ms from its start to
its task (`turok_frame_sent`) in the last 30 turns the wait off until they are quick again, as one that took a
retrace would miss its swap.

With Skip Buffering, RT64 presents a frame at the retrace that swaps it in. A frame started late has its task done
just before that retrace, and RT64 then dropped the frame it interpolates between it and the next for about one frame
in three (presented 16 or 20 ms apart before, 32 or 36 ms apart for those). Turok 2 presents early instead: RT64
presents a frame as soon as its task is done, so the late frame reaches the screen just before its retrace, and the
frames between are all there (60 a second, as before).

In the cutscenes, which the game runs at 15 frames a second (4 retraces a frame), presenting early costs the
picture's evenness: RT64 shows a frame for a few retraces and then the frames it interpolates up to the next in a
burst, so the cutscenes judder more than with Skip Buffering, and a limb close to the camera can flash for one of
those frames. Presenting early only while the game runs at 30 frames a second would keep both modes' good sides; the
runtime has no switch back to Skip Buffering yet.

### Measured

By the picture on a Linux build, from a press to the window's first change: twenty presses at the Port of Adia, ten
of the right stick and ten of the Right key, each 7 s and 1.7 ms after the last, so that the presses fall all through
the game's two-retrace cycle (presses a whole number of frames apart all fall at one point of it, and a play's median
then moves by up to a frame from one play to the next). The medians of a play, or of three plays' sixty presses; the
window is 320x240, and about 68 ms of each is the harness's capture of the window. One build's plays on different runs
of the host differ by up to 30 ms (the last row's three: 82, 106 and 114 ms), so a row of one play is good to about
that.

| Build | Key | Stick | Both |
|---|---|---|---|
| v0.2.14 | 201 ms | 195 ms | 197 ms |
| Order, Present Early | 141 ms | 132 ms | 139 ms |
| Order, late start | 117 ms | 107 ms | 110 ms |
| Order, late start, Present Early (three plays) | 102 ms | 101 ms | 101.5 ms |
