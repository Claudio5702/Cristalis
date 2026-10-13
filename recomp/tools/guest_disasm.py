"""Print the PowerPC instruction comments XenonRecomp left in the generated C++
around a guest address.

Usage: guest_disasm.py <ppc dir> <hex address> [instructions before] [instructions after]"""
import glob
import os
import re
import sys

ppc_dir = sys.argv[1]
target = int(sys.argv[2], 16)
before = int(sys.argv[3]) if len(sys.argv) > 3 else 30
after = int(sys.argv[4]) if len(sys.argv) > 4 else 10

mapping = open(os.path.join(ppc_dir, "ppc_func_mapping.cpp"), encoding="utf-8").read()
functions = sorted(int(m.group(1), 16) for m in re.finditer(r"\{ 0x([0-9A-F]+), sub_", mapping))
owner = max(f for f in functions if f <= target)
header = "PPC_FUNC_IMPL(__imp__sub_%08X)" % owner

for path in glob.glob(os.path.join(ppc_dir, "ppc_recomp.*.cpp")):
    lines = open(path, encoding="utf-8").read().split("\n")
    try:
        start = next(i for i, line in enumerate(lines) if line.startswith(header))
    except StopIteration:
        continue

    # Every instruction is one comment line; addresses advance by 4 from the
    # function start, and labels resynchronise the count.
    address = owner
    listing = []
    for line in lines[start + 1:]:
        if line.startswith("}"):
            break
        label = re.match(r"loc_([0-9A-F]+):", line)
        if label:
            address = int(label.group(1), 16)
            continue
        if line.startswith("\t// "):
            listing.append((address, line[4:]))
            address += 4

    print("sub_%08X (%s)" % (owner, os.path.basename(path)))
    for address, text in listing:
        if target - before * 4 <= address <= target + after * 4:
            print("%s %08X  %s" % ("=>" if address == target else "  ", address, text))
    break
