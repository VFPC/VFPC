#pragma once
#include <string>
#include "rapidjson/document.h"

// checkTimeWindow
//
// Returns true when the current time (day/hour/min) falls INSIDE the
// restriction window described by a RapidJSON restriction object, i.e.
// the restriction is ACTIVE and a flight plan should be checked against it.
//
// Day convention (Monday = 0 … Sunday = 6) must match the internal
// timedata[5] representation that versionCall() builds after the
// (day + 6) % 7 conversion from the server's Sunday-0 value.
//
// Both start and end boundaries are INCLUSIVE.
// Time strings are four-digit HHMM, e.g. "1600", "0745".
//
// Returns true (no restriction applies) when the restriction object
// has no "start"/"end" members, or neither contains a "date" nor a "time".
inline bool checkTimeWindow(int currentDay, int currentHour, int currentMin,
                             const rapidjson::Value& restriction)
{
    if (!restriction.HasMember("start") || !restriction.HasMember("end"))
        return true;

    const rapidjson::Value& start = restriction["start"];
    const rapidjson::Value& end   = restriction["end"];

    bool hasDate = start.HasMember("date") && start["date"].IsInt()
                && end.HasMember("date")   && end["date"].IsInt();

    bool hasTime = start.HasMember("time") && start["time"].IsString()
                && end.HasMember("time")   && end["time"].IsString();

    if (!hasDate && !hasTime)
        return true;

    int startdate = 0, enddate = 0;
    int starttime[2] = { 0, 0 };
    int endtime[2]   = { 0, 0 };

    if (hasDate) {
        startdate = start["date"].GetInt();
        enddate   = end["date"].GetInt();
    }

    if (hasTime) {
        std::string s = start["time"].GetString();
        std::string e = end["time"].GetString();
        starttime[0] = std::stoi(s.substr(0, 2));
        starttime[1] = std::stoi(s.substr(2, 2));
        endtime[0]   = std::stoi(e.substr(0, 2));
        endtime[1]   = std::stoi(e.substr(2, 2));
    }

    // Shared boundary helpers (all boundaries inclusive):
    //   afterStart  : current >= start time
    //   beforeEnd   : current <= end time
    auto afterStart = [&]() {
        return currentHour > starttime[0]
            || (currentHour == starttime[0] && currentMin >= starttime[1]);
    };
    auto beforeEnd = [&]() {
        return currentHour < endtime[0]
            || (currentHour == endtime[0] && currentMin <= endtime[1]);
    };

    bool valid = false;

    if (!hasDate) {
        // ── Time-only window ────────────────────────────────────────────────
        // When start >= end the window wraps midnight (e.g. 23:00 → 01:00).
        if (starttime[0] > endtime[0]
         || (starttime[0] == endtime[0] && starttime[1] >= endtime[1])) {
            // Midnight-wrapping: valid if >= start OR <= end
            if (afterStart() || beforeEnd())
                valid = true;
        } else {
            // Normal same-day window: valid if >= start AND <= end
            if (afterStart() && beforeEnd())
                valid = true;
        }

    } else if (startdate == enddate) {
        // ── Same weekday ────────────────────────────────────────────────────
        if (currentDay == startdate) {
            if (!hasTime || (afterStart() && beforeEnd()))
                valid = true;
        }

    } else if (startdate < enddate) {
        // ── Forward range (no week wrap, e.g. Monday → Tuesday) ─────────────
        if (currentDay > startdate && currentDay < enddate) {
            valid = true;                          // interior day: always valid
        } else if (currentDay == startdate) {
            if (!hasTime || afterStart())
                valid = true;
        } else if (currentDay == enddate) {
            if (!hasTime || beforeEnd())
                valid = true;
        }

    } else {
        // ── Wrap-around range (startdate > enddate, e.g. Friday → Monday) ───
        if (currentDay == startdate) {
            if (!hasTime || afterStart())
                valid = true;
        } else if (currentDay == enddate) {
            if (!hasTime || beforeEnd())
                valid = true;
        } else if (currentDay > startdate || currentDay < enddate) {
            valid = true;                          // interior wrap day: always valid
        }
    }

    return valid;
}
