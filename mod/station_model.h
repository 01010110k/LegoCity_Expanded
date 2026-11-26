// =============================================================================================
//  station_model.h - "Return to Police Station" = outside the station, only the player who asked
//  (2026-11-24, "station1"). The pure part: no engine, no Windows - test\station_harness.cpp runs it.
//
//  WHY (co-op): "Return to Police Station" returns the player to outside the police station
//  instead of inside it, so the other player is not disrupted and there is no long loading
//  screen.
//
//  THE STOCK ACTION [P exe bytes, Notes\69]: the pause menu (menu 2, update 0x14080AB60) shows
//  QUIT_TO_HUB "Return to Police Station" when game+0x335D != 0; picking it sets the mode int
//  *(int*)0x1417EC104 = 1 and pushes the confirm menu 0x24; its "Yes" (menu 0x24's update
//  0x1400412F0, cursor 0, accept) tail-jumps `jmp 0x14003ACA0` (rcx = the menu, edx = the mode,
//  r8 = 0) at 0x140041333, and 0x14003ACA0(_, 1, _) = GoToLevel(Police_Station_Lobby): a level
//  change, the whole game's - both players, a loading screen.
//
//  WHAT CHANGES: that one jmp goes to lcudrive (station.h). Decide() below says what happens;
//  anything but vOutside / vBackrooms / vBusy runs the game's own 0x14003ACA0 untouched.
//  vOutside = only that player is moved (fade, out of the vehicle, the game's Character_Teleport
//  recipe - busted_model.h's sequence, the same one the busted-outside move uses), no level change.
// =============================================================================================
#pragma once
#include <cstdint>
#include <cstring>

