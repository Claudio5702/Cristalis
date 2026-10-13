"""Disassemble Xenos shader microcode dumped by the runtime (run/shaders/*.bin).
Usage: xenos_disasm.py <shader.bin> [...]"""
import struct
import sys

CF_NAMES = ["nop", "exec", "exec_end", "cond_exec", "cond_exec_end", "cond_pred_exec", "cond_pred_exec_end",
            "loop_start", "loop_end", "cond_call", "return", "cond_jmp", "alloc", "cond_exec_pred_clean",
            "cond_exec_pred_clean_end", "mark_vs_fetch_done"]
VECTOR_OPS = ["add", "mul", "max", "min", "seq", "sgt", "sge", "sne", "frc", "trunc", "floor", "mad", "cndeq",
              "cndge", "cndgt", "dp4", "dp3", "dp2add", "cube", "max4", "setp_eq_push", "setp_ne_push",
              "setp_gt_push", "setp_ge_push", "kill_eq", "kill_gt", "kill_ge", "kill_ne", "dst", "maxa"]
SCALAR_OPS = ["adds", "adds_prev", "muls", "muls_prev", "muls_prev2", "maxs", "mins", "seqs", "sgts", "sges",
              "snes", "frcs", "truncs", "floors", "exp", "logc", "log", "rcpc", "rcpf", "rcp", "rsqc", "rsqf",
              "rsq", "maxas", "maxasf", "subs", "subs_prev", "setp_eq", "setp_ne", "setp_gt", "setp_ge",
              "setp_inv", "setp_pop", "setp_clr", "setp_rstr", "kills_eq", "kills_gt", "kills_ge", "kills_ne",
              "kills_one", "sqrt", "?41", "mulsc0", "mulsc1", "addsc0", "addsc1", "subsc0", "subsc1", "sin",
              "cos", "retain_prev"]
VERTEX_FORMATS = {6: "8_8_8_8", 7: "2_10_10_10", 16: "10_11_11", 17: "11_11_10", 25: "16_16", 26: "16_16_16_16",
                  31: "16_16_FLOAT", 32: "16_16_16_16_FLOAT", 33: "32", 34: "32_32", 35: "32_32_32_32",
                  36: "32_FLOAT", 37: "32_32_FLOAT", 38: "32_32_32_32_FLOAT", 57: "32_32_32_FLOAT"}
ALLOC_TYPES = ["none", "position", "interpolators/colors", "memexport"]


def bits(value, low, count):
    return (value >> low) & ((1 << count) - 1)


def swizzle(code, count=4):
    return "".join("xyzw"[(i + bits(code, i * 2, 2)) & 3] for i in range(count))


def mask(code):
    return "".join("xyzw"[i] for i in range(4) if code & (1 << i)) or "-"


def source(reg, sel, swiz, negate, is_vertex):
    name = ("r%d" % (reg & 0x3F)) if sel else ("c%d" % reg)
    return "%s%s.%s" % ("-" if negate else "", name, swizzle(swiz))


def dest(reg, export, is_vertex):
    if not export:
        return "r%d" % reg
    if is_vertex:
        return {62: "oPos", 63: "oPts"}.get(reg, "o%d" % reg)
    return {61: "oDepth"}.get(reg, "oC%d" % reg)


def fetch(w, is_vertex):
    opcode = bits(w[0], 0, 5)
    if opcode == 0:
        index = bits(w[0], 20, 5) * 3 + bits(w[0], 25, 2)
        fmt = bits(w[1], 16, 6)
        return "vfetch r%d.%s = vf%d[r%d.%s] format=%s %s%s stride=%d offset=%d%s" % (
            bits(w[0], 12, 6), "".join("xyzw01?_"[bits(w[1], i * 3, 3)] for i in range(4)),
            index, bits(w[0], 5, 6), "xyzw"[bits(w[0], 30, 2)],
            VERTEX_FORMATS.get(fmt, "?%d" % fmt), "signed" if bits(w[1], 12, 1) else "unsigned",
            " integer" if bits(w[1], 13, 1) else " normalized", bits(w[2], 0, 8), bits(w[2], 8, 23),
            " (mini)" if bits(w[1], 30, 1) else "")
    if opcode == 1:
        return "tfetch%dD r%d.%s = tf%d[r%d.%s]" % (
            bits(w[2], 16, 2) + 1, bits(w[0], 12, 6), "".join("xyzw01?_"[bits(w[1], i * 3, 3)] for i in range(4)),
            bits(w[0], 20, 5), bits(w[0], 5, 6), swizzle(bits(w[0], 26, 6), 3))
    return "fetch opcode %d: %08X %08X %08X" % (opcode, w[0], w[1], w[2])


