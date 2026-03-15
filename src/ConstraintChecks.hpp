// ConstraintChecks.hpp
// Pure, standalone inline functions for single-constraint evaluation.
//
// Follows the same pattern as TimeWindow.hpp — no EuroScope, no Windows
// dependencies, no class state.  Each function takes a single RapidJSON
// constraint object (one element of the "constraints" array from the API)
// plus the relevant flight-plan parameter(s) and returns bool.
//
// The CVFPCPlugin check* methods in analyzeFP.cpp are thin loops that
// iterate the constraints array and call these functions per element.
//
// Day/time handling lives in TimeWindow.hpp (checkTimeWindow).
//
// Usage in tests:
//   #include "ConstraintChecks.hpp"
//   using namespace ConstraintChecks;

#pragma once

#include "rapidjson/document.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace ConstraintChecks {

using namespace rapidjson;

// ── Local constants (mirrors Constant.hpp, no stdafx.h dependency) ───────────

static const std::string CC_WILDCARD        = "*";
static const std::string CC_EVEN_DIRECTION  = "EVEN";
static const std::string CC_ODD_DIRECTION   = "ODD";
static const int         CC_RVSM_UPPER      = 41000;

// ── Internal helpers ──────────────────────────────────────────────────────────

inline std::vector<std::string> cc_split(const std::string& s, char delim)
{
    std::vector<std::string> elems;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim))
        elems.push_back(item);
    return elems;
}

// Prefix-match: returns the matching prefix if 'icao' starts with any element
// of 'arr', else empty string.  Used for ICAO family codes ("EG" matches "EGLL").
inline std::string cc_destPrefixMatch(const Value& arr, const std::string& icao)
{
    for (SizeType i = 0; i < arr.Size(); i++) {
        const char* prefix = arr[i].GetString();
        if (icao.rfind(prefix, 0) != std::string::npos)
            return prefix;
    }
    return "";
}

// Exact string match in a JSON string array.
inline bool cc_arrayHas(const Value& arr, const std::string& s)
{
    for (SizeType i = 0; i < arr.Size(); i++)
        if (arr[i].GetString() == s) return true;
    return false;
}

// Returns true if any route variant in the JSON 'valid' array is a sequential
// prefix of 'rte'.  Wildcard ("*") matches any single token.
inline bool cc_routeHas(const std::vector<std::string>& rte, const Value& valid)
{
    for (SizeType i = 0; i < valid.Size(); i++) {
        std::string r = valid[i].GetString();
        if (r == CC_WILDCARD) return true;

        std::vector<std::string> pattern = cc_split(r, ' ');
        for (auto& tok : pattern)
            std::transform(tok.begin(), tok.end(), tok.begin(), ::toupper);

        if (pattern.size() > rte.size()) continue;

        bool ok = true;
        for (SizeType j = 0; j < static_cast<SizeType>(pattern.size()); j++) {
            if (pattern[j] != CC_WILDCARD && pattern[j] != rte[j]) {
                ok = false;
                break;
            }
        }
        if (ok) return true;
    }
    return false;
}

// ── Single-constraint checkers ────────────────────────────────────────────────

/// Returns false if 'destination' is in nodests, or if dests is present and
/// destination is NOT in it.
inline bool checkOneDestination(const Value& c, const std::string& destination)
{
    if (c.HasMember("nodests") && c["nodests"].IsArray() && c["nodests"].Size())
        if (cc_destPrefixMatch(c["nodests"], destination).size()) return false;

    if (c.HasMember("dests") && c["dests"].IsArray() && c["dests"].Size())
        if (!cc_destPrefixMatch(c["dests"], destination).size()) return false;

    return true;
}

/// Returns false if the flight plan's exit-point list does not intersect the
/// constraint's 'points' array, or if a 'nopoints' entry is matched.
inline bool checkOneExitPoint(const Value& c, const std::vector<std::string>& points)
{
    if (c.HasMember("points") && c["points"].IsArray() && c["points"].Size()) {
        bool found = false;
        for (const auto& p : points)
            if (cc_arrayHas(c["points"], p)) { found = true; break; }
        if (!found) return false;
    }

    if (c.HasMember("nopoints") && c["nopoints"].IsArray() && c["nopoints"].Size())
        for (const auto& p : points)
            if (cc_arrayHas(c["nopoints"], p)) return false;

    return true;
}

/// Returns false if no required-route variant matches, or if a noroute match
/// is found.
inline bool checkOneRoute(const Value& c, const std::vector<std::string>& route)
{
    if (c.HasMember("route") && c["route"].IsArray() && c["route"].Size())
        if (!cc_routeHas(route, c["route"])) return false;

    if (c.HasMember("noroute") && c["noroute"].IsArray() && c["noroute"].Size())
        if (cc_routeHas(route, c["noroute"])) return false;

    return true;
}

/// Returns false if the filed level (RFL in feet, e.g. FL360 = 36000) falls
/// outside [min, max].  min/max of 0 (or absent) means "no limit".
inline bool checkOneMinMax(const Value& c, int rfl)
{
    if (c.HasMember("min") && c["min"].GetInt() > 0 && (rfl / 100) < c["min"].GetInt())
        return false;
    if (c.HasMember("max") && c["max"].GetInt() > 0 && (rfl / 100) > c["max"].GetInt())
        return false;
    return true;
}

/// Returns false if RFL does not satisfy the even/odd direction requirement.
/// No "dir" field means any level is acceptable.
inline bool checkOneDirection(const Value& c, int rfl)
{
    if (!c.HasMember("dir") || !c["dir"].IsString()) return true;

    std::string dir = c["dir"].GetString();
    std::transform(dir.begin(), dir.end(), dir.begin(), ::toupper);

    if (dir == CC_EVEN_DIRECTION) {
        if (rfl > CC_RVSM_UPPER) return ((rfl - CC_RVSM_UPPER) / 1000) % 4 == 2;
        return (rfl / 1000) % 2 == 0;
    }
    if (dir == CC_ODD_DIRECTION) {
        if (rfl > CC_RVSM_UPPER) return ((rfl - CC_RVSM_UPPER) / 1000) % 4 == 0;
        return (rfl / 1000) % 2 == 1;
    }
    return true;
}

/// Returns false (banned) if any alert has ban=true.
/// Sets warn=true if any alert has warn=true.
inline bool checkOneAlerts(const Value& c, bool& warn)
{
    if (!c.HasMember("alerts") || !c["alerts"].IsArray()) return true;

    bool res = true;
    for (SizeType j = 0; j < c["alerts"].Size(); j++) {
        if (c["alerts"][j].HasMember("ban")  && c["alerts"][j]["ban"].GetBool())  res  = false;
        if (c["alerts"][j].HasMember("warn") && c["alerts"][j]["warn"].GetBool()) warn = true;
    }
    return res;
}

} // namespace ConstraintChecks
