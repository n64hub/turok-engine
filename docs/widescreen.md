# Widescreen

RT64 widens the picture by itself when its aspect ratio is Expand: it renders the frame wider and scales each 3D
projection that covers the whole screen to fit. What it can't know is the game's 2D, which needs the engine's help.

## What a game needs first

RT64 shows its own render only when it finds the framebuffer the VI shows. It takes that from the VI's origin less one
row, the convention of libultra's own video modes. A game that builds a mode with a field origin of 0 (Turok 2's hi-res
modes do, and Turok 3's own mode) points the VI at the framebuffer itself, and RT64 on N64Recomp-browser v0.186.0 and
later takes the framebuffer at the origin then, on the desktop and the web (rt64-web f5afe59), so a game needs nothing
for it. On an older pin the desktop's present looks a row before the origin, finds nothing there and shows the image
read back from RDRAM instead, at 1x, a row off, and never wider than 4:3, whatever the settings.

## The HUD and the border

`src/widescreen.cpp` writes RT64's extended GBI into the game's display list, only while Expand is on:

- Around the HUD (`turok_hud_begin`, `turok_hud_end`): the scissor pushed and opened to the whole window, and automatic
  anchoring on (`runtime-patches/rt64-3-auto-anchor-hud.patch`). For each 2D rectangle with no alignment of its own:
  - As wide as the screen (Turok 2's underwater tint, a menu's dimming, a cutscene's black bars): left unanchored.
    RT64 stretches any rectangle that covers the screen across the whole window, as it does the 3D. Anchoring it to
    the sides would take it only as far as the HUD may widen, which RT64's extended aspect ratio can hold narrower
    than the window (16:9, say, in a 1.92:1 window), and the tint would stop short of the picture's sides.
  - Half the screen wide or more: stretched across the HUD's width.
  - Smaller ones go to the nearest side by where they sit: the top quarter's to their own side, the bottom quarter's
    to the left (or to the right, past 60% of the width), and what's in between (the crosshair, messages) stays in the
    middle. A line of text is one rectangle a letter, each anchored by where it sits, so a message line that reaches
    the bottom quarter comes apart: Turok 1's first-level hint "TO OPEN LEVEL PORTALS", just below three quarters of
    the screen, had its first words at the window's left edge and "PORTALS" at the right. A game whose messages are
    drawn apart from its counters draws them after `turok_hud_end` (Turok 1's us.toml), centred as at 4:3.
- At the end of the frame (`turok_frame_end`): the border the games draw round the screen, whose fill rectangles at the
  old 4:3 edges would be lines down the widened picture. Each vertical G_FILLRECT at most 4 pixels wide and a quarter
  of the screen high or more, at the left or right edge, becomes a no-op. So does each horizontal one at most 4 pixels
  high and a quarter of the screen wide or more, at the top or bottom edge: Turok 2 fills its rows 0 and 359 after the
  HUD, and with no black bars above and below a widened picture they are black strips along the window's top and
  bottom edges (2-3 rows each in a 1000-high window). The frame's display list is read from where
  `turok_frame_begin` found the head to its G_ENDDL, not to the head: a game may write a second list somewhere else
  before the frame is done, and the head is then in that one. Turok 2's map does, drawn over the frame as lines. A
  G_ENDDL that the command before it branches past (a G_DL that doesn't push, to the command after it) is never
  reached, so the list is read on: Turok 3's cutscenes end each piece of their list that way, and draw their
  letterbox after them, with a 1-2 pixel border down each side of the picture between its bars (x 0-2 and 282-283 of
  284) that stayed as two black lines inside the widened picture.
- Also at the end of the frame, and before the HUD: the screen's last column and row. Turok 2's full-screen scissor is
  (0,0)-(479,359) on a 480x360 screen (its last pixel, where the RDP wants its size), so it never draws column 479 or
  row 359. At 4:3 they are black beside the window's black bars, but RT64 scales the scissor with the widened screen,
  and they become black strips along the window's right edge (8 pixels wide in a 3840-wide window) and bottom edge
  (3 rows in a 1000-high one). Each G_SETSCISSOR from the screen's top left corner to one pixel short of the colour
  image's width is opened to the image's edge and a row lower: the colour image doesn't say its height, and a scissor
  a column short is a row short too. The HUD's scissor reaches the window's right edge and the screen's bottom.
  RT64 then widens a rectangle that ends a pixel short of the scissor to it, as it does with "Fix LR with Scissor".
  It sizes a framebuffer by the rows drawn to it and writes back that many, so it now reads and writes all 360 rows
  of Turok 2's 480x360 framebuffers, which is their size: nothing past them is touched.

The screen's size is the frame's widest G_SETSCISSOR, which the games set before they draw, so the engine needs from
a game only its display list head (`turok::set_display_list_head`, the address of the pointer its code writes commands
through) and hooks at four places: after it starts the frame's display list (`turok_frame_begin`), before it's done
with it (`turok_frame_end`), and before and after the HUD.

The head comes with the microcode the display list is for, since RT64's extended GBI is switched on with that
microcode's no-op: an F3DEX2's (0xE0) for Turok 2's engine family, the default, and an F3DEX's (0x00) for Turok 1. A
game whose head isn't at one address (Turok 1's is in the graphics struct its code is handed) gives it from its frame's
hook, before `turok_frame_begin`, once the frame's display list buffer is the one it writes.

Where those are in each game is the game's own: its N64Recomp configuration's hooks say.

## Frame interpolation

RT64 interpolates the frames between the game's own (Framerate above Original) by matching each frame's matrices to
the last's by itself. The engine leaves that alone: a matrix group at the base of the frame's display list that forces
position, rotation and the rest to interpolate tears the level's geometry while the camera turns, where RT64's own
matching shows none.

RT64 interpolates a frame only when the framebuffer the VI shows is one the frame drew, so it needs to find it at the
VI's origin (What a game needs first) as much as widescreen does. Turok 3's page presented the game's own 30 a second
until it did; now its desktop build presents 57-60 a second at Framerate Display on a 59 Hz display (30 at Original),
and its page 315-340 uncapped.

## The culling

The games leave out whatever lies outside their own 4:3 view, so a widened picture would be missing it at its sides,
walls stopping short of the window's edge while the camera turns. They test it against the camera's float
view-projection matrix, from which they also make the display list's. A game hooks its camera after that copy is made
and calls `turok_widen_culling` with the float matrix's address: its x column is scaled to a view as wide as 21:9, so
the picture is as before and the culling lets through what a window up to that wide shows. Only while Expand is on.
