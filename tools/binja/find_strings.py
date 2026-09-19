"""List strings matching a regex in a Binary Ninja database, with the functions referencing them.

    python find_strings.py <db.bndb|project file> <regex> [max]
"""
import re
import sys

import binaryninja as bn


def open_view(path):
    if path.lower().endswith(".bnpr"):
        proj = bn.Project.open_project(path)
        for f in proj.files:
            if f.name.lower().endswith(".bndb"):
                return bn.load(f.path_on_disk, update_analysis=False)
        raise SystemExit("project has no .bndb")
    return bn.load(path, update_analysis=False)


def main():
    bv = open_view(sys.argv[1])
    rx = re.compile(sys.argv[2], re.IGNORECASE)
    limit = int(sys.argv[3]) if len(sys.argv) > 3 else 200
    n = 0
    for s in bv.get_strings():
        text = s.value
        if not rx.search(text):
            continue
        refs = sorted({r.function.start for r in bv.get_code_refs(s.start) if r.function})
        print("%#x  %-70s refs=%d %s" % (s.start, repr(text[:70]), len(refs),
                                          " ".join("%#x" % a for a in refs[:6])))
        n += 1
        if n >= limit:
            print("... (limit)")
            break
    print("%d strings matched" % n)


if __name__ == "__main__":
    main()
