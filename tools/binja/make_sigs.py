"""Generate arcdps_boon_magnifier_sigs.ini from an executable with Binary Ninja (headless).

    python make_sigs.py <exe|db.bndb|project.bnpr> <out.ini> [--targets targets.py] [--enable]
    python make_sigs.py <exe|db.bndb|project.bnpr> --self-test [N]

A signature is the target function's leading bytes with every address operand (call/jmp
targets, RIP-relative displacements, absolute image addresses, per Binary Ninja's
disassembly tokens) replaced by wildcards, extended one instruction at a time until it matches exactly once in the
code sections. The plugin re-resolves it at load, so after a game patch you rerun this
script instead of rebuilding the DLL.

targets.py defines TARGETS = {"sig_name": locator, ...} where locator(bv) returns the
address of the function to sign, or (function_address, global_address) to make the
signature resolve to that global through the function's rel32 reference to it, or None.
See targets_example.py and targets_gw2.py.

--self-test signs N random named functions of the exe and verifies each resolves back to
its own address; it exercises the generator without needing any targets.
"""
import argparse
import importlib.util
import os
import random
import re
import struct
import sys

import binaryninja as bn

MIN_LEN = 10
MAX_LEN = 96


# ---------------------------------------------------------------------------------
# PE identity: the plugin refuses a signature file made from a different build.
# ---------------------------------------------------------------------------------
def pe_identity(bv):
    """(TimeDateStamp, SizeOfImage) read from the mapped PE headers of the view."""
    e_lfanew = struct.unpack("<I", bv.read(bv.start + 0x3C, 4))[0]
    nt = bv.start + e_lfanew
    timestamp = struct.unpack("<I", bv.read(nt + 8, 4))[0]
    size_of_image = struct.unpack("<I", bv.read(nt + 0x18 + 0x38, 4))[0]
    return timestamp, size_of_image


def open_input(path):
    """A raw exe (analysed now), a .bndb, or a .bnpr project holding one (analysis reused)."""
    if path.lower().endswith(".bnpr"):
        proj = bn.Project.open_project(path)
        for f in proj.files:
            if f.name.lower().endswith(".bndb"):
                return bn.load(f.path_on_disk, update_analysis=False)
        raise SystemExit("project has no .bndb")
    if path.lower().endswith(".bndb"):
        return bn.load(path, update_analysis=False)
    bv = bn.load(path)
    bv.update_analysis_and_wait()
    return bv


# ---------------------------------------------------------------------------------
# Code bytes and matching (pure Python; mirrors src/core/sigscan.cpp)
# ---------------------------------------------------------------------------------
class CodeImage:
    """Executable sections read into memory, so uniqueness checks are plain regex scans."""

    def __init__(self, bv):
        self.ranges = []
        for sec in bv.sections.values():
            if sec.semantics != bn.SectionSemantics.ReadOnlyCodeSectionSemantics:
                continue
            self.ranges.append((sec.start, bv.read(sec.start, sec.length)))
        if not self.ranges:
            raise SystemExit("no code sections found")

    def count(self, pattern, limit=2):
        regex = re.compile(b"".join(b"." if m == 0 else re.escape(bytes([v])) for v, m in pattern), re.DOTALL)
        hits = []
        for start, blob in self.ranges:
            for m in regex.finditer(blob):
                hits.append(start + m.start())
                if len(hits) >= limit:
                    return hits
        return hits


ADDRESS_TOKENS = {
    bn.InstructionTextTokenType.PossibleAddressToken,
    bn.InstructionTextTokenType.CodeRelativeAddressToken,
    bn.InstructionTextTokenType.CodeSymbolToken,
    bn.InstructionTextTokenType.DataSymbolToken,
    bn.InstructionTextTokenType.ImportToken,
    bn.InstructionTextTokenType.ExternalSymbolToken,
}


def instruction_pattern(bv, addr, length, raw):
    """Bytes of one instruction with address operands wildcarded. Returns [(byte, mask)]."""
    mask = [1] * length
    tokens, _ = bv.arch.get_instruction_text(raw, addr)
    for tok in tokens or []:
        if tok.type not in ADDRESS_TOKENS or not (bv.start <= tok.value < bv.end):
            continue
        # The operand is encoded either RIP-relative (rel32 from the instruction end) or as an
        # absolute immediate; wildcard whichever encoding actually appears in the bytes.
        rel = tok.value - (addr + length)
        candidates = []
        if -0x80000000 <= rel <= 0x7FFFFFFF:
            candidates.append(struct.pack("<i", rel))
        candidates.append(struct.pack("<Q", tok.value))
        if tok.value <= 0xFFFFFFFF:
            candidates.append(struct.pack("<I", tok.value))
        for enc in candidates:
            k = raw.find(enc)
            while k != -1:
                for j in range(k, k + len(enc)):
                    mask[j] = 0
                k = raw.find(enc, k + 1)
    return [(raw[i], mask[i]) for i in range(length)]


