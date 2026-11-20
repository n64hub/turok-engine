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
those frames. So `turok_start_frame_late` also picks the presentation mode at each frame's start: Present Early while
the count is 2, Skip Buffering at any other (`recompui::renderer::set_presentation_mode()`, from
`runtime-patches/RecompFrontend-2-presentation-mode-at-run-time.patch`, which RT64 takes at the next display list).
The game's program makes its render context with Skip Buffering. The frame starts late only while the count is 2
too, where RT64 presents early: at any other count it starts at once, as before the late start.

Frame dumps of the Port of Adia's opening cutscene (95 to 112 s from the boot, 1280x720, four plays of each build)
had a lone frame between the game's own, a limb close to the camera, at 11 s in two of v0.2.16's plays (Present Early
throughout) and in none of v0.2.15's (Skip Buffering throughout) or this switch's. A dark frame where the camera
cuts through a figure at about 3.4 s came in one of v0.2.15's plays and three of the switch's: it is older than
presenting early. The repeated presents, 114 to 176 a play, differed more between one build's plays than between
builds.

### The map

The frame sends its tasks through func_00286B58: the world's, and, with the map open (the word at +0x128 of the frame
set), then the map's lines in a task of their own. RT64 presents early at the end of each task that drew to a framebuffer the screen
shows, so each frame went out twice, the first time without the map, and the game queued 60 presents a second for its
30 frames. At 60 Hz RT64 skips a present whose next one is queued and showed the right one; at a display rate over
that it showed both, a millisecond apart and then nothing between them and the next frame, and the map flickered over
a picture that stuttered. A frame dump of the Port of Adia at 120 Hz, turning with the map open for 9.5 s: v0.2.16
showed 50 of its frames once, without the frames between, and 27 of its pictures without the map; v0.2.15, which
presented with Skip Buffering, 13 and none.

A frame sent as more than one task (`turok_frame_tasks`, given the frame's count by a hook in func_00286B58 before
it sends the first) is presented with Skip Buffering, and the next frame starts at once, as v0.2.15 paced every
frame: a retrace more from a press to the picture while the map is open. The same dump: 30 presents queued a second,
1 frame shown once, none without the map; with the map closed, each frame shown four times, as before. A tally of the
frames in that play had two tasks only while the map was open, and one on the title, in the menus, the cutscenes and
on foot.

func_00205574(0), which decides between func_00286B58 and a single task sent straight to the scheduler, isn't the
map: it returns a pointer from a table, set nearly always. Counting every frame that went through func_00286B58 as two
tasks (Turok 2 v0.2.18) presented every frame with Skip Buffering and lost the late start everywhere; the page then
presented 225 frames a second uncapped, as v0.2.15's did, in place of 360.

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
| Both, only at 30 frames a second (one play) | 98.5 ms | 94.5 ms | 96 ms |

## Turok 1 and Turok 3

Both pace their frames as Turok 2 does and take the late start the same way, waiting only while the count is 2. A
frame that has started late presents early, and a frame at any other count presents with Skip Buffering, as in Turok 2:

- Turok 3's frame code is nearly Turok 2's. The frame is func_00285424, which the game thread starts on the last
  frame's task done message (func_002844A8); a frame is swapped in at its task's end (func_00282330) or by the
  retrace handler (func_00281BAC). The count, at 0x80139390 (copied into the task's +0x124), is set from the last
  four frames as in Turok 2: 2 at least, 4 in the cutscenes. The retraces since the swap are at 0x801661E0. The
  frame waits before 0x00285438 in func_00285424 and has sent its task before 0x00285754, and `turok_retrace` is
  before 0x00281E48, just after the handler's pad read (func_00251604). Turok 2's way of sending with the map open
  is there too (func_002849BC), but it sends one task, so it presents early like any other frame.
- Turok 1 runs an older version of the same scheduler (three frame buffers, the same swap rule). The frame is
  func_80009F6C, which starts on the last task's done message in play (the first two frames after a level or mode
  change start on retraces). The count is at 0x8011987C (copied into the task's +0x184, which the swap rule reads;
  2 at least, 3 where the game asks for it) and the retraces since the swap at 0x800F9CA0. The frame waits at
  func_80009F6C's start and has sent its task before 0x8000AA58, and `turok_retrace` is before 0x8002BE60 in the
  retrace handler func_8002BBFC. Its task goes out through func_80008060, with a second one after it when the
  task's +0x188 is set, the shape of Turok 2's frame with its map open: `turok_frame_tasks` is hooked there.

A trace of the frame's start, its task and each retrace, on the Linux build through the 20-press plays below: Turok
1's frames at a count of 2 started 5636 times out of 5654 at the retrace before their swap and were swapped in at the
next, every other retrace, and Turok 3's 6387 times out of 6395; the others each followed a level load or a stall of
the host that held every game's retraces for up to 6 s. A frame took 0.1 ms (Turok 1) or 0.24 ms (Turok 3) from its
start to its task, at the median, so the 8 ms that turns the wait off came only at boot.

### Turok 1's pad

Turok 1's retrace handler doesn't read the pads. The frame starts a read itself (osContStartReadData, at
0x80009FA4, unless one is under way), and the game's thread takes its result (osContGetReadData into 0x801198E0, at
0x800098CC) on the read's message, which it reads only once the frame has returned: each frame played with the pad
as it was at the end of the frame before, and with the late start that is a retrace before the frame's own. The
game's `us.toml` reads the pads into that buffer again at 0x80009FAC, after the frame's start and its read and before
the frame takes from the buffer the pad it plays with (func_80010D40). The game's own take after the frame still
comes, and the next frame reads the pads again over it.

### Measured

By the picture on the Linux build, as Turok 2's: twenty presses 7.0017 s apart, ten of the right stick and ten of a
key (Turok 1 turns right with the stick and the Right key at its first level's start; Turok 3 looks up with the stick
and the Up key in the Breach's first room, where the light's flicker swamps a turn), one play each, the window
320x240, about 68 ms of each the harness's capture. The late start's rows are a build with the trace on, and the
last of each game's the build as released; one build's plays differ by up to 30 ms, as Turok 2's did.

| Build | Key | Stick | Both |
|---|---|---|---|
| Turok 1 v0.2.9 | 179 ms | 178 ms | 179 ms |
| Turok 1, late start | 112 ms | 116 ms | 113 ms |
| Turok 1, late start, the pad of the frame's start | 71.5 ms | 71 ms | 71 ms |
| The same, as released (v0.2.10) | 92 ms | 86.5 ms | 88 ms |
| Turok 3 v0.2.8 | 137 ms | 133.5 ms | 137 ms |
| Turok 3, late start | 83 ms | 81 ms | 82 ms |
| The same, as released (v0.2.9) | 84 ms | 84 ms | 84 ms |

The trace build's Turok 1 plays answered 19 of their presses: one came in the host's stall, where the picture held
still.
