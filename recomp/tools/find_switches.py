"""Find absolute jump table dispatches XenonAnalyse missed and print TOML entries
for them. Pattern, within a few instructions before a bctr:

    lis rT, hi ; addi rT, rT, lo      table address
    rlwinm rS, rIdx, 2, 0, 29         index * 4
    lwzx rX, rT, rS ; mtctr rX ; bctr

Usage: find_switches.py <image.bin> <switch_tables.toml>"""
import struct
import sys
import tomllib

BASE = 0x82000000
TEXT = (0x820B0000, 0x002ED6CC)
TEXT_END = TEXT[0] + TEXT[1]
IMAGE_END = BASE + 0x420000
WINDOW = 10

img = open(sys.argv[1], "rb").read()


def word(addr):
    return struct.unpack_from(">I", img, addr - BASE)[0]


def sext16(value):
    return value - 0x10000 if value & 0x8000 else value


def is_label(value):
    return TEXT[0] <= value < TEXT_END and value % 4 == 0


with open(sys.argv[2], "rb") as f:
    known = sorted(s["base"] for s in tomllib.load(f)["switch"])

found = []
for bctr in range(TEXT[0] + WINDOW * 4, TEXT_END, 4):
    if word(bctr) != 0x4E800420:
        continue

    window = [(a, word(a)) for a in range(bctr - WINDOW * 4, bctr, 4)]

    # mtctr rX
    mtctr = next(((a, (v >> 21) & 31) for a, v in reversed(window) if v & 0xFC1FFFFF == 0x7C0903A6), None)
    if mtctr is None:
        continue

    # lwzx rX, rA, rB
    lwzx = next(((a, (v >> 16) & 31, (v >> 11) & 31) for a, v in reversed(window)
                 if a < mtctr[0] and v >> 26 == 31 and (v >> 1) & 0x3FF == 23 and (v >> 21) & 31 == mtctr[1]), None)
    if lwzx is None:
        continue

    # rlwinm rS, rIdx, 2, 0, 29 feeding one lwzx operand
    scaled = next(((a, (v >> 21) & 31, (v >> 16) & 31) for a, v in reversed(window)
                   if a < lwzx[0] and v >> 26 == 21 and v & 0xFFFF == 0x103A and (v >> 16) & 31 in lwzx[1:]), None)
    if scaled is None:
        continue
    index_register = scaled[1]
    table_register = lwzx[1] if lwzx[2] == scaled[2] else lwzx[2]

    # lis rT, hi ; addi rT, rT, lo
    addi = next(((a, sext16(v & 0xFFFF)) for a, v in reversed(window)
                 if a < lwzx[0] and v >> 26 == 14 and (v >> 21) & 31 == table_register and (v >> 16) & 31 == table_register), None)
    lis = next(((a, (v & 0xFFFF) << 16) for a, v in reversed(window)
                if a < lwzx[0] and v >> 26 == 15 and (v >> 21) & 31 == table_register and (v >> 16) & 31 == 0), None)
    if addi is None or lis is None:
        continue
    table = (lis[1] + addi[1]) & 0xFFFFFFFF
    if not BASE <= table < IMAGE_END:
        continue

    labels = []
    cursor = table
    while cursor < IMAGE_END and is_label(word(cursor)):
        # A table stored in .text ends where its first case begins.
        if labels and TEXT[0] <= table < TEXT_END and cursor >= min(labels):
            break
        labels.append(word(cursor))
        cursor += 4
    if len(labels) < 2:
        continue

    start = min(lis[0], addi[0], scaled[0])
    if any(start <= base <= bctr for base in known):
        continue
    found.append((start, bctr, index_register, table, labels))

print("# %d dispatches not covered by the existing switch tables" % len(found))
for start, bctr, index_register, table, labels in found:
    print()
    print("# ---- SCANNED: absolute table at 0x%08X, bctr at 0x%08X (tools/find_switches.py) ----" % (table, bctr))
    print("[[switch]]")
    print("base = 0x%08X" % start)
    print("r = %d" % index_register)
    print("default = 0x%08X" % labels[0])
    print("labels = [")
    for label in labels:
        print("    0x%08X," % label)
    print("]")
