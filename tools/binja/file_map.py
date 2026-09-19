"""Map every function that references a given source-file string: assert texts + line numbers.

    python file_map.py <db|project> <substring-of-path>
"""
import sys

import binaryninja as bn
from find_strings import open_view
from func_report import readable_at


def main():
    bv = open_view(sys.argv[1])
    needle = sys.argv[2].encode("ascii")
    # find the path string(s)
    paths = []
    for sec in bv.sections.values():
        if sec.semantics == bn.SectionSemantics.ReadOnlyCodeSectionSemantics:
            continue
        blob = bv.read(sec.start, sec.length)
        k = blob.find(needle)
        while k != -1:
            lo = k
            while lo > 0 and 0x20 <= blob[lo - 1] < 0x7F:
                lo -= 1
            paths.append(sec.start + lo)
            k = blob.find(needle, k + 1)
    funcs = {}
    for p in paths:
        for ref in bv.get_code_refs(p):
            if ref.function:
                funcs.setdefault(ref.function.start, ref.function)
    print("%d path string(s), %d referencing functions" % (len(paths), len(funcs)))
    for start in sorted(funcs):
        fn = funcs[start]
        print("\n%#x  %s  size=%d callers=%d  sig=%s" % (fn.start, fn.name, fn.total_bytes, len(fn.callers), fn.type))
        # assert calls: collect (string, line) from HLIL call sites
        try:
            for insn in fn.hlil.instructions:
                s = str(insn)
                if "Perforce" in s and "(" in s:
                    # keep the assert expression and line number, drop the path
                    parts = s.split('"')
                    expr = parts[1] if len(parts) > 1 else ""
                    tail = s.rsplit(",", 1)[-1].strip(" )")
                    print("    assert %-70s line %s" % (expr[:70], tail))
        except Exception as e:
            print("    (hlil unavailable: %s)" % e)


if __name__ == "__main__":
    main()
