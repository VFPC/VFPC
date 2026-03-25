// SidApplicability.hpp
// Pure, standalone inline function for testing whether any SID-level restriction
// in a restrictions array is currently applicable to a given flight.
//
// "Applicable" means ALL of the restriction's selectors match:
//   - sidlevel == true          (only SID-level restrictions are considered)
//   - suffix matches, or no suffix selector exists
//   - type matches, or no type selector exists
//   - time window passes, or no time selector exists
//
// This function drives the sidwide fallback in validateSid (analyzeFP.cpp).
// Extracted here so it can be unit-tested without EuroScope dependencies.
//
// Usage in production:
//   #include "SidApplicability.hpp"
//   bool active = anySidLevelRestrictionApplicable(
//       sid_ele["restrictions"], eng, actype, sid_suffix,
//       timedata[5], timedata[3], timedata[4]);
//
// Usage in tests:
//   #include "SidApplicability.hpp"
//   using namespace SidApplicability;

#pragma once

#include "rapidjson/document.h"
#include "TimeWindow.hpp"
#include <string>

namespace SidApplicability {

using namespace rapidjson;

// Returns true if at least one restriction in the array has sidlevel=true and
// all of its selectors (suffix, type, time window) match the current flight.
//
// Returns false if:
//   - the array is empty or absent
//   - every sidlevel=true restriction fails at least one selector
//   - there are no sidlevel=true restrictions at all
//
// Parameters:
//   restrictions  — the "restrictions" JSON array from a SID element
//   eng           — GetEngineType() for the flight (e.g. "J", "T", "P", "E")
//   actype        — GetAircraftType() for the flight (e.g. "B738")
//   sid_suffix    — the SID suffix string (e.g. "1C", "2D", "")
//   currentDay    — current day-of-week (Monday=0 … Sunday=6, per timedata[5])
//   currentHour   — current UTC hour (0-23, per timedata[3])
//   currentMin    — current UTC minute (0-59, per timedata[4])
inline bool anySidLevelRestrictionApplicable(
    const Value& restrictions,
    const std::string& eng,
    const std::string& actype,
    const std::string& sid_suffix,
    int currentDay,
    int currentHour,
    int currentMin)
{
    if (!restrictions.IsArray() || restrictions.Size() == 0) {
        return false;
    }

    for (SizeType i = 0; i < restrictions.Size(); i++) {
        const Value& r = restrictions[i];

        // Only SID-level restrictions are considered here.
        if (!r.HasMember("sidlevel") || !r["sidlevel"].GetBool()) {
            continue;
        }

        bool applicable = true;

        // Suffix selector: absent means applies to all suffixes.
        if (r.HasMember("suffix") && r["suffix"].IsArray() && r["suffix"].Size()) {
            bool suffixFound = false;
            for (SizeType j = 0; j < r["suffix"].Size(); j++) {
                if (r["suffix"][j].IsString()) {
                    std::string sfx = r["suffix"][j].GetString();
                    // Match if sid_suffix ends with the selector value (mirrors arrayContainsEnding).
                    if (sid_suffix.size() >= sfx.size() &&
                        sid_suffix.compare(sid_suffix.size() - sfx.size(), sfx.size(), sfx) == 0) {
                        suffixFound = true;
                        break;
                    }
                }
            }
            if (!suffixFound) {
                applicable = false;
            }
        }

        // Type selector: absent means applies to all types.
        if (applicable && r.HasMember("types") && r["types"].IsArray() && r["types"].Size()) {
            bool typeFound = false;
            for (SizeType j = 0; j < r["types"].Size(); j++) {
                if (r["types"][j].IsString()) {
                    std::string t = r["types"][j].GetString();
                    if (t == eng || t == actype) {
                        typeFound = true;
                        break;
                    }
                }
            }
            if (!typeFound) {
                applicable = false;
            }
        }

        // Time window selector: only active when both start and end are present.
        if (applicable && r.HasMember("start") && r.HasMember("end")) {
            if (!checkTimeWindow(currentDay, currentHour, currentMin, r)) {
                applicable = false;
            }
        }

        if (applicable) {
            return true;
        }
    }

    return false;
}

} // namespace SidApplicability
