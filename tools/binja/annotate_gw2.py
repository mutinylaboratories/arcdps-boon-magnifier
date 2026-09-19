"""Write the combat buff-bar findings into the Gw2-64 Binary Ninja database.

    python annotate_gw2.py <project.bnpr|db.bndb>

Names functions, defines the structures, types the key functions and adds comments,
then saves the database in place. Close the database in the Binary Ninja GUI first.
Addresses are for the build with PE timestamp noted below; rerun discovery for others.
"""
import sys

import binaryninja as bn
from find_strings import open_view

BUILD_NOTE = "Gw2-64.exe analysed 2026-09-19 (arcdps_boon_magnifier RE notes)"

TYPES = r"""
// Per-buff-instance object, 0x78 bytes, allocated by CmbtCliBuffBar::AddBuff.
struct CmbtCliBuff {
    void* vtable;
    uint32_t skillId;        // 0x08 copied from skillDef+0x28 by the ctor (buff skill id, e.g. Stability = 1122)
    uint32_t pad_0c;
    void* skillDef;          // 0x10 Content skill definition (+0x58 == 1 && +0x60 != 0 <=> GetBuff())
    uint32_t buffId;         // 0x18 per-instance id, key of CmbtCliBuffBar::m_buffs (same id arcdps reports as trackable id)
    uint32_t field_1c;
    void* buffBar;           // 0x20 owning CmbtCliBuffBar
    void* sourceAgent;       // 0x28 agent that applied the buff (may be null)
    void* sourceListPrev;    // 0x30 intrusive list node in the source's applied-buff list
    void* sourceListNext;    // 0x38
    uint32_t durationMs;     // 0x40 applied duration; updated by Deactivate/SetDuration handlers
    uint32_t startTimeMs;    // 0x44 game clock (GetTimeMs) when (re)activated; remaining = start + duration - now
    uint32_t field_48;       // ctor arg7 (message +0x16, u8)
    uint32_t isActive;       // 0x4c 1 while the timer runs (duration-stacked buffs queue with isActive = 0)
    void* listA[2];          // 0x50 self-linked list heads
    void* listB[2];          // 0x60
    uint64_t field_70;
};

// Slot of the open-addressing hash map m_buffs (Arena Core Collections).
struct CmbtCliBuffMapEntry {
    uint32_t key;            // buffId
    uint32_t pad;
    struct CmbtCliBuff* buff;// null = empty slot
    uint64_t occupied;       // non-zero once used (AddBuff grows when (count+1)*3 >= capacity*2)
};

// Combat-client buff container of one combatant (returned by combatant->vtable[8]()).
struct CmbtCliBuffBar {
    void* vtable;
    uint8_t pad_08[0x18];
    uint32_t buffsCapacity;                 // 0x20 m_buffs table size
    uint32_t buffsCount;                    // 0x24 live entries
    struct CmbtCliBuffMapEntry* buffsTable; // 0x28
    uint8_t pad_30[0x08];
    uint8_t sourceMap[0x40];                // 0x38 second map keyed by source (m_fullList / slot lists)
    uint8_t pad_78[0x38];
    void* listener;                         // 0xb0 vtable +0x120 buff activated, +0x130 deactivated, +0x138 duration changed
    uint8_t pad_b8[0x20];
    void* statusArray;                      // 0xd8 sorted array of 12-byte {int32 a; int32 b; uint32 buffId}
    uint32_t pad_e0;
    uint32_t statusCount;                   // 0xe4
};

// Network message payloads handled in Game\Combat\Cli\CmbtCliMsg.cpp (all begin with a 16-bit id).
struct __packed CmbtCliMsgBuffApply {   // handler line 0x30 -> CmbtCliBuffBar::AddBuff
    uint16_t msgId;
    uint32_t targetAgentId;    // 0x02 combatant that receives the buff (compare with own agent id)
    uint32_t sourceAgentId;    // 0x06
    uint32_t skillId;          // 0x0a e.g. 1122 = Stability
    uint32_t buffId;           // 0x0e instance id
    uint32_t durationMs;       // 0x12
    uint8_t  field_16;         // -> CmbtCliBuff+0x48
    uint8_t  isActive;         // 0x17 -> CmbtCliBuff+0x4c
};

struct __packed CmbtCliMsgBuffTimer {   // lines 0x21 (activate), 0x4c (deactivate), 0x5b (set duration)
    uint16_t msgId;
    uint32_t targetAgentId;    // 0x02
    uint32_t buffId;           // 0x06
    uint32_t durationMs;       // 0x0a activate: remaining; deactivate/set: new duration
};

struct __packed CmbtCliMsgBuffRemove {  // line 0x7b: remove one instance
    uint16_t msgId;
    uint32_t targetAgentId;    // 0x02
    uint32_t sourceAgentId;    // 0x06 (unused by the removal itself)
    uint32_t buffId;           // 0x0a
};
"""

