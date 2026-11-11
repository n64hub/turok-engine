"""Writes a Turok game's N64Recomp symbols (its syms TOML) from the ROM alone, from the layout file the game keeps.

No decompilation of these games exists and their ROMs carry no symbols, so every function is found in the code. The
engine's games run their code through the TLB (docs/rom_layout.md): it is linked at 0x00200400 and up, not in kseg0,
and some of it sits between string fragments the toolchain left in the text. The finder is PaxDino's
(https://github.com/wootbeer/PaxDino, tools/find_functions.py, GPL-3.0), made to read a game's layout instead of
holding Rage Wars' own. It puts these together into one list of function starts:

- spimdisasm's functions in each segment's text;
- every jal target, and each code address built with lui and addiu (or ori);
- each word in a segment's data that points at code, where the code before the target ends a function (a jr $ra, a
  j or a b, and its delay slot), so the case labels of switches stay out;
- a stack-frame setup (addiu $sp, $sp, -N) straight after a function's end;
- the targets of every jump table the code indexes and jr's through are case labels, never starts (jump_table_targets);
- in the gaps nothing above reaches, code that reads as a whole function (gap search), and each word of data there
  that points at code as the data's do;
- in a segment the layout calls packed, a few words of code between two functions: a leaf of its own, or the next
  one's first instructions, scheduled ahead of its stack-frame setup;
- the starts in the game's names file, taken as they are, and none at its labels: addresses inside a function that
  the scans would take for a start, as one in hand-written code that a j or a branch reaches from elsewhere in the
  same function, or a stack adjustment the compiler scheduled after a function's first instructions.

A start that does not read as code (an invalid instruction, a write to $zero, $k0/$k1, a branch out of every segment)
is data before a real function: the first later word that does is where that one starts.

libultra and libc are named by n64sym's signatures (https://github.com/shygoo/n64sym, MIT, cloned to ref/n64sym in the
game's repo): a function matches when the CRC32 of its first 8 bytes and of its whole body agree once the relocated
fields are masked. N64Recomp has the runtime replace libultra's functions by name, so the ones it reimplements must
carry their real names. The game's names file names the rest it knows, and keeps them for good (mods and hooks use
them). Everything else is func_<address>.

The layout file (syms/layout.toml in the game's repo):

    rom = "roms/turok2.us.z64"          # paths from the game's root
    sha1 = "..."                        # the .z64's
    out = "syms/turok2.us.syms.toml"
    names = "syms/names.toml"           # optional: [[func]] vram = 0x..., name = "..."; [[label]] vram = 0x...

    [[segment]]                         # code: its text, then its data, linked at vram
    name = "main"
    rom = 0x1000
    vram = 0x00200400
    text_end = 0x...                    # ROM offset where the text ends
    data_end = 0x...                    # ROM offset where the segment's data in ROM ends
    library = true                      # match libultra's signatures here (optional)
    packed = true                       # nothing but code and data between its functions, so a few words of code
                                        # between two take a start (optional; see Finder.find)

    [[alias]]                           # the same bytes at another address, with no functions: N64Recomp reads
    name = "main_kseg0"                 # the jump tables the code reaches through kseg0 from it
    rom = 0x1000
    vram = 0x80000400
    size = 0x100000

Usage, from the game's root: python3 lib/turok-engine/tools/gen_syms.py [syms/layout.toml] [--report]
(needs the ROM, rabbitizer and spimdisasm: pip install spimdisasm; n64sym's signatures in ref/n64sym are optional).
"""
import argparse
import bisect
import csv
import hashlib
import struct
import subprocess
import sys
import tempfile
import tomllib
import zlib
from pathlib import Path

import rabbitizer

JR_RA = 0x03E00008
SIGNATURES = Path("ref/n64sym/src/builtin_signatures.sig")
# libc names the host's C library declares too and N64Recomp doesn't rename (its renamed_funcs): the generated code's
# declaration of the game's function would conflict with the host's.
HOST_LIBC = {"qsort", "bsearch", "abs", "labs", "atoi", "atol", "strncpy", "strncmp", "strrchr", "strstr", "vsprintf",
             "printf", "rand", "srand"}


def word(buf, off):
    return struct.unpack_from(">I", buf, off)[0]


