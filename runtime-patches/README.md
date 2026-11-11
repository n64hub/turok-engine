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

A patch is made on the commit the game's submodule pins, which is the upstream commit N64Recomp-browser's patches are
on. When N64Recomp-browser moves, check each still applies (`git apply --check` in that folder after its own patches).
