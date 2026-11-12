# Render to RAM

RT64's render to RAM copies each frame it draws back into RDRAM, where the console's RDP leaves it, for a game whose
CPU reads it there: a pause menu's background made from the last frame, a screenshot for a save, a lens flare that
tests the depth buffer on the CPU. The copy is a second draw of every frame at the game's own resolution, and on a page
each display list waits 5-6 ms for the browser's readback (N64Recomp-browser's README, The recomp's own code). RT64
makes it unless told otherwise.

None of the Turok games reads a frame it drew, or writes into one, so `src/render_to_ram.cpp` turns the copy off, on
the desktop and the page. `turok::add_copy_frames_option` sets it off and puts Copy Frames to RAM on the Graphics tab,
Off by default, to turn it back on at the next start if something ever looks or behaves wrong without it. Turok 3
calls it with its Graphics tab. Rage Wars' page (PaxDino-web) turns the copy off in its own `create_render_context`;
its desktop build's runtime has no switch for it.

Turok: Dinosaur Hunter and Turok 2 keep the copy for now, though they don't need it: on the page, uncapped, RT64
draws fewer frames between their game frames without it (Measured, below), for a reason not yet known.

## How it was checked

With no decompilation to read, each game was played on the desktop build with every framebuffer's and depth buffer's
pages in RDRAM kept as guard pages, re-armed every few milliseconds. The display lists the game sent named the
buffers (G_SETCIMG, G_SETZIMG, each sized by the rows its scissor and rectangles reach), and each access to those pages
was counted by the host instruction that made it, symbolized against the build's PDB, and its thread. A CPU read of a
frame shows up as a recompiled function (`func_<address>`) reading inside a buffer.

| Game | Buffers | Played | The game's own accesses |
|---|---|---|---|
| Turok: Dinosaur Hunter | three 320x240 at 0x80200000, depth at 0x803DA800 | boot, title, the first level, the pause menu | only past 0x80270800 (the third buffer's end) and before 0x803DA800 |
| Turok 2 | three 480x360 at 0x80600000, depth at 0x80700000 | boot, menus, a new game's opening cinematics, the Port of Adia, the pause menu, the map | only the pages the buffers share with the data after them |
| Turok 3 | two 284 wide at 0x80500280, depth at 0x80600000, and a 128x128 8-bit image at 0x801FC900 it draws into | boot, menus, the Breach, the pause menu, the objectives | only the pages the buffers share with the data around them |
| Rage Wars | three 480x360 at 0x80500000, depth at 0x80600000 | boot, menus, a match of 80 s with a death | only the pages the buffers share with the data after them |

Everything inside the buffers was RT64's own thread: its hash of each framebuffer for changes the CPU makes, and on
Rage Wars' desktop build, which keeps the copy, the copy itself. Turok 3's 128x128 image is drawn by the RDP and
used by the RDP as a texture, which RT64 keeps on the GPU without the copy. Each game, played with the copy off, draws
the same as with it on.

## Measured

Each page built with the copy off against the same game's page with it, in Chrome uncapped at 3840x2000 on a desktop
GPU, played in turn (presents a second, each play's median):

| Game | With the copy | Without it |
|---|---|---|
| Turok: Dinosaur Hunter | 244, 245 | 225, 225 |
| Turok 2 | 281, 281 | 226, 226 |
| Turok 3 | 359 | 359 |
| Rage Wars | 358 | 355 |

Turok 3 and Rage Wars present near the 360 cap either way. Turok: Dinosaur Hunter and Turok 2 run 30 game frames a
second both ways, and in Turok: Dinosaur Hunter the GPU's time a present is the same (0.50 ms) with fewer passes
without the copy, but RT64 draws 7.5 frames for each game frame without it against 8.1 with it. Until that is
understood, those two keep the copy.