class Segment:
    def __init__(self, d):
        self.name = d["name"]
        self.rom = d["rom"]
        self.vram = d["vram"]
        self.text_end = d["text_end"]
        self.data_end = d["data_end"]
        self.library = d.get("library", False)
        self.packed = d.get("packed", False)

    @property
    def text_size(self):
        return self.text_end - self.rom

    @property
    def end(self):
        """The vram past the text."""
        return self.vram + self.text_size

    def rom_of(self, vram):
        return self.rom + (vram - self.vram)


class Layout:
    def __init__(self, path, root):
        d = tomllib.loads(path.read_text())
        self.root = root
        self.rom_path = root / d["rom"]
        self.sha1 = d["sha1"]
        self.out = root / d["out"]
        self.names_path = root / d["names"] if "names" in d else None
        self.segments = [Segment(s) for s in d["segment"]]
        self.aliases = d.get("alias", [])
        self.rom = self.rom_path.read_bytes()
        if hashlib.sha1(self.rom).hexdigest() != self.sha1:
            sys.exit(f"{self.rom_path}: not the ROM {path} is for (SHA-1 {self.sha1})")

    def seg_of(self, vram):
        """The segment whose text holds vram, or None."""
        for s in self.segments:
            if s.vram <= vram < s.end:
                return s
        return None

    def at(self, vram):
        """The word at a text address."""
        return word(self.rom, self.seg_of(vram).rom_of(vram))

    def rom_of_data(self, vram):
        """The ROM offset of an address in any segment or alias, text or data, or None."""
        for s in self.segments:
            if s.vram <= vram < s.vram + (s.data_end - s.rom):
                return s.rom_of(vram)
        for a in self.aliases:
            if a["vram"] <= vram < a["vram"] + a["size"]:
                return a["rom"] + (vram - a["vram"])
        return None

    def names(self):
        if self.names_path is None or not self.names_path.exists():
            return {}
        return {f["vram"]: f["name"] for f in tomllib.loads(self.names_path.read_text()).get("func", [])}

    def labels(self):
        if self.names_path is None or not self.names_path.exists():
            return set()
        return {f["vram"] for f in tomllib.loads(self.names_path.read_text()).get("label", [])}


def branch_target(w, v):
    """The target of a conditional or unconditional branch at v, or None."""
    op = w >> 26
    off = ((w & 0xFFFF) ^ 0x8000) - 0x8000
    if op in (0x04, 0x05, 0x06, 0x07, 0x14, 0x15, 0x16, 0x17):  # beq bne blez bgtz (and likely)
        return v + 4 + (off << 2)
    if op == 0x01 and ((w >> 16) & 0x1F) in (0, 1, 2, 3, 0x10, 0x11, 0x12, 0x13):  # regimm
        return v + 4 + (off << 2)
    if op == 0x11 and ((w >> 21) & 0x1F) == 0x08:  # bc1f/bc1t (and likely)
        return v + 4 + (off << 2)
    return None


def jump_target(w, v):
    return ((v + 4) & 0xF0000000) | ((w & 0x03FFFFFF) << 2)


def is_frame_setup(w):
    return (w >> 16) == 0x27BD and bool(w & 0x8000)  # addiu $sp, $sp, -N