# (address, name, comment)
FUNCTIONS = [
    (0x1409b4cd0, "Contexts_GetThreadLocal",
     "Returns the thread-local context table: +0x98 ChCliContext, +0xb8 CmbtCliContext, +0xe0 content defs."),
    (0x1409ddf30, "AssertFail", "Arena assert(expr, file, line). noreturn."),
    (0x1409ce250, "GetTimeMs", "Game clock in ms; CmbtCliBuff::startTimeMs is stamped from this."),
    (0x14101f930, "AgentView_GetAgentById", "Agent lookup by agent id (asserts 'agent' in callers)."),
    (0x140251d60, "HashMap_FindSlot", "Arena hash map slot index for key (used by CmbtCliBuffBar::m_buffs)."),
    (0x1412bde20, "CmbtCliContext_GetCombatantByAgentId",
     "CmbtCliContext.cpp:0x60. agent type 0xa -> gadget path; else ChCliContext->vt[3](agentId) then character->vt[0x108/8]() = GetCombatant."),
    (0x1412bfc60, "CmbtCliBuffBar_ctor", "CmbtCliBuff.cpp:0xd2/0x387. Initialises m_buffs, m_fullList, m_slotListArray, m_slotMask, m_combatant."),
    (0x1412bfaa0, "CmbtCliBuff_ctor",
     "(this, sourceAgent, buffBar, &skillDef, buffId, durationMs, field48, isActive). Sets +0x40 duration, +0x44 = GetTimeMs(), +0x4c isActive."),
    (0x1412c1160, "CmbtCliBuffBar_AddBuff",
     "CmbtCliBuff.cpp:0x23c '!m_buffs.Find(buffId)'. (this, sourceAgentId, &skillDef, buffId, durationMs, f16, isActive). Allocates 0x78-byte CmbtCliBuff, inserts into m_buffs."),
    (0x1412c10a0, "CmbtCliBuffBar_ActivateBuff",
     "CmbtCliBuff.cpp:0x227 '!buffData->isActive'. (this, buffId, remainingMs): startTime = now - (duration - remaining); isActive = 1; listener->vt[0x120]."),
    (0x1412c1640, "CmbtCliBuffBar_DeactivateBuff", "(this, buffId, newDurationMs): duration = arg; isActive = 0; listener->vt[0x130]."),
    (0x1412c16c0, "CmbtCliBuffBar_SetBuffDuration", "(this, buffId, newDurationMs): duration = arg; listener->vt[0x138] unless skill is filtered."),
    (0x1412c18f0, "CmbtCliBuffBar_RemoveBuffById", "(this, sourceAgent, buffId): finds in m_buffs then RemoveBuff."),
    (0x1412c1950, "CmbtCliBuffBar_RemoveBuff", "(this, CmbtCliBuff*): unlinks and destroys one buff instance."),
    (0x1412c1750, "CmbtCliBuffBar_RemoveBuffsByKey", "(this, ?, key): walks the +0x38 map for key and RemoveBuff each match (message line 0x6a)."),
    (0x1412c23b0, "CmbtCliBuffBar_StatusArrayInsert", "Sorted insert of {a, b, buffId} into +0xd8/+0xe4 (message line 0x8c, batched)."),
    (0x1412c2480, "CmbtCliBuffBar_StatusArrayRemoveById", "Removes entries with buffId from +0xd8/+0xe4 (message line 0xa0)."),
    (0x1412c25d0, "CmbtCliMsg_BuffActivate", "CmbtCliMsg.cpp:0x21. msg: CmbtCliMsgBuffTimer. -> ActivateBuff."),
    (0x1412c2640, "CmbtCliMsg_BuffApply",
     "CmbtCliMsg.cpp:0x30. msg: CmbtCliMsgBuffApply. Looks up combatant, skillDef (content->vt[0x70](0x40, skillId)), asserts skillDef->GetBuff(), -> AddBuff. REALTIME HOOK POINT."),
    (0x1412c2750, "CmbtCliMsg_BuffDeactivate", "CmbtCliMsg.cpp:0x4c. msg: CmbtCliMsgBuffTimer. -> DeactivateBuff."),
    (0x1412c27c0, "CmbtCliMsg_BuffSetDuration", "CmbtCliMsg.cpp:0x5b. msg: CmbtCliMsgBuffTimer. -> SetBuffDuration."),
    (0x1412c2830, "CmbtCliMsg_BuffRemoveByKey", "CmbtCliMsg.cpp:0x6a. +0x06 agent, +0x0a key. -> RemoveBuffsByKey."),
    (0x1412c28c0, "CmbtCliMsg_BuffRemove", "CmbtCliMsg.cpp:0x7b. msg: CmbtCliMsgBuffRemove. -> RemoveBuffById. REALTIME HOOK POINT."),
    (0x1412c2950, "CmbtCliMsg_BuffStatusBatch", "CmbtCliMsg.cpp:0x8c. +0x0a count, +0x0b ptr to 8-byte pairs. -> StatusArrayInsert each."),
    (0x1412c29f0, "CmbtCliMsg_BuffStatusRemove", "CmbtCliMsg.cpp:0xa0. -> StatusArrayRemoveById."),
    # Skill bar / recharge (see docs/gw2-buff-internals.md, "Skill bar")
    (0x1411d2990, "ChCliCharacter_CreateSkillbar", "Allocates ChCliSkillbar and stores it at character+0x520."),
    (0x1411f0880, "ChCliSkillbar_ctor", "ChCliSkillbar.cpp. +0x150 = CharSkillbar recharge manager*, +0x1d0 = skillDef*[23]."),
    (0x1411f4690, "ChCliSkillbar_GetSlotSkillDef", "ChCliSkillbar.cpp:0x668 'skillbarSlot < arrsize(m_slotSkillDefs)'. (this, out, slot) -> *(this+0x1d0+slot*8)."),
    (0x1411f4510, "ChCliSkillbar_GetSkillRecharge", "ChCliSkillbar.cpp:0x632 'hasSkillRecharge'. Looks up *(this+0x150) by skillDef."),
    (0x141210d80, "CharSkillbar_FindRecharge", "CharSkillbar.cpp:0x34b. Walks list at mgr+0x48: entry {+8 next, +0x10 leftAtBase, +0x18 skillDef, +0x24 kind}; kind must match."),
    (0x141210bd0, "CharSkillbar_ReadRecharge", "CharSkillbar.cpp. remaining = leftAtBase - (now - mgr+0x24) * mgr+0x30(rate); fills {remaining, skillDef, ...}."),
    (0x1411d3840, "ChCliCharacter_GetCombatant", "vtable +0x108: returns this+0x40 (embedded combatant)."),
    (0x1411b1390, "ChCliContext_GetControlledCharacter", "vtable +0x68: *(this+0x98) if bit 4 of character+0x178 is set."),
    (0x140400c10, "CmbtCliCombatant_GetBuffBar", "vtable +0x40: *(this+0x90)."),
]

