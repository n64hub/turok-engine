# Widescreen

RT64 widens the picture by itself when its aspect ratio is Expand: it renders the frame wider and scales each 3D
projection that covers the whole screen to fit. What it can't know is the game's 2D, which needs the engine's help.

## What a game needs first

RT64 shows its own render only when it finds the framebuffer the VI shows. It takes that from the VI's origin less one
row, the convention of libultra's own video modes. A game that builds a mode with a field origin of 0 (Turok 2's
hi-res modes do, and Turok 3's own mode) points the VI at the framebuffer itself. On N64Recomp-browser v0.184.0 and
later RT64 then takes the framebuffer at the origin (rt64-web d49a689), and the game needs nothing. On an older one RT64
finds nothing there and shows the image read back from RDRAM instead: at 1x, a row off, and never wider than 4:3,
whatever the settings, so Turok 2 sets a field 0 origin of 0 to one row (the width times the pixel size) before it
calls osViSetMode.

## The HUD and the border

`src/widescreen.cpp` writes RT64's extended GBI into the game's display list, only while Expand is on:

- Around the HUD (`turok_hud_begin`, `turok_hud_end`): the scissor pushed and opened to the whole window, and automatic
  anchoring on (`runtime-patches/rt64-3-auto-anchor-hud.patch`). RT64 then stretches each 2D rectangle with no
  alignment of its own that is half the screen wide or more across the window (a tint over the view, a cutscene's
  black bars), and places a smaller one at the nearest side by where it sits: the top quarter's to its own side, the
  bottom quarter's to the left (or the right, past 60% of the width), and what's in between (the crosshair, messages)
  stays in the middle.
- At the end of the frame (`turok_frame_end`): the border the games draw round the screen, whose fill rectangles at the
  old 4:3 edges would be lines down the widened picture. Each vertical G_FILLRECT at most 4 pixels wide and a quarter
  of the screen high or more, at the left or right edge, becomes a no-op.

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

## The culling

The games leave out whatever lies outside their own 4:3 view, so a widened picture would be missing it at its sides,
walls stopping short of the window's edge while the camera turns. They test it against the camera's float
view-projection matrix, from which they also make the display list's. A game hooks its camera after that copy is made
and calls `turok_widen_culling` with the float matrix's address: its x column is scaled to a view as wide as 21:9, so
the picture is as before and the culling lets through what a window up to that wide shows. Only while Expand is on.
