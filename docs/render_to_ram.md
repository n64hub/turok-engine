# Render to RAM

RT64's render to RAM copies each frame it draws back into RDRAM, where the console's RDP leaves it, for a game whose
CPU reads it there: a pause menu's background made from the last frame, a screenshot for a save, a lens flare that
tests the depth buffer on the CPU. The copy is a second draw of every frame at the game's own resolution, and on a page
each display list waits 5-6 ms for the browser's readback (N64Recomp-browser's README, The recomp's own code). RT64
makes it unless told otherwise.

None of the Turok games reads a frame it drew, or writes into one, so `src/render_to_ram.cpp` turns the copy off, on
the desktop and the page. `turok::add_copy_frames_option` sets it off and puts Copy Frames to RAM on the Graphics tab,
Off by default, to turn it back on at the next start if something ever looks or behaves wrong without it. Turok:
Dinosaur Hunter, Turok 2 and Turok 3 call it with their Graphics tab. Rage Wars' page (PaxDino-web) turns the copy off
in its own `create_render_context`; its desktop build's runtime has no switch for it.

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
