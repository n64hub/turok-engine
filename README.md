# Turok engine

> **Made with AI.** Almost all of the code, documentation and tests in this repo were written by AI agents
> (Claude), directed and play-tested by a person. None of it is offered to the upstream projects it builds on, which
> don't take AI-written code.

The code the static recompilations of the N64 Turok games share, written once. Acclaim's Turok 2 engine family
(Turok 2: Seeds of Evil, Turok: Rage Wars, Turok 3: Shadow of Oblivion) runs its code through the TLB rather than from
kseg0, has no decompilation, and drives the hardware in ways the runtime doesn't expect, so a recompilation of any of
them needs the same things done. What is here is written against a short list of each game's own names and addresses.
Each game has this repo as a submodule, `lib/turok-engine`, pinned at a commit:
[Turok 2: Recompiled](https://github.com/n64hub/turok-2-recomp), [Turok 3: Recompiled](https://github.com/n64hub/turok-3-recomp)
and [Turok: Recompiled](https://github.com/n64hub/turok-recomp) so far. Turok: Dinosaur Hunter came before the family's
TLB-mapped code and runs all of its code in kseg0, so it uses the finder, the Controller Pak, the FPU mode,
osClockRate and widescreen, and none of the TLB views or the VM server.

It holds no ROM and nothing made from one, and builds only as part of a game. Much of it began in
[PaxDino](https://github.com/wootbeer/PaxDino) (GPL-3.0), Turok: Rage Wars' recompilation; each file taken from it
says so.

## What is here

- `docs/rom_layout.md`: where the games' code is in the ROM, where it runs (the TLB mappings), and what that means for
  the symbols, the runtime and the web.
- `tools/gen_syms.py`: the function finder. It writes a game's `syms/` (the symbols N64Recomp reads) from the ROM
  and the game's `syms/layout.toml`, with no decompilation: spimdisasm's functions, jal targets, code pointers made
  in code and in data, stack-frame setups, the gaps between, with jump tables' case labels and the data fragments the
  toolchain left in the text kept out, and libultra named by n64sym's signatures. Its header says how.
- `src/tlb_views.cpp`: the TLB-mapped code where the recompiled code reads and calls it. It opens the pages of every
  section below kseg0, fills them from the ROM and registers their functions there, and maps the ROM regions the
  game's own VM server hands out (`turok_vm_map_rom_region`). Also the FPU's FR mode the boot code leaves set, for
  every thread.
- `src/pi_dma.cpp`: the game's own DMA thread, which calls osPiRawStartDma itself and waits on OS_EVENT_PI
  (`turok_pi_raw_start_dma`, `turok_set_pi_event`), and osClockRate (`turok_set_clock_rate`).
- `src/audio.cpp`: the audio thread's pace, osAiGetStatus reporting a full FIFO once about two retraces of audio are
  queued (`turok_ai_get_status`). With RSPRecomp's audio microcode, which a game recompiles from its own ROM, that is
  the audio setup.
- `src/controller_pak.cpp`: a Controller Pak saved to a file per port, and each port's accessory (a Controller Pak or
  a Rumble Pak), which the games probe with osMotorInit first (`turok_motor_init`).
- `src/libultra.cpp`: the libultra functions the games link that N64Recomp leaves out and the runtime lacks (the
  thread queues, the pak's raw reads, osUnmapTLB).
- `src/widescreen.cpp`: what RT64's widescreen (Expand) needs from the game's display list: the HUD anchored to the
  window's sides, the border at the old 4:3 edges taken out and the camera's culling widened (`docs/widescreen.md`).
- `src/turok_engine.h`: all of the above, as a game calls it.
- `runtime-patches/`: changes to N64Recomp and RT64 every Turok game needs (`runtime-patches/README.md`).

Anything else the games share comes here when a change needs it in more than one of them.

## How a game uses it

- `CMakeLists.txt` compiles `lib/turok-engine/src/*.cpp` into the game, with `lib/turok-engine/src` on its include
  path, and its patch step applies `lib/turok-engine/runtime-patches/` after N64Recomp-browser's patches and before
  the game's own.
- `syms/layout.toml` says where the game's code segments are in the ROM and where they run (`tools/gen_syms.py`'s
  header), and `syms/names.toml` names the functions the game knows, among them the natives below. Running
  `python3 lib/turok-engine/tools/gen_syms.py` from the game's root writes `syms/<game>.syms.toml`.
- The game's N64Recomp configuration lists each native it names in `ignored`, so the engine's takes its place:

  - `turok_vm_map_rom_region`: the game's VmMapRomRegion.
  - `turok_pi_raw_start_dma`: its osPiRawStartDma.
  - `turok_ai_get_status`: its osAiGetStatus.
  - `turok_motor_init`: its osMotorInit.

  It hooks its DMA thread's osSetEventMesg(OS_EVENT_PI, ...) with `turok_set_pi_event`, and the call to osInitialize
  with `turok_set_clock_rate`, at the game's own addresses.
- The game's GameEntry has `.entrypoint_address = turok::boot_load_address` and
  `.thread_create_callback = turok::on_thread_create`. Its on_init_callback calls `turok::set_fpu_mode`, then
  `turok::map_tlb_sections` with N64Recomp's section table and `turok::set_vm_base` with the first address its VM
  server hands out. Its audio callbacks call `turok::on_audio_frequency` and `turok::on_audio_queued`. A game with
  another accessory than a Controller Pak in port 1 calls `turok::set_accessories`, and one that supports widescreen
  calls `turok::set_display_list_head` and hooks its frame, its HUD and its camera (`docs/widescreen.md`).

## Changing it

A change is a pull request to `main`: the games pin its commits. A change reaches a game when the game's
`lib/turok-engine` moves onto it, by a pull request there.

This repo's CI builds each game with `lib/turok-engine` at the commit under test, by calling the game's own CI on the
game's `main` (Windows and Linux, everything but the code generated from the ROM, no link). A change that needs a
game's change too stays red here until the game's change is on its `main`.

## License

GPL-3.0, as the recompilations it serves.
