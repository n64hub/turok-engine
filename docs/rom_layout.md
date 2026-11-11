# The ROM and memory layout

Acclaim's Turok 2 engine family (Turok 2, Rage Wars, Turok 3) doesn't run its code from kseg0 as most N64 games do.
This is where the code is, where it runs, and what the engine does about each part. A game's `syms/layout.toml`
holds its own numbers; Turok 2 USA 1.0's are the examples here.

## The first MB: code at 0x00200400

The console's boot code (IPL3) copies the first MB of the ROM after the header to 0x80000400 and jumps to the header's
entry point there, as for any game. The entry point then sets TLB entry 31 so that 0x00200000-0x003FFFFF maps onto
physical 0-2 MB, and jumps into the same code at its TLB address: the code is linked at 0x00200400, so every jal,
every function pointer and every string the code builds with lui and addiu is a 0x002xxxxx address. Rodata and data
that follow the text in the same MB are reached through kseg0 (0x800Axxxx and up), and their jump tables hold kseg0
addresses of the case labels.

So a game's layout file has a segment for the first MB at 0x00200400 (`[[segment]]`, its text and its data), and an
alias of the same bytes at 0x80000400 (`[[alias]]`), where N64Recomp reads the jump tables from.

- The GameEntry's `entrypoint_address` is the kseg0 address, `turok::boot_load_address` (0x80000400): the runtime
  loads the first MB there, as IPL3 would, before anything runs.
- The recompiled code turns a guest address into `rdram + (address - 0xFFFFFFFF80000000)`, so a TLB address such as
  0x00200400 lands 0x80200400 bytes into the runtime's 4 GB reservation, past what is ever made accessible.
  `turok::map_tlb_sections` makes the pages of every section below kseg0 readable and writable, copies the section's
  bytes there from the ROM, and registers its functions at their TLB addresses for calls through pointers. The pages
  are copies, not aliases of physical memory; the games write neither their code nor data through the TLB view.

Turok 3's program is longer than that MB: its boot code copies the next 512 KB of the ROM to 0x80100400 itself,
reading the cartridge bus word by word, and the code and data run to 0x80114B60 (0x00314B60 through the TLB). The
runtime can't read the bus, so the game makes that copy before it runs and jumps over the loop; its `main` segment is
the whole program.

## More code: 0x00400000

Turok 2 keeps a second block of code in the ROM (ROM 0x14A000 on) that its VM setup maps with TLB entry 0 at
0x00400000: the game DMAs it from the ROM into the top of an Expansion Pak's memory and maps it there (Turok 3's is
the ROM straight after its program, mapped with 1 MB pages). It is another segment in the layout file. Its jump
tables are in the first MB's rodata, so the alias covers them too. Other games of the family have more blocks like
it; each is a segment.

## ROM regions the VM server maps

The game's VM server maps regions of the ROM (levels, models, sounds) into TLB space on demand, and its pager fills a
page from the ROM on each TLB miss. The engine has no TLB misses: the game's VmMapRomRegion is replaced by
`turok_vm_map_rom_region`, which hands out the next free range from the base the game sets (`turok::set_vm_base`:
0x00480000 in Turok 2, 0x00800000 in Turok 3), makes it accessible and copies the region in at once. A region holds
pointers into itself, linked for where the game's server puts it, so the base is where the game's first region's
pointers point. Nothing is ever unmapped (`osUnmapTLB` does nothing).

## The FPU

IPL3 leaves the FPU in FR mode (32 64-bit registers), and the games' code relies on it. `turok::set_fpu_mode` sets the
recompiled context's mode for the entry thread, and `turok::on_thread_create` for every thread after.

## Finding the functions

There is no decompilation and the ROM holds no symbols, so `tools/gen_syms.py` finds every function in the code. Three
things in these games make that hard, and its header has the rules for each:

- The toolchain left fragments of strings and older code in the text, between functions. A fragment can hold a word
  that reads as a jal; its target is no function start, and one in Turok 2 pointed at another function's epilogue,
  splitting it so that the call returned with $sp 0x18 short. A start that only such jals reach is dropped.
- Hand-written code starts in the delay slot of a jr, or after a doubled stack-frame setup.
- Switches jump through tables whose case labels look like function starts.

A split function shows as one whose stack adjustments don't balance (an `addiu $sp, $sp, -N` with no `+N`); thread
entries that never return are the only ones that should.

## The web

A web build runs the same code in WebAssembly, where rdram is a linear memory, not a 4 GB reservation with pages to
open. N64Recomp-browser's runtime (v0.183.0 and later) has a KUSEG window for it: a game that defines
`RECOMP_KUSEG_WINDOW` for every target, before adding the runtime, gets kuseg's first 16 MB in a window after RDRAM on
the web, and `recomp::make_kuseg_accessible(rdram, vaddr, size)` opens a range on every platform (the window on the
web, the reserved pages committed natively). With the define, `turok::make_accessible` calls it; without it (a game on
an older runtime) it opens the pages with VirtualProtect or mprotect, as that runtime commits the whole 4 GB. The TLB
sections and the VM server's regions must stay below 0x01000000 for the web.
