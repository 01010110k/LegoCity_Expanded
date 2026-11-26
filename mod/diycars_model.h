// =============================================================================================
//  diycars_model.h - [diycars1] the DIY-car GARAGE (Notes\71). Pure: test\diycars_harness.cpp runs it as is.
//
//  A DIY car is a real vehicle of the game once World\diycars\diycars.py has put it into PATCH.DAT
//  (SmallCars_CraterDIY1 .. 8: AITypes Vehicle.Residential, its own .cd with the player's stats). Its
//  handling needs nothing here: lcudrive's car model reads every car's own definition (model.h
//  DeriveParams: Max Velocity, engine Power Max, MaxAngle, per-wheel TyreGrip and Drive, the body mass),
//  so the stats in car.ini reach the stock handling AND lcudrive's.
//  What this adds is where they wait: [diycars] garage = x, y, z, heading [, label] and
//  cars = model, model, ... -> one [sites] stand per car (sites_model.h's Stand, kind car), in a row
//  from the garage point toward the car's RIGHT, `spacing` apart, all facing `heading`. sites.h then does
//  what it does for every stand (appear while a player is near, parked, map pin, taken = yours, faded
//  when left elsewhere and nobody is near, never removed beside a player) - the machinery the airport
//  planes (Pegasus / ThunderRoc, new vehicles from PATCH.DAT) ride on in play.
//  The stands go into the [sites] slots after the life-spot cars (config.h); they are NOT gated by
//  [spots] cars (sites.h uses cfg.diy_first).
//  Heading convention (dino_model.h / sites_model.h): 0 = +z, 90 = +x; facing (sin h, 0, cos h); the
//  car's right = (cos h, 0, -sin h) [game model space: left = +x, so right = facing x up = this].
// =============================================================================================
#pragma once
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include "sites_model.h"

namespace diym {

enum : int { kMaxCars = 8 };

struct Garage
{
    bool  on;
    float pos[3];
    float heading;
    char  label[32];
};

// garage: 0 = a garage, 1 = empty (none), 2 = malformed (err says why)
inline int ParseGarage(const char* text, Garage& out, char* err, size_t errCap)
{
    auto fail = [&](const char* why) { if (err && errCap) strncpy_s(err, errCap, why, _TRUNCATE); out = Garage{}; return 2; };
    out = Garage{};
    if (err && errCap) err[0] = 0;
    const char* s = text ? text : "";
    while (*s == ' ' || *s == '\t') ++s;
    if (!*s) return 1;
    float v[4];
    for (int i = 0; i < 4; ++i)
    {
        while (*s == ' ' || *s == '\t') ++s;
        char* end = nullptr;
        v[i] = strtof(s, &end);
        if (end == s) return fail("needs: x, y, z, heading [, label]");
        if (!std::isfinite(v[i])) return fail("a number is not finite");
        s = end;
        while (*s == ' ' || *s == '\t') ++s;
        if (i < 3) { if (*s != ',') return fail("needs: x, y, z, heading [, label]"); ++s; }
    }
    if (*s == ',')
    {
        ++s;
        while (*s == ' ' || *s == '\t') ++s;
        strncpy_s(out.label, s, _TRUNCATE);
        size_t n = strlen(out.label);
        while (n && (out.label[n - 1] == ' ' || out.label[n - 1] == '\t' || out.label[n - 1] == '\r')) out.label[--n] = 0;
    }
    else if (*s) return fail("needs: x, y, z, heading [, label]");
    if (std::fabs(v[0]) > 5000.0f || std::fabs(v[2]) > 5000.0f || v[1] < -200.0f || v[1] > 500.0f) return fail("the point is outside the world");
    out.on = true;
    out.pos[0] = v[0]; out.pos[1] = v[1]; out.pos[2] = v[2];
    float h = std::fmod(v[3], 360.0f); if (h < 0.0f) h += 360.0f;
    out.heading = h;
    return 0;
}

// cars: the model names (AITypes' second column), comma separated. Returns how many were taken (<= max);
// *bad = how many were refused (an empty item is skipped silently; a name with a character no model name
// has, or one too long, is refused).
inline int ParseCars(const char* text, char names[][48], int max, int* bad)
{
    int n = 0, b = 0;
    const char* s = text ? text : "";
    while (*s)
    {
        while (*s == ' ' || *s == '\t' || *s == ',') ++s;
        if (!*s) break;
        const char* e = s;
        while (*e && *e != ',') ++e;
        const char* t = e;
        while (t > s && (t[-1] == ' ' || t[-1] == '\t' || t[-1] == '\r')) --t;
        const size_t len = static_cast<size_t>(t - s);
        bool ok = len > 0 && len < 48;
        for (const char* c = s; ok && c < t; ++c)
            if (!((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_')) ok = false;
        if (ok && n < max) { memcpy(names[n], s, len); names[n][len] = 0; ++n; }
        else ++b;
        s = e;
    }
    if (bad) *bad = b;
    return n;
}

// the stands of a garage: car k at pos + k * spacing * right, facing heading, kind car, label "DIY garage k"
inline int Layout(const Garage& g, const char names[][48], int n, float spacing, sitem::Stand* out, int cap)
{
    if (!g.on) return 0;
    const float kDeg = 3.14159265358979f / 180.0f;
    const float h = g.heading * kDeg;
    const float rx = std::cos(h), rz = -std::sin(h);                 // the car's right
    int m = 0;
    for (int k = 0; k < n && m < cap; ++k)
    {
        sitem::Stand& s = out[m++];
        s = sitem::Stand{};
        s.on = true;
        strncpy_s(s.model, names[k], _TRUNCATE);
        s.kind = sitem::kCar;
        s.pos[0] = g.pos[0] + rx * spacing * k;
        s.pos[1] = g.pos[1];
        s.pos[2] = g.pos[2] + rz * spacing * k;
        s.heading = g.heading;
        _snprintf_s(s.label, sizeof(s.label), _TRUNCATE, "%s %d", g.label[0] ? g.label : "DIY garage", k + 1);
        s.quiet = false;
    }
    return m;
}

} // namespace diym
