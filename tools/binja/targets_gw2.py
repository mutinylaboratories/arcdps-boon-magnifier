r"""Locators for the Stability polling source (see docs/gw2-buff-internals.md).

    python make_sigs.py "C:\Program Files\Guild Wars 2\Gw2-64.exe" out.ini --targets targets_gw2.py --enable

Everything is anchored on assert strings and call shape, not addresses, so it should
survive patches as long as the code keeps its structure. Nothing here is a hook target;
the plugin only reads memory through the chain these signatures unlock.
"""
import binaryninja as bn


def _string_addr(bv, text):
    needle = text.encode("ascii")
    for sec in bv.sections.values():
        if sec.semantics == bn.SectionSemantics.ReadOnlyCodeSectionSemantics:
            continue
        blob = bv.read(sec.start, sec.length)
        k = blob.find(needle)
        while k != -1:
            # must be the start of a string (preceded by NUL) to avoid substring hits
            if k == 0 or blob[k - 1] == 0:
                return sec.start + k
            k = blob.find(needle, k + 1)
    return None


def _func_referencing(bv, text):
    addr = _string_addr(bv, text)
    if addr is None:
        return None
    funcs = {r.function.start: r.function for r in bv.get_code_refs(addr) if r.function}
    return funcs.popitem()[1] if len(funcs) == 1 else None


def buff_apply_handler(bv):
    """CmbtCliMsg::BuffApply: the single caller of AddBuff ('!m_buffs.Find(buffId)')."""
    add_buff = _func_referencing(bv, "!m_buffs.Find(buffId)")
    if add_buff is None:
        return None
    callers = list({c.start: c for c in add_buff.callers}.values())
    return callers[0] if len(callers) == 1 else None


def _data_refs(bv, fn):
    """Non-code addresses referenced by the function's instruction operands."""
    code = [s for s in bv.sections.values() if s.semantics == bn.SectionSemantics.ReadOnlyCodeSectionSemantics]
    in_code = lambda a: any(s.start <= a < s.end for s in code)
    data = set()
    for tokens, _ in fn.instructions:
        for tok in tokens:
            if tok.type in (bn.InstructionTextTokenType.DataSymbolToken,
                            bn.InstructionTextTokenType.PossibleAddressToken,
                            bn.InstructionTextTokenType.CodeRelativeAddressToken) and                bv.start <= tok.value < bv.end and not in_code(tok.value):
                data.add(tok.value)
    return data


def contexts_tls_index(bv):
    """The global holding the TLS slot index of the context table.

    Contexts_GetThreadLocal is a tiny leaf callee of BuffApply with thousands of callers
    whose only data reference is that global (mov ecx, [rip+idx]; mov rax, gs:[0x58]; ...)."""
    handler = buff_apply_handler(bv)
    if handler is None:
        return None
    hits = []
    for callee in {c.start: c for c in handler.callees}.values():   # callees list repeats per call site
        if callee.total_bytes > 40 or len(callee.callers) < 1000:
            continue
        data = _data_refs(bv, callee)
        if len(data) == 1:
            hits.append((callee.start, data.pop()))
    return hits[0] if len(hits) == 1 else None


TARGETS = {
    "contexts_tls_index": contexts_tls_index,
}