class Finder:
    def __init__(self, lay):
        self.lay = lay
        self.rom = lay.rom

    def ends_function(self, vram, after_data=False):
        """The two instructions before vram end a function (or, with after_data, data sits right before it)."""
        s = self.lay.seg_of(vram)
        if s is None:
            return False
        off = s.rom_of(vram)
        while off - 8 >= s.rom and word(self.rom, off - 4) == 0 and word(self.rom, off - 8) == 0:
            off -= 4  # nop padding
        if off - 8 < s.rom:
            return off == s.rom
        w = word(self.rom, off - 8)
        if w == JR_RA or (w >> 26) == 2 or (w >> 16) == 0x1000:  # jr $ra, j, b
            return True
        if not after_data:
            return False
        # Data padded with zero words up to the start (Turok 3's second block has data between its functions).
        if off < s.rom_of(vram):
            return True
        # Hand-written assembly (noreorder) starts a function in the delay slot of the one before's jr $ra.
        if word(self.rom, off - 4) == JR_RA:
            return True
        if not rabbitizer.Instruction(word(self.rom, off - 4)).isValid():
            return True
        return sum(0x20 <= b < 0x7F for b in self.rom[off - 8:off]) >= 5  # string fragments

    def jump_table_targets(self):
        """The entries of every jump table the code indexes and then jr's through: {case labels}, {jr: [entries]}.

        A linear constant propagation over each text: lui/addiu/ori build constants, an addu of a constant and an
        unknown is "constant + index", a lw from that is "the entry of the table at constant + offset", and a jr of such
        an entry names the table. The table's base often sits in a saved register set long before the jr, so values
        live until the register is written or the function returns."""
        tables = []
        for s in self.lay.segments:
            reg = {}
            for off in range(s.rom, s.text_end, 4):
                w = word(self.rom, off)
                op = w >> 26
                r_s, r_t, r_d = (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
                imm = w & 0xFFFF
                simm = imm - 0x10000 if imm & 0x8000 else imm
                if w == JR_RA:
                    reg = {}
                    continue
                if op == 0 and (w & 0x3F) == 0x08:  # jr rX
                    v = reg.get(r_s)
                    if isinstance(v, tuple) and v[0] == "entry":
                        tables.append((s.vram + off - s.rom, v[1]))
                    continue
                if op == 0x0F:  # lui
                    reg[r_t] = (imm << 16) & 0xFFFFFFFF
                elif op == 0x09:  # addiu
                    v = reg.get(r_s)
                    if isinstance(v, int):
                        reg[r_t] = (v + simm) & 0xFFFFFFFF
                    elif isinstance(v, tuple) and v[0] == "index":
                        reg[r_t] = ("index", (v[1] + simm) & 0xFFFFFFFF)
                    else:
                        reg.pop(r_t, None)
                elif op == 0x0D:  # ori
                    v = reg.get(r_s)
                    if isinstance(v, int):
                        reg[r_t] = v | imm
                    else:
                        reg.pop(r_t, None)
                elif op == 0x23:  # lw
                    v = reg.get(r_s)
                    if isinstance(v, int):
                        reg[r_t] = ("entry", (v + simm) & 0xFFFFFFFF)
                    elif isinstance(v, tuple) and v[0] == "index":
                        reg[r_t] = ("entry", (v[1] + simm) & 0xFFFFFFFF)
                    else:
                        reg.pop(r_t, None)
                elif op == 0 and (w & 0x3F) in (0x21, 0x20):  # addu / add
                    a, b = reg.get(r_s), reg.get(r_t)
                    if isinstance(a, int) and b is None and r_t != 0:
                        reg[r_d] = ("index", a)
                    elif isinstance(b, int) and a is None and r_s != 0:
                        reg[r_d] = ("index", b)
                    elif r_t == 0 and a is not None:
                        reg[r_d] = a  # move
                    elif r_s == 0 and b is not None:
                        reg[r_d] = b
                    else:
                        reg.pop(r_d, None)
                elif op == 0:
                    reg.pop(r_d, None)
                elif op in (0x20, 0x21, 0x24, 0x25, 0x0A, 0x0B, 0x0C, 0x0E, 0x08):
                    reg.pop(r_t, None)
        targets, by_jr = set(), {}
        for jr, base in tables:
            t = self.lay.rom_of_data(base)
            if t is None:
                continue
            n = 0
            while n < 512 and t + 4 * n + 4 <= len(self.rom):
                e = word(self.rom, t + 4 * n)
                if self.lay.seg_of(e) is None:
                    break
                targets.add(e)
                by_jr.setdefault(jr, []).append(e)
                n += 1
        return targets, by_jr

    def has_exit(self, start, end):
        """The last instruction pair of [start, end) is an exit."""
        if end - start < 8:
            return False
        w = self.lay.at(end - 8)
        return w == JR_RA or (w >> 26) == 2 or (w >> 16) == 0x1000

    def function_end(self, start, limit, jtables):
        """Past the delay slot of the first exit (jr $ra, a tail-call j, an unconditional b) that no branch, jump or
        jump-table entry seen so far reaches beyond."""
        furthest = start
        for v in range(start, limit, 4):
            w = self.lay.at(v)
            op = w >> 26
            t = branch_target(w, v)
            if t is not None and start <= t < limit:
                furthest = max(furthest, t)
            if op == 2:
                t = jump_target(w, v)
                if start <= t < limit:
                    furthest = max(furthest, t)
                    if v + 8 > furthest:
                        return v + 8
                    continue
                return v + 8 if v + 8 > furthest else limit  # tail call
            if op == 0 and (w & 0x3F) == 0x08:
                if (w >> 21) & 31 != 31:
                    for e in jtables.get(v, ()):
                        if start <= e < limit:
                            furthest = max(furthest, e)
                    continue
                if v + 8 > furthest:
                    return v + 8
            if (w >> 16) == 0x1000 and v + 8 > furthest:  # b
                return v + 8
        return limit

    def looks_like_code(self, start, end):
        """Rejects a "function" that is really data left in the text (string fragments between functions)."""
        for v in range(start, end, 4):
            w = self.lay.at(v)
            ins = rabbitizer.Instruction(w, vram=v)
            if not ins.isValid():
                return False
            # compilers never write $zero but in the canonical nop
            if not ins.isNop() and ((ins.modifiesRd() and ins.rd.value == 0)
                                    or (ins.modifiesRt() and ins.rt.value == 0)):
                return False
            if (w >> 26) in (2, 3) and self.lay.seg_of(jump_target(w, v)) is None:
                return False
            t = branch_target(w, v)
            if t is not None and self.lay.seg_of(t) is None:
                return False
            # $k0/$k1 are the exception handlers', which a game names in its names file; anywhere else they are data
            op = w >> 26
            if op in (0x10, 0x11, 0x12):  # coprocessor ops: the fields aren't GPRs
                gprs = ()
            elif op in (0x31, 0x35, 0x39, 0x3D):  # lwc1/ldc1/swc1/sdc1: rt is an FPR
                gprs = ((w >> 21) & 31,)
            elif op == 0:
                gprs = ((w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31)
            else:
                gprs = ((w >> 21) & 31, (w >> 16) & 31)
            if any(r in (26, 27) for r in gprs):
                return False
        return True

    def is_stray_branch(self, start, end):
        """A few data words between one function's exit and the next function that read as a branch: no exit of their
        own, and a branch that leaves them or one with its delay slot past their end. N64Recomp can't recompile
        either."""
        if end - start > 0x10 or self.has_exit(start, end):
            return False
        for v in range(start, end, 4):
            w = self.lay.at(v)
            t = branch_target(w, v)
            if t is not None and not start <= t < end:
                return True
            if v + 4 == end and (t is not None or (w >> 26) in (2, 3) or (w >> 26 == 0 and (w & 0x3E) == 0x08)):
                return True  # a branch, j, jal, jr or jalr as the last word
        return False

    def skip_leading_junk(self, start, limit):
        """A stray data word right before a stack-frame setup: start after it."""
        w0 = self.lay.at(start)
        if start + 4 < limit and is_frame_setup(w0) and is_frame_setup(self.lay.at(start + 4)):
            return start + 4  # no function sets up its frame twice: the first is data that reads as one
        if start + 4 < limit and not is_frame_setup(w0) and is_frame_setup(self.lay.at(start + 4)):
            t = branch_target(w0, start)
            if (t is not None and not (start < t < limit)) or (w0 >> 26) in (2, 3):
                return start + 4
        return start

    def find(self, spim_starts, forced):
        """[(start, end, name or None, source)] for every function, sorted. forced: {vram: name} taken as they are."""
        lay = self.lay
        labels = lay.labels()
        starts = {}  # vram -> (name or None, source)
        data_targets = set()
        jal_sites = {}  # vram -> the jals' own addresses
        jt_targets, jt_tables = self.jump_table_targets()
        print(f"{len(jt_tables)} jump tables, {len(jt_targets)} case labels kept out")

        def add(v, src, name=None, force=False):
            if lay.seg_of(v) is None or v & 3 or v in labels:
                return
            if v in starts:
                if name and not starts[v][0]:
                    starts[v] = (name, starts[v][1] + "," + src)
                return
            if not force and src != "jal" and v in jt_targets:
                return
            # Data fragments sit in front of some functions, so a start after data is taken on stronger evidence: a
            # pointer made in code, or a target that opens with a stack-frame setup.
            if not force and src in ("ptr", "data") and not self.ends_function(
                    v, after_data=(src == "ptr" or is_frame_setup(lay.at(v)))):
                return
            starts[v] = (name, src)

        for s in lay.segments:
            add(s.vram, "segstart")
        for v in spim_starts:
            add(v, "spim")
        for v, name in forced.items():
            add(v, "extra", name, force=True)

        for s in lay.segments:
            lui = {}
            for off in range(s.rom, s.text_end, 4):
                w = word(self.rom, off)
                op = w >> 26
                if op == 3:  # jal
                    t = jump_target(w, s.vram + off - s.rom)
                    jal_sites.setdefault(t, []).append(s.vram + off - s.rom)
                    add(t, "jal")
                elif op == 0x0F:  # lui
                    lui[(w >> 16) & 31] = (w & 0xFFFF) << 16
                elif op in (0x09, 0x0D):  # addiu / ori
                    r = (w >> 21) & 31
                    if r in lui:
                        imm = w & 0xFFFF
                        if op == 0x09 and imm & 0x8000:
                            imm -= 0x10000
                        add((lui[r] + imm) & 0xFFFFFFFF, "ptr")
                if w == JR_RA:
                    lui = {}  # forget them across a function's end
            for off in range(s.text_end, s.data_end - 3, 4):  # data words pointing at code
                add(word(self.rom, off), "data")
                data_targets.add(word(self.rom, off))
            for off in range(s.rom + 8, s.text_end, 4):  # a frame setup right after a function's end
                v = s.vram + off - s.rom
                if is_frame_setup(word(self.rom, off)) and self.ends_function(v, after_data=True):
                    add(v, "frame")

        # A start in the delay slot of a jr $ra is hand-written assembly's entry (Turok 3's RNC unpacker shares its
        # first instruction with the end of the wrapper that calls it), so the word after it is that function's
        # second instruction, not the next function, though it follows a function's end as one would.
        for v in [v for v in starts if lay.seg_of(v - 4) is lay.seg_of(v) and lay.at(v - 4) == JR_RA]:
            nxt = starts.get(v + 4)
            if nxt and not set(nxt[1].split(",")) & {"extra", "jal"}:
                del starts[v + 4]

        def resolve():
            out, dropped = [], 0
            addrs = sorted(starts)
            for i, a in enumerate(addrs):
                s = lay.seg_of(a)
                nxt = addrs[i + 1] if i + 1 < len(addrs) and addrs[i + 1] < s.end else s.end
                name, src = starts[a]
                if "extra" not in src:
                    a = self.skip_leading_junk(a, nxt)
                    if a >= nxt:
                        continue  # only the junk word; the function is listed at nxt
                end = self.function_end(a, nxt, jt_tables)
                if "extra" not in src and src.split(",")[0] != "segstart" and not self.looks_like_code(a, end):
                    # Data in front of a real function: the first later word that reads as a whole function is where
                    # it starts.
                    window = range(a + 4, min(a + 0x40, nxt), 4)
                    framed = [p for p in window if is_frame_setup(lay.at(p))]
                    for p in framed + [p for p in window if p not in framed]:
                        pend = self.function_end(p, nxt, jt_tables)
                        if (p in framed or self.has_exit(p, pend)) and self.looks_like_code(p, pend):
                            a, end, name = p, pend, None
                            break
                    else:
                        dropped += 1
                        continue
                if "extra" not in src and self.is_stray_branch(a, end):
                    dropped += 1
                    continue
                out.append((a, end, nxt, name, src))
            return out, dropped

        funcs, dropped = resolve()
        # A jal word in a data fragment can point into the middle of a function, as one before 0x00428CF0 does into
        # an epilogue: a start that only jals found is kept if one of them sits in a function.
        while True:
            spans = sorted((a, end) for a, end, *_ in funcs)
            heads = [a for a, _ in spans]

            def in_code(v):
                i = bisect.bisect_right(heads, v) - 1
                return i >= 0 and v < spans[i][1]

            stray = [v for v, (name, src) in starts.items()
                     if src == "jal" and not any(in_code(j) for j in jal_sites.get(v, ()))]
            if not stray:
                break
            for v in stray:
                del starts[v]
            print(f"{len(stray)} starts dropped that only jals in data reach")
            funcs, dropped = resolve()
        # Gaps: code after a function's end that nothing above found, usually a function reached only through a
        # pointer the scans missed, behind a data fragment. In a gap that reads as a whole function (valid code ending
        # in an exit), a start is a stack-frame setup, or the target of a data word right after data. In a gap,
        # neither can split a function. A gap runs to the next function kept, not the next start: a start dropped as
        # data would otherwise end it (one at 0x0041000C hid 20 KB of Turok 3's second block, a function in it too).
        found = 0
        kept = sorted(a for a, *_ in funcs)
        for a, end, _, name, src in funcs:
            i = bisect.bisect_right(kept, a)
            seg_end = lay.seg_of(a).end
            nxt = min(kept[i], seg_end) if i < len(kept) else seg_end
            p = end
            for q in range(end, nxt, 4):  # data between functions, as in Turok 3's second block, points at code too
                v = lay.at(q)
                if v not in starts:
                    add(v, "data")
                    if v in starts:
                        data_targets.add(v)
                        found += 1
            while p < nxt:
                if p not in jt_targets and (is_frame_setup(lay.at(p)) or (
                        p in data_targets and self.ends_function(p, after_data=True))):
                    pend = self.function_end(p, nxt, jt_tables)
                    if self.has_exit(p, pend) and self.looks_like_code(p, pend):
                        starts[p] = (None, "gap")
                        found += 1
                        p = pend
                        continue
                p += 4
        if found:
            funcs, dropped = resolve()
        # Code between a function's end and the next start, a few words of it, that every scan above passed over. GCC
        # schedules a function's first loads ahead of its stack-frame setup, so a function found by its frame setup can
        # start a few words before it; and a leaf reached only through a pointer made at run time reads as a whole
        # function in front of the next. Only in a segment the layout calls packed: Turok 2's first MB has stale code
        # between its library functions, which reads the same. Words that are all valid code, with no padding and no
        # use of $sp.
        between = 0
        for (a, end, *_), (b, _, _, _, bsrc) in zip(funcs, funcs[1:]):
            seg = lay.seg_of(end)
            if not end < b <= end + 0x20 or seg is not lay.seg_of(b) or not seg.packed or end in starts:
                continue
            ws = [lay.at(x) for x in range(end, b, 4)]
            ins = [rabbitizer.Instruction(w, vram=x) for w, x in zip(ws, range(end, b, 4))]
            if any(w == 0 or not i.isValid() or 29 in ((w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31)
                   for w, i in zip(ws, ins)):
                continue
            flow = [i.isBranch() or i.isJump() for i in ins]
            if len(ws) >= 2 and ws[-2] == JR_RA and not any(flow[:-2]):
                starts[end] = (None, "between")  # a leaf of its own
            elif not any(flow) and not set(bsrc.split(",")) & {"extra", "jal", "ptr", "data"}:
                starts[end] = starts.pop(b)  # the next function's first instructions
            else:
                continue
            between += 1
        if between:
            funcs, dropped = resolve()
        print(f"{len(funcs)} functions ({found} found in gaps, {between} between functions, {dropped} data fragments "
              "dropped)")
        # A function ends at its last exit: what lies between that and the next start is the string fragments the
        # toolchain left in the text, which N64Recomp can't read as code.
        return [(a, end, name, src) for a, end, nxt, name, src in funcs]


def load_signatures(root):
    path = root / SIGNATURES
    if not path.exists():
        print(f"no {SIGNATURES}: libultra isn't named (git clone https://github.com/shygoo/n64sym ref/n64sym)")
        return []
    sigs, cur = [], None
    for line in path.read_text().splitlines():
        p = line.split()
        if not p or p[0].startswith("#"):
            continue
        if p[0].startswith("."):
            cur["relocs"] += [(int(o, 16), p[0], p[1]) for o in p[2:]]
        else:
            cur = {"name": p[0], "size": int(p[1], 16), "crc8": int(p[2], 16), "crc": int(p[3], 16), "relocs": []}
            sigs.append(cur)
    return [s for s in sigs if s["size"] >= 12]


def masked(buf, off, size, relocs=()):
    data = bytearray(buf[off:off + size])
    if len(data) < size:
        return None
    for o, kind, _ in relocs:
        w = struct.unpack_from(">I", data, o)[0]
        w &= 0xFFFF0000 if kind in (".hi16", ".lo16") else 0xFC000000
        struct.pack_into(">I", data, o, w)
    return bytes(data)


def match_library(lay, seg, starts, sigs):
    """{vram: name} for the functions whose code matches a signature (the largest match wins), and the jal targets
    their relocations name."""
    by_crc8 = {}
    for s in sigs:
        by_crc8.setdefault(s["crc8"], []).append(s)
    names = {}
    for addr in starts:
        off = seg.rom_of(addr)
        hits = []
        for s in by_crc8.get(zlib.crc32(masked(lay.rom, off, 8) or b""), []):
            d = masked(lay.rom, off, s["size"], s["relocs"])
            if d is not None and zlib.crc32(d[:8]) == s["crc8"] and zlib.crc32(d) == s["crc"]:
                hits.append(s)
        if not hits:
            continue
        size = max(s["size"] for s in hits)
        best = [s for s in hits if s["size"] == size]
        if len({s["name"] for s in best}) > 1:
            print(f"  ambiguous at {addr:08X}: {sorted({s['name'] for s in best})}")
            continue
        names[addr] = best[0]["name"]
        for o, kind, target in best[0]["relocs"]:
            if kind == ".targ26" and "_text_" not in target:
                names.setdefault(jump_target(word(lay.rom, off + o), addr + o), target)
    return names


def spimdisasm_starts(lay, seg, tmp):
    binpath = Path(tmp) / f"{seg.name}.bin"
    binpath.write_bytes(lay.rom[seg.rom:seg.text_end])
    info = Path(tmp) / f"{seg.name}.csv"
    subprocess.run([sys.executable, "-m", "spimdisasm", "singleFileDisasm", str(binpath), str(Path(tmp) / "asm"),
                    "--start", "0", "--end", hex(seg.text_size), "--vram", hex(seg.vram), "--function-info",
                    str(info)], check=True, capture_output=True)
    with info.open() as f:
        return [int(r["address"], 16) for r in csv.DictReader(f)]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("layout", nargs="?", default="syms/layout.toml")
    ap.add_argument("--report", action="store_true", help="list where each function came from")
    args = ap.parse_args()
    root = Path.cwd()
    lay = Layout(root / args.layout, root)
    finder = Finder(lay)
    hand = lay.names()

    spim = []
    with tempfile.TemporaryDirectory() as tmp:
        for seg in lay.segments:
            found = spimdisasm_starts(lay, seg, tmp)
            print(f"{seg.name}: {len(found)} functions from spimdisasm")
            spim += found
    funcs = finder.find(spim, hand)

    sigs = load_signatures(root)
    library = {}
    for seg in lay.segments:
        if seg.library and sigs:
            library.update(match_library(lay, seg, [a for a, *_ in funcs if seg.vram <= a < seg.end], sigs))
    print(f"{len(library)} library functions named by their signatures")

    out = [f"# Written from the ROM by lib/turok-engine/tools/gen_syms.py and {args.layout}; not edited by hand.", ""]
    used = set()
    for seg in lay.segments:
        out += ["[[section]]", f'name = ".{seg.name}"', f"rom = 0x{seg.rom:06X}", f"vram = 0x{seg.vram:08X}",
                f"size = 0x{seg.data_end - seg.rom:X}", "", "functions = ["]
        for a, end, name, src in funcs:
            if not seg.vram <= a < seg.end:
                continue
            name = hand.get(a) or library.get(a) or name
            if name in HOST_LIBC:
                name += "_n64"
            if name is None or name in used:
                name = f"func_{a:08X}"
            used.add(name)
            out.append(f'    {{ name = "{name}", vram = 0x{a:08X}, size = 0x{end - a:X} }},')
            if args.report:
                print(f"  {a:08X} {end - a:6X} {name} {src}")
        out += ["]", ""]
    for al in lay.aliases:
        out += ["[[section]]", f'name = ".{al["name"]}"', f"rom = 0x{al['rom']:06X}", f"vram = 0x{al['vram']:08X}",
                f"size = 0x{al['size']:X}", "", "functions = []", ""]
    lay.out.parent.mkdir(parents=True, exist_ok=True)
    lay.out.write_text("\n".join(out), newline="\n")
    print(f"wrote {lay.out.relative_to(root)}")


if __name__ == "__main__":
    main()
