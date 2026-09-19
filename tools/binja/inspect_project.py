"""Summarise a Binary Ninja project: files, analysis state, user-named functions, user types.

    python inspect_project.py <project.bnpr>
"""
import os
import sys

import binaryninja as bn


def main():
    path = sys.argv[1]
    proj = bn.Project.open_project(path)
    print("project:", proj.name, "at", proj.path)
    print("files:")
    for f in proj.files:
        print("  %-40s %s  (%s)" % (f.name, f.get_path_in_project(), f.description))
        try:
            size = os.path.getsize(f.path_on_disk)
        except OSError:
            size = -1
        print("      on disk: %s  %d bytes" % (f.path_on_disk, size))

    for f in proj.files:
        if not f.name.lower().endswith(".bndb"):   # the database carries the analysis; skip the raw exe
            continue
        print("\n=== opening %s ===" % f.name)
        bv = bn.load(f.path_on_disk, update_analysis=False)
        if bv is None:
            print("  could not open")
            continue
        print("  view type: %s, arch: %s, base: %#x, size: %#x" % (bv.view_type, bv.arch.name, bv.start, bv.end - bv.start))
        print("  analysis: %s functions, has database: %s" % (len(bv.functions), bv.file.has_database))
        print("  sections:", ", ".join("%s(%#x)" % (s.name, s.length) for s in bv.sections.values()))

        named = [fn for fn in bv.functions if not fn.name.startswith(("sub_", "j_sub_")) and fn.symbol.auto is False]
        print("  user-named functions: %d" % len(named))
        for fn in named[:60]:
            print("    %#x  %s" % (fn.start, fn.name))

        user_types = [(n, t) for n, t in bv.types.items() if not str(n).startswith(("_", "tag"))]
        print("  types: %d (showing user-looking ones)" % len(bv.types))
        for n, t in user_types[:60]:
            print("    %s  = %s" % (n, str(t)[:100]))

        print("  data vars with user names:")
        n = 0
        for addr, dv in bv.data_vars.items():
            sym = bv.get_symbol_at(addr)
            if sym and not sym.auto and not sym.name.startswith("data_"):
                print("    %#x  %s : %s" % (addr, sym.name, dv.type))
                n += 1
                if n >= 60:
                    break
        tags = list(bv.tags)
        print("  tags: %d" % len(tags))
        for addr, tag in tags[:40]:
            print("    %#x  [%s] %s" % (addr, tag.type.name, tag.data))
        bv.file.close()


if __name__ == "__main__":
    main()
