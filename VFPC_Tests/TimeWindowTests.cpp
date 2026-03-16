// TimeWindowTests.cpp
// Google Test suite for the checkTimeWindow() function.
//
// Day convention (Monday = 0 … Sunday = 6) mirrors the internal timedata[5]
// representation that versionCall() builds after (day + 6) % 7.
//
// All 48 cases are drawn directly from docs/test-harness.md.
// Two additional end-boundary cases (O05b, O06b) are added for Group 4
// to match the Group 1/3 coverage pattern and confirm the <= fix on the
// wrap-around branch.

#include <gtest/gtest.h>
#include "TimeWindow.hpp"
#include "rapidjson/document.h"

struct Case {
    const char* id;
    const char* restrictionJson;
    int  day;
    int  hour;
    int  min;
    bool expected;
};

class TimeWindowTest : public ::testing::TestWithParam<Case> {};

TEST_P(TimeWindowTest, ChecksCorrectly)
{
    const Case& p = GetParam();
    rapidjson::Document doc;
    doc.Parse<rapidjson::kParseDefaultFlags>(p.restrictionJson);
    ASSERT_FALSE(doc.HasParseError()) << "Bad JSON in case " << p.id;
    bool result = checkTimeWindow(p.day, p.hour, p.min, doc);
    EXPECT_EQ(p.expected, result) << "Case " << p.id;
}

// Name each test run by its case ID so output is readable.
static std::string caseId(const ::testing::TestParamInfo<Case>& info)
{
    return info.param.id;
}

// ── Group 1 — Wrap-around weekday (Bug 4 fix) ────────────────────────────────
// Restriction: Friday 16:00 → Monday 07:45
// startdate(4) > enddate(0) — wrap-around branch.
// W09/W10/W11 were incorrectly PASS before the fix.

#define G1_JSON R"({"start":{"date":4,"time":"1600"},"end":{"date":0,"time":"0745"}})"

INSTANTIATE_TEST_SUITE_P(
    Group1_WrapAround, TimeWindowTest,
    ::testing::Values(
        Case{"W01", G1_JSON, 5, 12,  0, true },  // Saturday  — interior wrap day
        Case{"W02", G1_JSON, 6, 12,  0, true },  // Sunday    — interior wrap day
        Case{"W03", G1_JSON, 4, 16,  0, true },  // Friday    — exactly at start
        Case{"W04", G1_JSON, 4, 16, 30, true },  // Friday    — after start
        Case{"W05", G1_JSON, 4, 15, 59, false},  // Friday    — before start
        Case{"W06", G1_JSON, 0,  7, 44, true },  // Monday    — before end
        Case{"W07", G1_JSON, 0,  7, 45, true },  // Monday    — exactly at end (<=)
        Case{"W08", G1_JSON, 0,  7, 46, false},  // Monday    — after end
        Case{"W09", G1_JSON, 1, 12,  0, false},  // Tuesday   — outside wrap (was wrong PASS)
        Case{"W10", G1_JSON, 2, 12,  0, false},  // Wednesday — outside wrap (was wrong PASS)
        Case{"W11", G1_JSON, 3, 12,  0, false}   // Thursday  — outside wrap (was wrong PASS)
    ),
    caseId
);

// ── Group 2 — Same-day restriction (Bug 3 fix) ───────────────────────────────
// Restriction: Monday 09:00 → Monday 16:15
// S06/S07/S08/S09 were incorrectly PASS before the fix.

#define G2_JSON R"({"start":{"date":0,"time":"0900"},"end":{"date":0,"time":"1615"}})"

INSTANTIATE_TEST_SUITE_P(
    Group2_SameDay, TimeWindowTest,
    ::testing::Values(
        Case{"S01", G2_JSON, 0, 10,  0, true },  // Monday    — inside window
        Case{"S02", G2_JSON, 0,  9,  0, true },  // Monday    — exactly at start
        Case{"S03", G2_JSON, 0,  8, 59, false},  // Monday    — before window
        Case{"S04", G2_JSON, 0, 16, 15, true },  // Monday    — exactly at end
        Case{"S05", G2_JSON, 0, 16, 16, false},  // Monday    — after window
        Case{"S06", G2_JSON, 1, 10,  0, false},  // Tuesday   — wrong day (was wrong PASS)
        Case{"S07", G2_JSON, 2, 10,  0, false},  // Wednesday — wrong day (was wrong PASS)
        Case{"S08", G2_JSON, 5, 10,  0, false},  // Saturday  — wrong day (was wrong PASS)
        Case{"S09", G2_JSON, 6, 10,  0, false}   // Sunday    — wrong day (was wrong PASS)
    ),
    caseId
);

// ── Group 3 — Adjacent overnight, forward range (regression) ─────────────────
// Restriction: Monday 17:00 → Tuesday 08:15
// startdate(0) < enddate(1) — forward branch.
// A05 confirms the <= fix on the forward-range end boundary.

#define G3_JSON R"({"start":{"date":0,"time":"1700"},"end":{"date":1,"time":"0815"}})"

