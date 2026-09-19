"""Find vtables assigned in functions of a source file and decompile a given slot.

    python vtable_probe.py <db|project> <path-substring> <slot-byte-offset> [max-vtables]

Lists functions referencing the source path that store a .rdata address into *arg1
(constructor pattern), then prints the HLIL of the function found at vtable+slot.
"""
import sys

import binaryninja as bn
from find_strings import open_view


def path_functions(bv, needle):
    needle = needle.encode("ascii")
    funcs = {}
    for sec in bv.sections.values():
        if sec.semantics == bn.SectionSemantics.ReadOnlyCodeSectionSemantics:
            continue
        blob = bv.read(sec.start, sec.length)
        k = blob.find(needle)
        while k != -1:
            lo = k
            while lo > 0 and 0x20 <= blob[lo - 1] < 0x7F:
                lo -= 1
            for ref in bv.get_code_refs(sec.start + lo):
                if ref.function:
                    funcs[ref.function.start] = ref.function
            k = blob.find(needle, k + 1)
    return funcs


def main():
    bv = open_view(sys.argv[1])
    slot = int(sys.argv[3], 16)
    limit = int(sys.argv[4]) if len(sys.argv) > 4 else 6
    rdata = [s for s in bv.sections.values() if s.name == ".rdata"][0]
    seen = set()
    for start, fn in sorted(path_functions(bv, sys.argv[2]).items()):
        for insn in fn.hlil.instructions:
            if insn.operation != bn.HighLevelILOperation.HLIL_ASSIGN:
                continue
            dest, src = insn.dest, insn.src
            if dest.operation != bn.HighLevelILOperation.HLIL_DEREF:
                continue
            if src.operation not in (bn.HighLevelILOperation.HLIL_CONST_PTR, bn.HighLevelILOperation.HLIL_CONST):
                continue
            vt = src.constant
            if not (rdata.start <= vt < rdata.end) or vt in seen:
                continue
            # only "*arg1 = vt" (first store into the object) is a vtable candidate
            if "arg1" not in str(dest):
                continue
            seen.add(vt)
            target = int.from_bytes(bv.read(vt + slot, 8), "little")
            tfn = bv.get_function_at(target)
            print("vtable %#x (set in %s %#x), slot +%#x -> %#x %s" % (
                vt, fn.name, fn.start, slot, target, tfn.name if tfn else "(no function)"))
            if tfn:
                for line in list(tfn.hlil.root.lines)[:12]:
                    print("      " + str(line))
            if len(seen) >= limit:
                return


if __name__ == "__main__":
    main()
