# Runtime patches

Changes to the submodules a Turok game builds with, which every game of the family needs. A game's patch step applies
N64Recomp-browser's own patches first, then these, then the game's own `runtime-patches/`, each
`<project>-<n>-*.patch` in order of name. `<project>` is a name in N64Recomp-browser's `patches/patches.txt`, which says
the folder under the game's `lib/` it applies in. Applying them again changes nothing.

- `N64Recomp-1-jump-table-index-from-register.patch`: N64Recomp's C output indexes a jump table by the register the
  code jumps through, less the table's address, as its live generator does, rather than by the offset it saved at the
  addu. The games step a loaded entry's address in a loop instead of redoing the addu, and with the saved offset
  every pass took the first entry. From PaxDino.
- `rt64-1-acclaim-f3dex2-variant.patch`: RT64 takes Acclaim's F3DEX2 variant ("F3DEX2.NoN.fifo 2.05 (Acclaim)"),
  which the games draw with, as F3DEX2 rather than unknown. From PaxDino.
- `rt64-2-msaa-resolve-needs-a-colour-target.patch`: with MSAA on and frame interpolation's target override active,
  RT64 resolves the shown framebuffer pair's colour target into the override, and read a null one when that pair has
  only a depth target, which these games' frames have: the game crashed a few seconds into its boot at MSAA 2x,
  RecompFrontend's default. The resolve now needs a colour target.
- `rt64-3-auto-anchor-hud.patch`: an extended GBI command, gEXSetAutoAnchor (G_EX_SETAUTOANCHOR_V1, 0x34), that has
  RT64 anchor each 2D rectangle with no alignment of its own to the side it's nearest by where it sits, or stretch it
  across the window when it is half the screen wide or more, for the HUD in widescreen (`src/widescreen.cpp`,
  `docs/widescreen.md`). From PaxDino.
- `rt64-4-l3dex-reads-as-f3dex.patch`: RT64 reads display lists for L3DEX 1.x, F3DEX's line microcode, with F3DEX's
  commands, and takes its G_LINE3D (0xB5, F3DEX's G_QUAD) as drawing nothing, as it already does for L3DEX2's. It knew
  L3DEX 1.x by its hash, but as an unknown microcode, which gave it only the RDP's commands: a list that loaded it never
  reached its end, a branch or the load back to F3DEX, and the interpreter ran on through RDRAM until it faulted. Turok
  1 draws its map with L3DEX 1.00, and crashed a few seconds after L opened the map. The next patch draws the lines.
- `rt64-5-l3dex-lines-drawn-as-quads.patch`: RT64 draws the line microcodes' G_LINE3D (gSPLineW3D: L3DEX 1.x's 0xB5,
  L3DEX2's 0x08), which it took as drawing nothing. A line is a quad along it on the screen, 1.5 pixels wide and half a
  pixel wider for each step of its width. RT64 transforms vertices on the GPU, so the quad's corners are copies of the
  line's two ends that RSPModifyCS, which already moves G_MW_POINTS's vertices on the screen, moves off the line by half
  its width after the transform: the lines move with the camera, frame interpolation and widescreen as triangles do.
  Turok 1 draws its map (L) with L3DEX 1.00; Turok 2, 3 and Rage Wars carry L3DEX2.

A patch is made on the commit the game's submodule pins, which is the upstream commit N64Recomp-browser's patches are
on. When N64Recomp-browser moves, check each still applies (`git apply --check` in that folder after its own patches).
