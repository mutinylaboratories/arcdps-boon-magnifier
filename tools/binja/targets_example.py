"""Locators for the functions/data to sign. Copy to targets.py and fill in.

Each value is a function taking a BinaryView and returning the address to sign, or None.
Prefer anchors that survive patches: strings the code references, imports it calls,
call-graph shape (a function with N callers that calls M known things), not raw addresses.
"""
import binaryninja as bn


def by_string_xref(text, depth=0):
    """Function containing a reference to the given string; depth=1 -> its single caller, etc."""

    def locate(bv):
        needle = text.encode("utf-8")
        for s in bv.get_strings():
            if bv.read(s.start, s.length) == needle:
                for ref in bv.get_code_refs(s.start):
                    f = ref.function
                    for _ in range(depth):
                        callers = f.callers
                        if len(callers) != 1:
                            return None
                        f = callers[0]
                    return f.start
        return None

    return locate


def by_import_caller(import_name):
    """The single function that calls the named import (e.g. a Win32 API)."""

    def locate(bv):
        sym = bv.get_symbol_by_raw_name(import_name)
        if sym is None:
            return None
        callers = {ref.function.start for ref in bv.get_code_refs(sym.address)}
        return callers.pop() if len(callers) == 1 else None

    return locate


TARGETS = {
    # "self_buff_update": by_string_xref("some string the buff code references", depth=1),
}
