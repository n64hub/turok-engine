# Widescreen

RT64 widens the picture by itself when its aspect ratio is Expand: it renders the frame wider and scales each 3D
projection that covers the whole screen to fit. What it can't know is the game's 2D, which needs the engine's help.

## What a game needs first

RT64 shows its own render only when it finds the framebuffer the VI shows. It takes that from the VI's origin less one
row, the convention of libultra's own video modes. A game that builds a mode with a field origin of 0 (Turok 2's
hi-res modes do) points the VI at the framebuffer itself, RT64 finds nothing there, and it shows the image read back
from RDRAM instead: at 1x, a row off, and never wider than 4:3, whatever the settings. The game sets a field 0 origin of
0 to one row (the width times the pixel size) before it calls osViSetMode.

## The HUD and the border

`src/widescreen.cpp` writes RT64's extended GBI into the game's display list, only while Expand is on:

- Around the HUD (`turok_hud_begin`, `turok_hud_end`): the scissor pushed and opened to the whole window, and automatic
  anchoring on (`runtime-patches/rt64-3-auto-anchor-hud.patch`). RT64 then places each 2D rectangle with no alignment
  of its own at the nearest side by where it sits: the top quarter's to its own side, the bottom quarter's to the left
  (or the right, past 60% of the width), and what's in between (the crosshair, messages) stays in the middle.
- At the end of the frame (`turok_frame_end`): the border the games draw round the screen, whose fill rectangles at the
  old 4:3 edges would be lines down the widened picture. Each vertical G_FILLRECT at most 4 pixels wide and a quarter
  of the screen high or more, at the left or right edge, becomes a no-op.

The screen's size is the frame's widest G_SETSCISSOR, which the games set before they draw, so the engine needs from
a game only its display list head (`turok::set_display_list_head`, the address of the pointer its code writes commands
through) and hooks at four places: after it starts the frame's display list (`turok_frame_begin`), before it's done
with it (`turok_frame_end`), and before and after the HUD.

Where those are in each game is the game's own: its N64Recomp configuration's hooks say.