SIGNATURES = {
    "CmbtCliBuffBar_AddBuff": "uint64_t f(struct CmbtCliBuffBar* bar, uint32_t sourceAgentId, void** skillDef, uint32_t buffId, uint32_t durationMs, uint32_t field16, uint32_t isActive)",
    "CmbtCliBuffBar_ActivateBuff": "void f(struct CmbtCliBuffBar* bar, uint32_t buffId, uint32_t remainingMs)",
    "CmbtCliBuffBar_DeactivateBuff": "void f(struct CmbtCliBuffBar* bar, uint32_t buffId, uint32_t newDurationMs)",
    "CmbtCliBuffBar_SetBuffDuration": "void f(struct CmbtCliBuffBar* bar, uint32_t buffId, uint32_t newDurationMs)",
    "CmbtCliBuffBar_RemoveBuffById": "void f(struct CmbtCliBuffBar* bar, void* sourceAgent, uint32_t buffId)",
    "CmbtCliBuffBar_RemoveBuff": "void f(struct CmbtCliBuffBar* bar, struct CmbtCliBuff* buff)",
    "CmbtCliMsg_BuffApply": "int64_t f(void* ctx, struct CmbtCliMsgBuffApply* msg)",
    "CmbtCliMsg_BuffActivate": "int64_t f(void* ctx, struct CmbtCliMsgBuffTimer* msg)",
    "CmbtCliMsg_BuffDeactivate": "int64_t f(void* ctx, struct CmbtCliMsgBuffTimer* msg)",
    "CmbtCliMsg_BuffSetDuration": "int64_t f(void* ctx, struct CmbtCliMsgBuffTimer* msg)",
    "CmbtCliMsg_BuffRemove": "int64_t f(void* ctx, struct CmbtCliMsgBuffRemove* msg)",
    "CmbtCliContext_GetCombatantByAgentId": "void* f(void* cmbtCtx, uint32_t agentId)",
    "GetTimeMs": "uint32_t f()",
    "Contexts_GetThreadLocal": "void* f()",
}

