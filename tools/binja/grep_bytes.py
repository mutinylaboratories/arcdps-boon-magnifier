"""Raw byte search across a database's data sections, ASCII and UTF-16LE, with code refs.

    python grep_bytes.py <db|project> <text> [max]
"""
import re
import sys

import binaryninja as bn
from find_strings import open_view


def main():
    bv = open_view(sys.argv[1])
    needle = sys.argv[2]
    limit = int(sys.argv[3]) if len(sys.argv) > 3 else 60
    print("string table size:", len(bv.get_strings()))
    pats = [("ascii", re.compile(re.escape(needle.encode("ascii")), re.IGNORECASE)),
            ("utf16", re.compile(re.escape(needle.encode("utf-16-le")), re.IGNORECASE))]
    n = 0
    for sec in bv.sections.values():
        if sec.semantics == bn.SectionSemantics.ReadOnlyCodeSectionSemantics:
            continue
        blob = bv.read(sec.start, sec.length)
        for kind, rx in pats:
            for m in rx.finditer(blob):
                addr = sec.start + m.start()
                # widen to the surrounding printable run for context
                lo = m.start()
                step = 2 if kind == "utf16" else 1
                while lo - step >= 0 and 0x20 <= blob[lo - step] < 0x7F and (step == 1 or blob[lo - step + 1] == 0):
                    lo -= step
                hi = m.end()
                while hi + step <= len(blob) and 0x20 <= blob[hi] < 0x7F and (step == 1 or blob[hi + 1] == 0):
                    hi += step
                raw = blob[lo:hi]
                text = raw.decode("utf-16-le", "replace") if kind == "utf16" else raw.decode("ascii", "replace")
                refs = sorted({r.function.start for r in bv.get_code_refs(sec.start + lo) if r.function})
                print("%#x %-5s %-8s %-80s refs=%d %s" % (addr, kind, sec.name, repr(text[:80]), len(refs),
                                                         " ".join("%#x" % a for a in refs[:5])))
                n += 1
                if n >= limit:
                    print("... (limit)")
                    return
    print("%d hits" % n)


if __name__ == "__main__":
    main()
