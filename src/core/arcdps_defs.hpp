#pragma once
#include <cstdint>

// arcdps plugin ABI, from https://www.deltaconnected.com/arcdps/api/ and the
// evtc README. Only the parts this plugin uses are spelled out.

struct cbtevent {
    uint64_t time;
    uint64_t src_agent;
    uint64_t dst_agent;
    int32_t  value;
    int32_t  buff_dmg;
    uint32_t overstack_value;
    uint32_t skillid;
    uint16_t src_instid;
    uint16_t dst_instid;
    uint16_t src_master_instid;
    uint16_t dst_master_instid;
    uint8_t  iff;
    uint8_t  buff;
    uint8_t  result;
    uint8_t  is_activation;
    uint8_t  is_buffremove;
    uint8_t  is_ninety;
    uint8_t  is_fifty;
    uint8_t  is_moving;
    uint8_t  is_statechange;
    uint8_t  is_flanking;
    uint8_t  is_shields;
    uint8_t  is_offcycle;
    uint8_t  pad61;   // pad61-64: uint32 trackable buff instance id on buff events
    uint8_t  pad62;
    uint8_t  pad63;
    uint8_t  pad64;
};
static_assert(sizeof(cbtevent) == 64, "cbtevent must match the arcdps layout");

struct ag {
    const char* name;
    uintptr_t   id;
    uint32_t    prof;
    uint32_t    elite;
    uint32_t    self;
    uint16_t    team;
};

struct arcdps_exports {
    uintptr_t   size;
    uint32_t    sig;
    uint32_t    imguivers;
    const char* out_name;
    const char* out_build;
    void*       wnd_nofilter;
    void*       combat;
    void*       imgui;
    void*       options_end;
    void*       combat_local;
    void*       wnd_filter;
    void*       options_windows;
};

// is_statechange values we care about.
enum : uint8_t {
    CBTS_NONE              = 0,
    CBTS_BUFFINITIAL       = 18,
    CBTS_BUFFACTIVE        = 27,   // was STACKACTIVE
    CBTS_BUFFDEACTIVE      = 28,   // was STACKRESET
    CBTS_MAPCHANGE         = 65,
    CBTS_ANIMATIONSTART    = 67,   // skill activation began (src_agent casts skillid)
    CBTS_ANIMATIONSTOP     = 68,
    CBTS_BUFFAPPLY         = 69,
    CBTS_BUFFCHANGE        = 70,
    CBTS_BUFFREMOVE_SINGLE = 71,
    CBTS_BUFFREMOVE_ALL    = 72,
};

enum : uint8_t {
    CBTB_NONE   = 0,
    CBTB_ALL    = 1,
    CBTB_SINGLE = 2,
    CBTB_MANUAL = 3,
};
