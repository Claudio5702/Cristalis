"""Report what lies in the .text gaps not covered by .pdata functions.
Usage: text_gaps.py <image.bin>  (image dumped by xex_dump, base 0x82000000)"""
import collections
import struct
import sys

BASE = 0x82000000
PDATA = (0x8209A400, 0x0000DBF0)
TEXT = (0x820B0000, 0x002ED6CC)

img = open(sys.argv[1], "rb").read()


def word(addr):
    return struct.unpack_from(">I", img, addr - BASE)[0]


funcs = []
for off in range(0, PDATA[1], 8):
    begin = word(PDATA[0] + off)
    data = word(PDATA[0] + off + 4)
    if begin == 0:
        continue
    length = ((data >> 8) & 0x3FFFFF) * 4
    funcs.append((begin, length))
funcs.sort()
print("pdata functions: %d, zero-length: %d" % (len(funcs), sum(1 for _, l in funcs if l == 0)))
print("first=0x%08X last=0x%08X" % (funcs[0][0], funcs[-1][0]))
for b, l in funcs:
    if l == 0:
        print("  zero-length pdata entry at 0x%08X" % b)

text_end = TEXT[0] + TEXT[1]
gaps = []
cursor = TEXT[0]
for b, l in funcs:
    if b < TEXT[0] or b >= text_end:
        continue
    if b > cursor:
        gaps.append((cursor, b))
    cursor = max(cursor, b + l)
if cursor < text_end:
    gaps.append((cursor, text_end))

total = sum(e - s for s, e in gaps)
print("gaps: %d, bytes: 0x%X (%.1f%% of .text)" % (len(gaps), total, 100.0 * total / TEXT[1]))

first_words = collections.Counter()
all_words = collections.Counter()
for s, e in gaps:
    first_words[word(s)] += 1
    for a in range(s, e - (e - s) % 4, 4):
        all_words[word(a)] += 1

print("most common first word of a gap:")
for w, c in first_words.most_common(12):
    print("  0x%08X x%d" % (w, c))
print("most common word inside gaps:")
for w, c in all_words.most_common(12):
    print("  0x%08X x%d" % (w, c))

print("largest gaps:")
for s, e in sorted(gaps, key=lambda g: g[0] - g[1])[:12]:
    print("  0x%08X-0x%08X size=0x%X first=%s" % (
        s, e, e - s, " ".join("%08X" % word(a) for a in range(s, min(e, s + 16), 4))))
