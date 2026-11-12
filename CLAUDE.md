# CLAUDE.md

The code the static recompilations of the N64 Turok games share, written once: the TLB layout their code runs in,
the symbol tooling that finds their functions with no decompilation, and the runtime hooks and library patches every one
needs. Almost all of it is written by AI agents, so this file is what the next one needs. README.md says what is here
and how a game uses it; docs/rom_layout.md is the games' memory and ROM layout.

## What lives where

- `tools/gen_syms.py`: writes a game's `syms/` from its ROM and its `syms/layout.toml` (the header says both).
- `tools/submodules.sh`: fills a game checkout's submodules from a cache of mirrors the machine's checkouts share.
  Building a game to try a change (README.md, "Changing it") starts with it, never with `git submodule update --init
  --recursive`, which downloads them all again. Never copy another checkout's submodules either: nested ones name
  their git directories by absolute path, so the copy's git writes into the other checkout.
- `src/`: C++ a game compiles into its program, declared in `src/turok_engine.h`.
- `runtime-patches/`: changes to the submodules every Turok game applies, after N64Recomp-browser's patches and before
  the game's own (`runtime-patches/README.md`).
- `docs/`: how the games are laid out and what the engine does about it.

## Rules

- No ROMs, ROM-derived assets or extracted game data, here or in a game's repo: a game commits only `syms/`'s names,
  addresses and sizes and its layout file. The engine builds only as part of a game.
- Something goes here when more than one game needs it, or when it is about the engine family rather than one game
  (the TLB, the FPU mode, the Acclaim microcodes). What one game alone needs stays in that game.
- What is wrong in RT64, N64ModernRuntime or RecompFrontend for every recomp, and not for something the Turok games do,
  is N64Recomp-browser's (its patches carry the fixes the recomps share), not this repo's or a game's: work and
  reports on it go there. `runtime-patches/` is only for what the Turok games need of them.
- Each game holds this repo as `lib/turok-engine`, pinned at a commit. A change here is a PR to this repo's `main`, and
  each game moves onto it with a PR of its own that moves the pin.
- A change here that breaks a game's build is fixed in the same work: this repo's CI builds every game that holds it
  at the change's commit.
- Write for a stranger, in the present tense: nothing about a contributor's machine, paths or accounts, and nothing
  about where anyone keeps ROMs.
- Code taken from PaxDino (https://github.com/wootbeer/PaxDino, GPL-3.0) says so in its file's header.
- GPL-3.0, as the recompilations it serves.

## If the change reaches another repo, YOU make it

This repo's work is all of the Turok recompilations' shared work, so a change it needs in one of the games that hold
it (`turok-2-recomp`, `turok-3-recomp`, `turok-recomp`) is made in the same work, not handed on as work of its own: a PR
there, from a checkout of its `main`, landed by whoever does the work like one here. The game's PR lands first when
this repo's CI builds that game's `main`; a game's PR that moves the pin lands after this repo's.