def alu(w, is_vertex):
    export = bits(w[0], 15, 1)
    vector_mask = bits(w[0], 16, 4)
    scalar_mask = bits(w[0], 20, 4)
    vector_op = bits(w[2], 24, 5)
    scalar_op = bits(w[0], 26, 6)
    src1 = source(bits(w[2], 16, 8), bits(w[2], 31, 1), bits(w[1], 16, 8), bits(w[1], 26, 1), is_vertex)
    src2 = source(bits(w[2], 8, 8), bits(w[2], 30, 1), bits(w[1], 8, 8), bits(w[1], 25, 1), is_vertex)
    src3 = source(bits(w[2], 0, 8), bits(w[2], 29, 1), bits(w[1], 0, 8), bits(w[1], 24, 1), is_vertex)
    parts = []
    if vector_mask:
        name = VECTOR_OPS[vector_op] if vector_op < len(VECTOR_OPS) else "?%d" % vector_op
        operands = [src1, src2, src3] if name in ("mad", "cndeq", "cndge", "cndgt", "dp2add") else (
            [src1] if name in ("frc", "trunc", "floor") else [src1, src2])
        parts.append("%s.%s = %s(%s)%s" % (dest(bits(w[0], 0, 6), export, is_vertex), mask(vector_mask), name,
                                           ", ".join(operands), " sat" if bits(w[0], 24, 1) else ""))
    if scalar_mask:
        name = SCALAR_OPS[scalar_op] if scalar_op < len(SCALAR_OPS) else "?%d" % scalar_op
        parts.append("%s.%s = %s(%s)%s" % (dest(bits(w[0], 8, 6), export, is_vertex), mask(scalar_mask), name, src3,
                                           " sat" if bits(w[0], 25, 1) else ""))
    return "; ".join(parts) or "nop"


def disassemble(path):
    data = open(path, "rb").read()
    words = struct.unpack(">%dI" % (len(data) // 4), data)
    is_vertex = "vs_" in path.replace("\\", "/").split("/")[-1]
    print("==== %s (%d words, %s) ====" % (path.replace("\\", "/").split("/")[-1], len(words), "vertex" if is_vertex else "pixel"))

    # Control flow: two 48-bit instructions per 3 words, until an *_end.
    done = False
    for i in range(0, len(words) - 2, 3):
        a = words[i] | ((words[i + 1] & 0xFFFF) << 32)
        b = (words[i + 1] >> 16) | (words[i + 2] << 16)
        for cf in (a, b):
            opcode = bits(cf, 44, 4)
            name = CF_NAMES[opcode]
            if name == "alloc":
                print("  %-10s %s size=%d" % (name, ALLOC_TYPES[bits(cf, 41, 2)], bits(cf, 0, 3)))
            elif "exec" in name:
                address, count, sequence = bits(cf, 0, 12), bits(cf, 12, 3), bits(cf, 16, 12)
                print("  %-10s addr=%d count=%d" % (name, address, count))
                for k in range(count):
                    w = words[(address + k) * 3:(address + k) * 3 + 3]
                    is_fetch = bits(sequence, k * 2, 1)
                    print("      %2d: %s" % (address + k, fetch(w, is_vertex) if is_fetch else alu(w, is_vertex)))
                if name.endswith("_end"):
                    done = True
            elif name != "nop":
                print("  %-10s %012X" % (name, cf))
            if done:
                break
        if done:
            break
    print()


for path in sys.argv[1:]:
    disassemble(path)
