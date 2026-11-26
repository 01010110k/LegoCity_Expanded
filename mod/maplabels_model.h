// =============================================================================================
//  maplabels_model.h - [labels1] the pure part of maplabels.h (no engine access; tested by
//  test\maplabels_harness.cpp). Notes 59 section 6.
//
//  Classify(cellName, x, y, z) -> a new place's index into mlt::kDistricts, or -1 = leave the
//  game's own location line alone. Rules (data in maplabels_table.h, made from places.py):
//   1. a cell named PREFIX_AABB (PREFIX one of kCellPrefixes, '_', exactly 4 digits, end;
//      case-insensitive): its district; a "-1" prefix (BackRooms) below kSuppressBelowY -> -1.
//   2. a cell of one of the game's 23 districts (the exe's own matching, 0x1400CAD90: the name
//      minus "_AABB" equals a table name, or NY_Monaco_Tunnel*, or NY_Uptown_1803..1806) -> -1.
//   3. otherwise (no cell, Water_, Mountains_, ...): the first side box holding (x, z), else -1.
//  FindPin(name, groups) -> the pin whose name equals `name` (case-insensitive) in an enabled
//  group, else nullptr.
// =============================================================================================
#pragma once
#include <cstring>
#include <cctype>
#include "maplabels_table.h"

namespace mlm {

// the exe's district table (0x140C409E0, 23 x 0x40; index 22 UNASSIGNED is never a cell prefix) [P]
static const char* const kStockDistricts[22] = {
    "SF_Residential", "SF_Docks", "WI_Farmlands", "WI_Forest", "BR_Brooklyn_Bridge", "BR_Golden_Gate",
    "BR_Twin_Bridge", "IS_Liberty_Island", "IS_Prison_Island", "IS_Space_Centre", "NY_Airport", "NY_Beach",
    "NY_Canal", "NY_Central_Park", "NY_Chinatown", "NY_Downtown", "NY_Downtown_East", "NY_Downtown_West",
    "NY_Little_Italy", "NY_Times_Square", "NY_Tunnel", "NY_Uptown" };

inline bool IEqN(const char* a, const char* b, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        const int ca = std::tolower(static_cast<unsigned char>(a[i])), cb = std::tolower(static_cast<unsigned char>(b[i]));
        if (ca != cb) return false;
        if (!ca) return true;
    }
    return true;
}
inline bool IEq(const char* a, const char* b)
{
    const size_t la = std::strlen(a), lb = std::strlen(b);
    return la == lb && IEqN(a, b, la);
}

// name == prefix + "_" + 4 digits exactly
inline bool IsGridName(const char* name, const char* prefix)
{
    const size_t lp = std::strlen(prefix), ln = std::strlen(name);
    if (ln != lp + 5 || !IEqN(name, prefix, lp) || name[lp] != '_') return false;
    for (size_t i = lp + 1; i < ln; ++i) if (name[i] < '0' || name[i] > '9') return false;
    return true;
}

// the game's own test (0x1400CAD90): strlen(name) - 5 == strlen(table) and the name starts with it (case-sensitive
// there; case-insensitive here, which only widens "stock" - the safe side)
inline bool IsStockCell(const char* name)
{
    const size_t ln = std::strlen(name);
    if (IEqN(name, "NY_Monaco_Tunnel", 16)) return true;
    for (const char* s : kStockDistricts)
    {
        const size_t ls = std::strlen(s);
        if (ln == ls + 5 && IEqN(name, s, ls)) return true;
    }
    return false;
}

inline int Classify(const char* cellName, float x, float y, float z)
{
    if (cellName && cellName[0])
    {
        for (int i = 0; i < mlt::kCellPrefixCount; ++i)
        {
            const mlt::CellPrefix& p = mlt::kCellPrefixes[i];
            if (!IsGridName(cellName, p.prefix)) continue;
            if (p.district >= 0) return p.district;
            if (y < mlt::kSuppressBelowY) return -1;
            goto boxes;                                   // a suppress prefix above its height: the boxes decide
        }
        if (IsStockCell(cellName)) return -1;
    }
boxes:
    if (!(x == x) || !(z == z)) return -1;                // NaN
    for (int i = 0; i < mlt::kSideBoxCount; ++i)
    {
        const mlt::Box& b = mlt::kSideBoxes[i];
        if (x >= b.x0 && x <= b.x1 && z >= b.z0 && z <= b.z1) return b.district;
    }
    return -1;
}

inline unsigned GroupMask(int districts, int places, int boats)
{
    return (districts ? 1u << mlt::kGroupDistrict : 0u) | (places ? 1u << mlt::kGroupPlace : 0u) |
           (boats ? 1u << mlt::kGroupBoat : 0u);
}

inline int FindPin(const char* name, unsigned groups)
{
    if (!name || std::strncmp(name, "LCUM_", 5) != 0 && !IEqN(name, "LCUM_", 5)) return -1;
    for (int i = 0; i < mlt::kPinCount; ++i)
        if ((groups & (1u << mlt::kPins[i].group)) && IEq(name, mlt::kPins[i].name)) return i;
    return -1;
}

}  // namespace mlm
