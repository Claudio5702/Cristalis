"""Compute real bounds for leaf functions whose switch cases XenonRecomp reports as
"jumping outside function". Those functions have no .pdata entry, so their size was
guessed and stops at the bctr. Follows control flow (including jump table labels)
and prints a `functions = [...]` block for the recompiler config.

Usage: fix_function_bounds.py <image.bin> <switch_tables.toml> <xenonrecomp.log> <ppc_func_mapping.cpp>"""
import bisect
import collections
import re
import struct
import sys
import tomllib

BASE = 0x82000000
PDATA = (0x8209A400, 0x0000DBF0)
TEXT = (0x820B0000, 0x002ED6CC)
TEXT_END = TEXT[0] + TEXT[1]

img = open(sys.argv[1], "rb").read()


def word(addr):
    return struct.unpack_from(">I", img, addr - BASE)[0]


def sext(value, bits):
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


# Functions with unwind info: trusted bounds.
pdata = []
for off in range(0, PDATA[1], 8):
    begin = word(PDATA[0] + off)
    if begin:
        pdata.append((begin, ((word(PDATA[0] + off + 4) >> 8) & 0x3FFFFF) * 4))
pdata.sort()
pdata_starts = [b for b, _ in pdata]

# bl targets are function entry points too.
trusted = set(pdata_starts)
for addr in range(TEXT[0], TEXT_END - 3, 4):
    insn = word(addr)
    if insn >> 26 == 18 and insn & 3 == 1:
        trusted.add(addr + sext(insn & 0x03FFFFFC, 26))

with open(sys.argv[2], "rb") as f:
    switches = {s["base"]: s for s in tomllib.load(f)["switch"]}

sites = collections.defaultdict(set)
for line in open(sys.argv[3], encoding="utf-8", errors="replace"):
    m = re.search(r"Switch case at ([0-9A-F]+) is trying to jump outside function: ([0-9A-F]+)", line)
    if m:
        sites[int(m.group(1), 16)].add(int(m.group(2), 16))

recomp_funcs = sorted(int(m.group(1), 16) for m in re.finditer(
    r"\{ 0x([0-9A-F]+), sub_", open(sys.argv[4], encoding="utf-8").read()))


def analyse(start, limit):
    """Returns the set of instruction addresses reachable from start within [start, limit)."""
    visited = set()
    work = [start]
    while work:
        addr = work.pop()
        while start <= addr < limit and addr not in visited:
            insn = word(addr)
            if insn == 0:
                break
            visited.add(addr)

            switch = switches.get(addr)
            if switch is not None:
                targets = list(switch["labels"])
                if "default" in switch:
                    targets.append(switch["default"])
                work.extend(t for t in targets if start <= t < limit)

            op = insn >> 26
            lk = insn & 1
            bo = (insn >> 21) & 31
            always = (bo & 0x14) == 0x14
            if op == 18:
                if not lk:
                    target = sext(insn & 0x03FFFFFC, 26) + (0 if insn & 2 else addr)
                    if start < target < limit and target not in trusted:
                        work.append(target)
                    break
            elif op == 16:
                target = sext(insn & 0xFFFC, 16) + (0 if insn & 2 else addr)
                if not lk and start <= target < limit:
                    work.append(target)
                if always and not lk:
                    break
            elif op == 19 and ((insn >> 1) & 0x3FF) in (16, 528):
                if always and not lk:
                    break
            addr += 4
    return visited


results = []
prev_end = 0
for site in sorted(sites):
    if results and results[-1][0] <= site < results[-1][0] + results[-1][1]:
        continue

    i = bisect.bisect_right(pdata_starts, site) - 1
    floor = max(pdata[i][0] + pdata[i][1], prev_end)
    limit = pdata_starts[i + 1] if i + 1 < len(pdata_starts) else TEXT_END

    chosen = None
    lo = bisect.bisect_left(recomp_funcs, floor)
    hi = bisect.bisect_right(recomp_funcs, site)
    for candidate in recomp_funcs[lo:hi]:
        visited = analyse(candidate, limit)
        if site in visited:
            chosen = (candidate, max(visited) + 4 - candidate)
            break

    if chosen is None:
        print("# UNRESOLVED: switch at 0x%08X (floor=0x%08X limit=0x%08X)" % (site, floor, limit))
        continue

    results.append(chosen)
    prev_end = chosen[0] + chosen[1]

covered = 0
for site, labels in sorted(sites.items()):
    owner = next(((b, s) for b, s in results if b <= site < b + s), None)
    if owner is None:
        continue
    outside = [l for l in labels if not owner[0] <= l < owner[0] + owner[1]]
    if outside:
        print("# WARNING: switch 0x%08X still has %d labels outside 0x%08X+0x%X" % (
            site, len(outside), owner[0], owner[1]))
    else:
        covered += 1

print("# %d switch sites, %d fully covered by %d functions" % (len(sites), covered, len(results)))
print("functions = [")
for base, size in results:
    print("    { address = 0x%08X, size = 0x%X }," % (base, size))
print("]")