CHAIN_COMMENT = (
    "Local player's buff bar: Contexts_GetThreadLocal()+0x98 -> ChCliContext; "
    "ChCliContext->vt[0x68/8]() = GetControlledCharacter; character->vt[0x108/8]() = GetCombatant; "
    "combatant->vt[0x40/8]() = CmbtCliBuffBar. Iterate buffsTable[0..buffsCapacity), buff != null, buff->skillId == 1122."
)


def main():
    path = sys.argv[1]
    bv = open_view(path)
    print("opened %s (%d functions)" % (bv.file.filename, len(bv.functions)))

    types = bv.parse_types_from_string(TYPES)
    for name, t in types.types.items():
        bv.define_user_type(name, t)
    print("defined %d types" % len(types.types))

    renamed = 0
    for addr, name, comment in FUNCTIONS:
        fn = bv.get_function_at(addr)
        if fn is None:
            print("  MISSING function at %#x (%s)" % (addr, name))
            continue
        fn.name = name
        fn.comment = "%s\n[%s]" % (comment, BUILD_NOTE)
        if name in SIGNATURES:
            try:
                fn.type = bv.parse_type_string(SIGNATURES[name])[0]
            except Exception as e:
                print("  signature failed for %s: %s" % (name, e))
        renamed += 1
    print("named %d functions" % renamed)

    apply_fn = bv.get_function_at(0x1412c2640)
    if apply_fn:
        apply_fn.comment = apply_fn.comment + "\n" + CHAIN_COMMENT
    ctx_fn = bv.get_function_at(0x1409b4cd0)
    if ctx_fn:
        ctx_fn.comment = ctx_fn.comment + "\n" + CHAIN_COMMENT

    bv.update_analysis_and_wait()
    ok = bv.file.save_auto_snapshot()
    print("saved:", ok)
    bv.file.close()


if __name__ == "__main__":
    main()