def make_signature(bv, image, addr, max_len=MAX_LEN, min_len=MIN_LEN):
    """Grow a wildcarded byte pattern from addr until it is unique. Returns pattern or None."""
    pattern = []
    cur = addr
    while len(pattern) < max_len:
        length = bv.get_instruction_length(cur)
        if length == 0:
            return None
        raw = bv.read(cur, length)
        pattern.extend(instruction_pattern(bv, cur, length, raw))
        cur += length
        if len(pattern) >= min_len and any(m for _, m in pattern):
            hits = image.count(pattern)
            if hits == [addr]:
                return pattern
    return None


def rel32_offset_for_data(bv, func_addr, data_addr):
    """Byte offset (from func_addr) of the rel32 in the first instruction of the function
    that references data_addr, or None. Lets a signature name a global via follow_rel32."""
    fn = bv.get_function_at(func_addr)
    if fn is None:
        return None
    for ref in bv.get_code_refs(data_addr):
        if ref.function != fn:
            continue
        length = bv.get_instruction_length(ref.address)
        raw = bv.read(ref.address, length)
        rel = data_addr - (ref.address + length)
        k = raw.find(struct.pack("<i", rel))
        if k != -1:
            return ref.address - func_addr + k
    return None


def pattern_text(pattern):
    return " ".join("%02X" % v if m else "??" for v, m in pattern)


# ---------------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------------
def write_sigfile(path, bv, source, sigs, enabled):
    ts, size = pe_identity(bv)
    lines = [
        "# generated by tools/binja/make_sigs.py from %s" % os.path.basename(source),
        "enabled=%d" % (1 if enabled else 0),
        "pe_timestamp=0x%08x" % ts,
        "pe_size_of_image=0x%x" % size,
        "",
    ]
    for name, pattern, follow in sigs:
        lines += ["[%s]" % name, "pattern=%s" % pattern_text(pattern)]
        if follow is not None:
            lines.append("follow_rel32=%d" % follow)
        lines.append("")
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(lines))


def load_targets(path):
    spec = importlib.util.spec_from_file_location("targets", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.TARGETS


# ---------------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("exe")
    ap.add_argument("out", nargs="?")
    ap.add_argument("--targets", default=os.path.join(os.path.dirname(__file__), "targets.py"))
    ap.add_argument("--enable", action="store_true", help="write enabled=1 (default 0: plugin stays inert)")
    ap.add_argument("--self-test", nargs="?", const=25, type=int, metavar="N")
    args = ap.parse_args()

    print("loading %s ..." % args.exe)
    bv = open_input(args.exe)
    image = CodeImage(bv)
    ts, size = pe_identity(bv)
    print("pe_timestamp=0x%08x size_of_image=0x%x, %d functions" % (ts, size, len(bv.functions)))

    if args.self_test is not None:
        funcs = [f for f in bv.functions if f.total_bytes >= MIN_LEN]
        random.seed(1)
        sample = random.sample(funcs, min(args.self_test, len(funcs)))
        ok = 0
        for f in sample:
            pat = make_signature(bv, image, f.start)
            if pat is None:
                print("  FAIL no unique signature within %d bytes: %s @ %#x" % (MAX_LEN, f.name, f.start))
                continue
            hits = image.count(pat)
            if hits != [f.start]:
                print("  FAIL resolves to %s, expected %#x: %s" % (hits, f.start, f.name))
                continue
            ok += 1
            print("  ok %-40s %3d bytes  %s" % (f.name[:40], len(pat), pattern_text(pat)[:60]))
        print("self-test: %d/%d signatures unique and correct" % (ok, len(sample)))
        return 0 if ok == len(sample) else 1

    if not args.out:
        ap.error("out.ini is required unless --self-test is given")
    if not os.path.exists(args.targets):
        ap.error("targets file not found: %s (copy targets_example.py to targets.py)" % args.targets)

    sigs = []
    failed = False
    for name, locate in load_targets(args.targets).items():
        found = locate(bv)
        if found is None:
            print("  [%s] locator found nothing" % name)
            failed = True
            continue
        # A locator returns a code address, or (code_address, data_address) to sign the
        # function and point the signature at the global it references (follow_rel32).
        follow = None
        if isinstance(found, tuple):
            addr, data_addr = found
            follow = rel32_offset_for_data(bv, addr, data_addr)
            if follow is None:
                print("  [%s] function %#x has no rel32 reference to %#x" % (name, addr, data_addr))
                failed = True
                continue
        else:
            addr = found
        pat = make_signature(bv, image, addr, min_len=(follow + 4) if follow is not None else MIN_LEN)
        if pat is None:
            print("  [%s] no unique signature within %d bytes at %#x" % (name, MAX_LEN, addr))
            failed = True
            continue
        print("  [%s] %#x  %d bytes%s" % (name, addr, len(pat), "  -> data %#x via rel32@%d" % (data_addr, follow) if follow is not None else ""))
        sigs.append((name, pat, follow))

    if failed:
        print("not writing %s: some targets failed" % args.out)
        return 1
    write_sigfile(args.out, bv, args.exe, sigs, args.enable)
    print("wrote %s (%d signatures, enabled=%d)" % (args.out, len(sigs), int(args.enable)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
