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
    the screen, had its first words at the window's left edge and "PORTALS" at the right, and a line in the top
    quarter does too: Turok 3's "New Objective" in the Breach, "New O" at the left and "bjective" at the right. A game
    whose messages are drawn apart from its counters draws them after `turok_hud_end` (Turok 1's us.toml); one that
    draws them among its counters closes the HUD around each, `turok_hud_end` where it starts and `turok_hud_begin`
    where it returns (Turok 3's us.toml). Either way they are centred as at 4:3.
- At the end of the frame (`turok_frame_end`): the border the games draw round the screen, whose fill rectangles at the
  old 4:3 edges would be lines down the widened picture. Each vertical G_FILLRECT at most 4 pixels wide and a quarter
  of the screen high or more, at the left or right edge, becomes a no-op. So does each horizontal one at most 4 pixels
  high and a quarter of the screen wide or more, at the top or bottom edge: Turok 2 fills its rows 0 and 359 after the
  HUD, and with no black bars above and below a widened picture they are black strips along the window's top and
  bottom edges (2-3 rows each in a 1000-high window). The frame's display list is read from where
  `turok_frame_begin` found the head to its G_ENDDL, not to the head: a game may write a second list somewhere else
  before the frame is done, and the head is then in that one. Turok 2's map does, drawn over the frame as lines. The
  G_ENDDL is the first one the list reaches as the RSP runs it: a G_DL that doesn't push, forward inside the frame's
  list, is followed, and a G_ENDDL it branches past is never reached. Turok 3 writes its list in pieces, each a branch
  past the piece and its G_ENDDL, then a G_DL that pushes, back to the piece, and its cutscenes draw their letterbox
  after them, with a 1-2 pixel border down each side of the picture between its bars (x 0-2 and 282-283 of 284) that
  stays as two black lines inside the widened picture unless it is taken out. In the opening scene each piece before
  the letterbox is empty; in the Breach's first in-level cutscene (the tentacles at the window) the first holds
  commands of its own, so the G_ENDDL ending it doesn't follow its branch, and only following the branch gets past it.
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

What RT64 can't tell from the matrices is a camera cut: it matches the new shot's view to the old one's and draws its
frames between the two with the camera partway from one to the other. At Turok 1's Campaigner death scene, which cuts
from a shot across the rooftop to one of the hall, that was a flat tan field with one pillar, for one frame of the
display's between two of the game's own. The engine sees the cut in the camera's float view-projection matrix, which
a game hooks for the culling (`turok_widen_culling`, below), at any width: where the eye is (the one point whose clip
x, y and w are 0) and which way it looks (the w column), held up to the frame before's. An eye that moved further than
160 units, or a view that turned more than 45 degrees, is a cut. A game draws a view for each camera, so the n-th
camera of a frame is held up to the n-th of the last. While RT64 interpolates (Framerate above Original),
`turok_frame_begin` leaves room for three commands at the start of the frame's display list, no-ops, and on a frame
whose camera cut `turok_frame_end` writes RT64's extended GBI and a matrix group for the projections there that
ignores them (`gEXMatrixGroupNoInterpolate`, `G_MTX_PROJECTION`): RT64 matches that frame's view to none of the last's,
and its frames between the two show the new shot.

Turok 2 and 3 cut the same way, and the same thresholds hold for them. At Framerate Manual 60, captured back to
back: without the fix, 11 of the 16 cuts of Turok 2's Port of Adia opening that change the picture showed
frames between the two shots, and 24 of the 26 of Turok 3's opening room scene; with it, none did. Their ordinary
movement stays well under the thresholds: Turok 2's walking, turning and swimming at most 35 units and 7 degrees a
frame, Turok 3's turning 3 degrees. The number of cameras a frame draws changes with what is on screen: Turok 2 draws
two in a level, one in its menus and four on some frames at a shot change, Turok 3 one or two and up to ten while a
scene loads. On a frame whose count differs from the last's, a camera is held up to another's view and may read as
a cut, which only leaves that frame's movement unsmoothed. A picture changes on the display about
160 ms after the frame that drew it, so a capture's cut is that far after the frame the engine saw it in.

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

The level is culled a second way, apart from that matrix. The camera makes a view pyramid in its own space, five points
(the eye, then four far corners from the field of view, 4:3), moves it into the world and takes from it the side
planes and bounding box the level's rooms are chosen against. Turok 2's is a third wider than its view, so the matrix
alone left a 21:9 picture missing the level past about 16:9: walls and water stopping on one vertical line, the room
past a corner not drawn at all, the fog colour showing instead. A game hooks its camera once the points are made and
before they are moved, and calls `turok_widen_frustum` with their address and count: each point's x is scaled as the
matrix's x column is. Turok 2's camera (us.toml) makes them at its stack's 0x3F8 and moves them with `func_0020F908`;
Turok 3's makes them in `func_0028E8B8`, at its stack's 0x18, and moves them with `func_002628E8`. The same code,
at its stack's 0x18, is in Rage Wars' `func_00234DC0`, which needs no hook: its far corners' angle is the
vertical one times the aspect, in degrees, so at the game's field of view they reach about 67 degrees to each side,
wider than a 21:9 picture, and with them narrowed to a quarter of that its arenas were drawn the same out to a 21:9
window's sides while turning.
