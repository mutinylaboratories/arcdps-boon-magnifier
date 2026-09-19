"""Report on functions: signature, size, callers, strings referenced, and HLIL.

    python func_report.py <db|project> <addr> [addr...] [--hlil N]   (N = max HLIL lines, default 60)
"""
import sys

import binaryninja as bn
from find_strings import open_view


def readable_at(bv, addr, limit=90):
    data = bv.read(addr, limit)
    if not data:
        return None
    if all(0x20 <= b < 0x7F or b == 0 for b in data[:4]) and data[0] != 0:
        end = data.find(b"\x00")
        text = data[: end if end != -1 else limit]
        if len(text) >= 4 and all(0x20 <= b < 0x7F for b in text):
            return text.decode("ascii")
    if len(data) >= 8 and data[1] == 0 and data[3] == 0 and 0x20 <= data[0] < 0x7F:
        try:
            text = data.decode("utf-16-le", "ignore")
            text = text.split("\x00")[0]
            if len(text) >= 3:
                return "L" + text
        except Exception:
            pass
    return None


def main():
    args = sys.argv[1:]
    hlil_n = 60
    if "--hlil" in args:
        i = args.index("--hlil")
        hlil_n = int(args[i + 1])
        del args[i:i + 2]
    bv = open_view(args[0])
    for a in args[1:]:
        addr = int(a, 16)
        fn = bv.get_function_at(addr)
        if fn is None:
            print("%#x: no function" % addr)
            continue
        print("=" * 100)
        print("%#x  %s  size=%d bytes  callers=%d  callees=%d" % (
            fn.start, fn.name, fn.total_bytes, len(fn.callers), len(fn.callees)))
        print("  signature: %s" % fn.type)
        print("  callers:   %s" % " ".join("%#x" % c.start for c in fn.callers[:12]))
        # strings this function references, via its constant data references
        seen = []
        for block in fn.basic_blocks:
            addr2 = block.start
            while addr2 < block.end:
                for ref in bv.get_data_refs_from(addr2):
                    if ref in seen:
                        continue
                    text = readable_at(bv, ref)
                    if text:
                        seen.append(ref)
                        print("  string @%#x: %r" % (ref, text[:100]))
                ln = bv.get_instruction_length(addr2)
                if ln == 0:
                    break
                addr2 += ln
        print("  --- HLIL ---")
        n = 0
        for line in fn.hlil.root.lines if fn.hlil else []:
            print("    " + str(line))
            n += 1
            if n >= hlil_n:
                print("    ... (%d more lines)" % (len(list(fn.hlil.root.lines)) - n))
                break


if __name__ == "__main__":
    main()