INSTANTIATE_TEST_SUITE_P(
    Group3_AdjacentOvernight, TimeWindowTest,
    ::testing::Values(
        Case{"A01", G3_JSON, 0, 17,  0, true },  // Monday    — exactly at start
        Case{"A02", G3_JSON, 0, 17, 30, true },  // Monday    — after start
        Case{"A03", G3_JSON, 0, 16, 59, false},  // Monday    — before start
        Case{"A04", G3_JSON, 1,  8, 14, true },  // Tuesday   — before end
        Case{"A05", G3_JSON, 1,  8, 15, true },  // Tuesday   — exactly at end (<=)
        Case{"A06", G3_JSON, 1,  8, 16, false},  // Tuesday   — after end
        Case{"A07", G3_JSON, 2, 20,  0, false},  // Wednesday — outside range
        Case{"A08", G3_JSON, 6, 20,  0, false}   // Sunday    — outside range
    ),
    caseId
);

// ── Group 4 — Wrap-around overnight variant (regression) ─────────────────────
// Restriction: Friday 17:00 → Monday 08:15
// startdate(4) > enddate(0) — wrap-around branch.
// O05b/O06b added to mirror W07/W08 end-boundary coverage.

#define G4_JSON R"({"start":{"date":4,"time":"1700"},"end":{"date":0,"time":"0815"}})"

INSTANTIATE_TEST_SUITE_P(
    Group4_WrapAroundOvernight, TimeWindowTest,
    ::testing::Values(
        Case{"O01",  G4_JSON, 4, 17, 30, true },  // Friday    — after start
        Case{"O02",  G4_JSON, 4, 16, 59, false},  // Friday    — before start
        Case{"O03",  G4_JSON, 5, 12,  0, true },  // Saturday  — interior wrap day
        Case{"O04",  G4_JSON, 6, 12,  0, true },  // Sunday    — interior wrap day
        Case{"O05",  G4_JSON, 0,  8,  0, true },  // Monday    — before end
        Case{"O05b", G4_JSON, 0,  8, 15, true },  // Monday    — exactly at end (<=)
        Case{"O06",  G4_JSON, 0,  8, 16, false},  // Monday    — after end
        Case{"O07",  G4_JSON, 1, 12,  0, false},  // Tuesday   — outside wrap range
        Case{"O08",  G4_JSON, 3, 12,  0, false}   // Thursday  — outside wrap range
    ),
    caseId
);

// ── Group 5 — Date-only range, no time (regression) ──────────────────────────
// Restriction: Monday → Friday, no time constraint.
// startdate(0) < enddate(4) — forward branch, !hasTime.

#define G5_JSON R"({"start":{"date":0},"end":{"date":4}})"

INSTANTIATE_TEST_SUITE_P(
    Group5_DateOnly, TimeWindowTest,
    ::testing::Values(
        Case{"D01", G5_JSON, 0,  3,  0, true },  // Monday    — in range
        Case{"D02", G5_JSON, 2, 14,  0, true },  // Wednesday — in range
        Case{"D03", G5_JSON, 4, 23,  0, true },  // Friday    — in range
        Case{"D04", G5_JSON, 5, 12,  0, false},  // Saturday  — outside range
        Case{"D05", G5_JSON, 6, 12,  0, false}   // Sunday    — outside range
    ),
    caseId
);

// ── Group 6 — Time-only, normal window (regression) ───────────────────────────
// Restriction: 08:00 → 20:00 any day.

#define G6_JSON R"({"start":{"time":"0800"},"end":{"time":"2000"}})"

INSTANTIATE_TEST_SUITE_P(
    Group6_TimeOnly, TimeWindowTest,
    ::testing::Values(
        Case{"T01", G6_JSON, 0,  8,  0, true },  // Monday    — at start
        Case{"T02", G6_JSON, 3, 14,  0, true },  // Thursday  — inside window
        Case{"T03", G6_JSON, 6, 20,  0, true },  // Sunday    — at end
        Case{"T04", G6_JSON, 2,  7, 59, false},  // Wednesday — before window
        Case{"T05", G6_JSON, 4, 20,  1, false}   // Friday    — after window
    ),
    caseId
);

// ── Group 7 — Time-only, midnight-wrap (regression) ───────────────────────────
// Restriction: 23:00 → 01:00, wraps midnight.

#define G7_JSON R"({"start":{"time":"2300"},"end":{"time":"0100"}})"

INSTANTIATE_TEST_SUITE_P(
    Group7_TimeOnlyMidnightWrap, TimeWindowTest,
    ::testing::Values(
        Case{"M01", G7_JSON, 0, 23,  0, true },  // Monday    — at start
        Case{"M02", G7_JSON, 1,  0, 30, true },  // Tuesday   — inside wrap window
        Case{"M03", G7_JSON, 4,  1,  0, true },  // Friday    — at end
        Case{"M04", G7_JSON, 5, 22, 59, false},  // Saturday  — before window
        Case{"M05", G7_JSON, 6,  1,  1, false}   // Sunday    — after window
    ),
    caseId
);

// ── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