namespace stn {

enum Verdict : uint8_t
{
    vStock,         // the game's own 0x14003ACA0: the level change, both players
    vOutside,       // only `who` is moved to the pavement outside the station
    vBackrooms,     // `who` is in the Backrooms: the Backrooms' own way out (its menu Leave) for them
    vBusy,          // `who` is on the way already (our move, or the busted move): just resume
};

enum Why : uint8_t
{
    wNone,          // vOutside / vBackrooms
    wNotStation,    // the confirm's mode is not 1 (0 = "Return to LEGO City" from a level: not ours)
    wSwitchedStock, // [general] station_return = stock
    wModOff,        // `[` (stock for the session), unhooked, or the station code faulted
    wMission,       // a story mission (wd::MissionRunning; the game hides the row then anyway)
    wNotCity,       // the current level is not Lego_City (the spot is a city spot)
    wWhoUnknown,    // two players and the game does not say who paused
    wNoPlayer,      // nobody to move
    wBackroomsOff,  // in the Backrooms but its code is off (fault): the stock way out still works
    wMoving,        // vBusy: our move is running for them
    wBusted,        // vBusy: they are being busted (the busted move takes them outside the station)
    wBrTransit,     // vBusy: the Backrooms are moving them already (the shaft / surfacing): theirs to finish
};

struct Ask
{
    int  mode = 1;              // the confirm's mode int (edx at the jmp)
    bool outside = true;        // [general] station_return = outside
    bool modOn = true;          // lcudrive on: not `[`, not unhooked, the station code alive
    bool mission = false;       // wd::MissionRunning()
    bool inCity = true;         // the current level's name is Lego_City
    int  pauser = 0;            // *(int*)0x1411754F0: 0 / 1, -1 = either pad
    bool present[2] = { true, false };
    bool inBackrooms[2] = { false, false };   // br::Inside: in the maze or its arrival fall (PS_IN / PS_FALL)
    bool brTransit[2] = { false, false };     // any other Backrooms state but PS_OUT (the shaft, surfacing)
    bool backroomsOk = true;    // the Backrooms code is alive
    bool busted[2] = { false, false };
    bool moving[2] = { false, false };
};

struct Answer { Verdict v; Why why; int who; };

// Who asked: the pausing player when the game says (and they are there); else the only player in
// the game; two players and no answer = nobody (-1).
inline int Who(const Ask& a)
{
    if ((a.pauser == 0 || a.pauser == 1) && a.present[a.pauser]) return a.pauser;
    const int n = (a.present[0] ? 1 : 0) + (a.present[1] ? 1 : 0);
    if (n == 1) return a.present[0] ? 0 : 1;
    return -1;
}

inline Answer Decide(const Ask& a)
{
    if (a.mode != 1)  return { vStock, wNotStation, -1 };
    if (!a.outside)   return { vStock, wSwitchedStock, -1 };
    if (!a.modOn)     return { vStock, wModOff, -1 };
    if (a.mission)    return { vStock, wMission, -1 };
    if (!a.inCity)    return { vStock, wNotCity, -1 };
    const int who = Who(a);
    if (who < 0)      return { vStock, (a.present[0] || a.present[1]) ? wWhoUnknown : wNoPlayer, -1 };
    if (a.moving[who]) return { vBusy, wMoving, who };
    if (a.busted[who]) return { vBusy, wBusted, who };
    if (a.inBackrooms[who])
        return a.backroomsOk ? Answer{ vBackrooms, wNone, who } : Answer{ vStock, wBackroomsOff, -1 };
    if (a.brTransit[who] && a.backroomsOk) return { vBusy, wBrTransit, who };
    return { vOutside, wNone, who };
}

inline const char* WhyText(Why w)
{
    switch (w)
    {
    case wNotStation:    return "the confirm is not the police-station one (mode != 1)";
    case wSwitchedStock: return "[general] station_return = stock";
    case wModOff:        return "lcudrive is stock right now ([ / unhooked / the station code faulted)";
    case wMission:       return "a story mission is on";
    case wNotCity:       return "not in LEGO City (the spot is a city spot)";
    case wWhoUnknown:    return "two players and the game does not say who paused";
    case wNoPlayer:      return "no player character";
    case wBackroomsOff:  return "in the Backrooms, and the Backrooms code is off";
    case wMoving:        return "already on the way";
    case wBusted:        return "being busted (the busted move takes them outside the station)";
    case wBrTransit:     return "the Backrooms are moving them already (shaft / surfacing)";
    default:             return "";
    }
}

// [general] station_return: "outside" (default) or "stock". Also 1/0, on/off, game. Anything else
// (a typo) = the default, outside. Returns true = outside.
inline bool ParseOutside(const char* s)
{
    if (!s) return true;
    while (*s == ' ' || *s == '\t') ++s;
    char w[16] = {}; int n = 0;
    for (; *s && *s != ' ' && *s != '\t' && n < 15; ++s, ++n) w[n] = (*s >= 'A' && *s <= 'Z') ? static_cast<char>(*s + 32) : *s;
    if (!std::strcmp(w, "stock") || !std::strcmp(w, "0") || !std::strcmp(w, "off") || !std::strcmp(w, "game") ||
        !std::strcmp(w, "inside") || !std::strcmp(w, "lobby"))
        return false;
    return true;
}

// ---------------------------------------------------------------------------------------------
//  The patch site, checked before anything is written [P exe bytes, exe sha 624FED07...]:
//    0x140041328  8B 15 D6 AD 7A 01     mov edx, [0x1417EC104]     ; the mode int
//    0x14004132E  48 83 C4 20           add rsp, 0x20
//    0x140041332  5B                    pop rbx
//    0x140041333  E9 68 99 FF FF        jmp 0x14003ACA0            ; <- the rel32 we retarget
//  and the callee's prologue (0x14003ACA0), and menu 0x24's update slot = 0x1400412F0.
// ---------------------------------------------------------------------------------------------
static const uintptr_t kRvaCtx = 0x41328, kRvaSite = 0x41333, kRvaGo = 0x3ACA0;
static const uint8_t kCtx[11] = { 0x8B, 0x15, 0xD6, 0xAD, 0x7A, 0x01, 0x48, 0x83, 0xC4, 0x20, 0x5B };
static const uint8_t kGoPrologue[15] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x30 };

// b = the 16 bytes at kRvaCtx as they are now. 0 = ours to patch (stock bytes); 1 = already ours
// (the jmp resolves elsewhere: another mod, or a stale patch) = refuse; 2 = not the expected code.
inline int SiteState(const uint8_t b[16])
{
    if (std::memcmp(b, kCtx, sizeof(kCtx)) != 0) return 2;
    if (b[11] != 0xE9) return 2;
    int32_t rel; std::memcpy(&rel, b + 12, 4);
    const int64_t tgt = static_cast<int64_t>(kRvaSite) + 5 + rel;
    return tgt == static_cast<int64_t>(kRvaGo) ? 0 : 1;
}
inline bool GoPrologueOk(const uint8_t b[15]) { return std::memcmp(b, kGoPrologue, sizeof(kGoPrologue)) == 0; }

// The current level's name (the game's own lookup, 0x1407F1D7D: [[*(0x141C6A768)+0x180]+0x50+0x18]).
inline bool IsCity(const char* name)
{
    if (!name) return false;
    const char* c = "lego_city";
    for (int i = 0; ; ++i)
    {
        char x = name[i]; if (x >= 'A' && x <= 'Z') x = static_cast<char>(x + 32);
        if (x != c[i]) return false;
        if (!x) return true;
    }
}

} // namespace stn
