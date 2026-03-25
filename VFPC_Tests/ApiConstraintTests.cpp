// ApiConstraintTests.cpp
// Google Test suite for the constraint-evaluation free functions in
// ConstraintChecks.hpp.
//
// All JSON literals are taken from actual UKVFPCAPI responses (out.json from
// the mc-preprocessor branch of New-SRDParser, February 2026 AIRAC).  Each
// integration test references the SRD CSV line number so it can be cross-
// checked against verification_report.json.
//
// Run:
//   Build VFPC_Tests project in Visual Studio (Debug|x64)
//   bin\Debug\tests\VFPC_Tests.exe --gtest_filter=ApiConstraint*
//
// Day convention for time-window tests: Monday=0 … Sunday=6.

#include <gtest/gtest.h>
#include "ConstraintChecks.hpp"
#include "rapidjson/document.h"

using namespace ConstraintChecks;
using namespace rapidjson;

// ── Helpers ───────────────────────────────────────────────────────────────────

// RapidJSON Document has a private copy constructor — it cannot be returned
// by value.  Use this macro to declare + parse inline inside each test.
#define JSON(var, literal) \
    rapidjson::Document var; \
    var.Parse<rapidjson::kParseDefaultFlags>(literal)

static std::vector<std::string> vec(std::initializer_list<const char*> il)
{
    std::vector<std::string> v;
    for (auto s : il) v.push_back(s);
    return v;
}

// ── checkOneDestination ───────────────────────────────────────────────────────

TEST(ApiConstraint_Destination, NoDests_EG_BlocksEGLL)
{
    // nodests=["EG","EI"] — any EG or EI airport is blocked.
    JSON(c, R"({"nodests":["EG","EI"],"dests":[]})");
    EXPECT_FALSE(checkOneDestination(c, "EGLL"));
    EXPECT_FALSE(checkOneDestination(c, "EIDW"));
    EXPECT_TRUE (checkOneDestination(c, "LFPG"));  // not EG/EI
    EXPECT_TRUE (checkOneDestination(c, "KLAX"));
}

TEST(ApiConstraint_Destination, Dests_WithNodests_MeansOverflight)
{
    // When exit=EGPF (4-char airport code), the parser sets BOTH:
    //   dests=["EGPF","EGPK"]  — this constraint covers flights overflying EGPF/EGPK
    //   nodests=["EG","EI"]    — block all domestic EG/EI flights
    //
    // These are evaluated as AND (nodests checked first).
    // EGPF starts with "EG" so it is caught by nodests — even though it is in dests.
    // The plugin evaluates nodests first and never un-sets res; this is correct by design:
    // the constraint is about overflights, not flights landing at EGPF.
    JSON(c, R"({"nodests":["EG","EI"],"dests":["EGPF","EGPK"]})");
    EXPECT_FALSE(checkOneDestination(c, "EGPF"));  // in nodests (EG prefix) → false
    EXPECT_FALSE(checkOneDestination(c, "EGPK"));  // in nodests (EG prefix) → false
    EXPECT_FALSE(checkOneDestination(c, "EGLL"));  // in nodests (EG prefix), not in dests → false
    EXPECT_FALSE(checkOneDestination(c, "EIDW"));  // in nodests (EI prefix), not in dests → false
    EXPECT_FALSE(checkOneDestination(c, "LFPG"));  // not in dests → false
    // No destination passes both checks because dests only lists EG airports,
    // and all EG airports are caught by nodests.  This constraint will never match
    // any destination — it is an impossible combination that the verifier should flag.
}

TEST(ApiConstraint_Destination, NeitherField_AlwaysPasses)
{
    JSON(c, R"({"dir":"even","min":255})");
    EXPECT_TRUE(checkOneDestination(c, "EGLL"));
    EXPECT_TRUE(checkOneDestination(c, "KLAX"));
}

TEST(ApiConstraint_Destination, EmptyArrays_AlwaysPasses)
{
    JSON(c, R"({"nodests":[],"dests":[]})");
    EXPECT_TRUE(checkOneDestination(c, "EGLL"));
}

// ── checkOneExitPoint ─────────────────────────────────────────────────────────

TEST(ApiConstraint_ExitPoint, PointsMatch)
{
    // Constraint requires exit via GOMUP.
    // CSV line 9291 EGKB/GOMUP.
    JSON(c, R"({"points":["GOMUP"]})");
    EXPECT_TRUE (checkOneExitPoint(c, vec({"GOMUP"})));
    EXPECT_FALSE(checkOneExitPoint(c, vec({"BALIX"})));
    EXPECT_FALSE(checkOneExitPoint(c, vec({"ETILO", "BALIX"})));  // neither is GOMUP
}

TEST(ApiConstraint_ExitPoint, MultiplePointsInConstraint)
{
    // Constraint covers several exit points.
    JSON(c, R"({"points":["ADARA","DINIM","LIMRI","TOBOR"]})");
    EXPECT_TRUE (checkOneExitPoint(c, vec({"ADARA"})));
    EXPECT_TRUE (checkOneExitPoint(c, vec({"TOBOR"})));
    EXPECT_FALSE(checkOneExitPoint(c, vec({"GOMUP"})));
}

TEST(ApiConstraint_ExitPoint, NoPointsField_AlwaysPasses)
{
    // No points/nopoints — constraint applies to all exits.
    JSON(c, R"({"dir":"even","min":255})");
    EXPECT_TRUE(checkOneExitPoint(c, vec({"GOMUP"})));
    EXPECT_TRUE(checkOneExitPoint(c, vec({})));
}

TEST(ApiConstraint_ExitPoint, NoPoints_BlocksMatch)
{
    JSON(c, R"({"nopoints":["GOMUP"]})");
    EXPECT_FALSE(checkOneExitPoint(c, vec({"GOMUP"})));
    EXPECT_TRUE (checkOneExitPoint(c, vec({"BALIX"})));
}

// ── checkOneRoute ─────────────────────────────────────────────────────────────

TEST(ApiConstraint_Route, RouteMatch_Prefix)
{
    // Route must start with "EBOTO UN601 RIBEL" (as first three tokens).
    JSON(c, R"({"route":["EBOTO UN601 RIBEL"]})");
    EXPECT_TRUE (checkOneRoute(c, vec({"EBOTO", "UN601", "RIBEL", "LATLO"})));
    EXPECT_TRUE (checkOneRoute(c, vec({"EBOTO", "UN601", "RIBEL"})));
    EXPECT_FALSE(checkOneRoute(c, vec({"EBOTO", "DTY",   "RIBEL"})));
    EXPECT_FALSE(checkOneRoute(c, vec({"BALIX", "UN601", "RIBEL"})));
}

TEST(ApiConstraint_Route, Wildcard_MatchesAny)
{
    JSON(c, R"({"route":["*"]})");
    EXPECT_TRUE(checkOneRoute(c, vec({"EBOTO", "UN601"})));
    EXPECT_TRUE(checkOneRoute(c, vec({})));
}

TEST(ApiConstraint_Route, NoRoute_Blocks)
{
    JSON(c, R"({"noroute":["EBOTO UN601"]})");
    EXPECT_FALSE(checkOneRoute(c, vec({"EBOTO", "UN601", "RIBEL"})));
    EXPECT_TRUE (checkOneRoute(c, vec({"EBOTO", "DTY",   "RIBEL"})));
}

TEST(ApiConstraint_Route, NoRouteField_AlwaysPasses)
{
    JSON(c, R"({"dir":"even"})");
    EXPECT_TRUE(checkOneRoute(c, vec({"EBOTO", "UN601"})));
}

// ── checkOneMinMax ────────────────────────────────────────────────────────────

TEST(ApiConstraint_MinMax, MinBoundary)
{
    // min=255 → FL255 (25500 ft) is the lowest acceptable level.
    JSON(c, R"({"min":255})");
    EXPECT_TRUE (checkOneMinMax(c, 25500));  // exactly at min
    EXPECT_TRUE (checkOneMinMax(c, 36000));  // above min
    EXPECT_FALSE(checkOneMinMax(c, 25400));  // FL254 — below min
}

TEST(ApiConstraint_MinMax, MaxBoundary)
{
    // max=245 → FL245 (24500 ft) is the highest acceptable level.
    JSON(c, R"({"max":245})");
    EXPECT_TRUE (checkOneMinMax(c, 24500));  // exactly at max
    EXPECT_TRUE (checkOneMinMax(c, 10500));  // well below
    EXPECT_FALSE(checkOneMinMax(c, 24600));  // FL246 — above max
}

TEST(ApiConstraint_MinMax, MinAndMax_Band)
{
    // min=105, max=245 → FL105–FL245.
    // CSV line 19488 EGSC/EGPF.
    JSON(c, R"({"min":105,"max":245})");
    EXPECT_TRUE (checkOneMinMax(c, 18000));  // FL180 — in band
    EXPECT_FALSE(checkOneMinMax(c, 10000));  // FL100 — below min
    EXPECT_FALSE(checkOneMinMax(c, 25000));  // FL250 — above max
}

TEST(ApiConstraint_MinMax, ZeroMin_NoLowerLimit)
{
    // min=0 means "no minimum" (not stored in API, but tested for robustness).
    JSON(c, R"({"min":0,"max":245})");
    EXPECT_TRUE(checkOneMinMax(c, 5000));   // FL050 — passes (no floor)
    EXPECT_FALSE(checkOneMinMax(c, 25000)); // FL250 — above max
}

TEST(ApiConstraint_MinMax, NoFields_AlwaysPasses)
{
    JSON(c, R"({"dir":"even"})");
    EXPECT_TRUE(checkOneMinMax(c, 36000));
    EXPECT_TRUE(checkOneMinMax(c, 5000));
}

// ── checkOneDirection ─────────────────────────────────────────────────────────

TEST(ApiConstraint_Direction, Even_RVSM)
{
    // "even" in RVSM: FL280,320,360,400 pass; FL290,310,350 fail.
    JSON(c, R"({"dir":"even"})");
    EXPECT_TRUE (checkOneDirection(c, 36000));  // FL360 even
    EXPECT_TRUE (checkOneDirection(c, 32000));  // FL320 even
    EXPECT_FALSE(checkOneDirection(c, 35000));  // FL350 odd
    EXPECT_FALSE(checkOneDirection(c, 29000));  // FL290 odd
}

TEST(ApiConstraint_Direction, Odd_RVSM)
{
    JSON(c, R"({"dir":"odd"})");
    EXPECT_TRUE (checkOneDirection(c, 35000));
    EXPECT_FALSE(checkOneDirection(c, 36000));
}

TEST(ApiConstraint_Direction, NoDir_AlwaysPasses)
{
    JSON(c, R"({"min":255})");
    EXPECT_TRUE(checkOneDirection(c, 36000));
    EXPECT_TRUE(checkOneDirection(c, 35000));
}

// ── checkOneAlerts ────────────────────────────────────────────────────────────

TEST(ApiConstraint_Alerts, BanAlert_ReturnsFalse)
{
    // Note 440 on EGKB/GOMUP: ban=true.  CSV line 9291.
    JSON(c, R"({
        "alerts":[{"srd":440,
                   "note":"Only available when pre-approved during events. Reroute at all other times.",
                   "warn":false,"ban":true}]
    })");
    bool warn = false;
    EXPECT_FALSE(checkOneAlerts(c, warn));
    EXPECT_FALSE(warn);  // ban-only note must not also set warn
}

TEST(ApiConstraint_Alerts, WarnAlert_ReturnsTrueAndSetsWarn)
{
    JSON(c, R"({"alerts":[{"srd":489,"note":"Pilot discretion.","warn":true,"ban":false}]})");
    bool warn = false;
    EXPECT_TRUE(checkOneAlerts(c, warn));
    EXPECT_TRUE(warn);
}

TEST(ApiConstraint_Alerts, NoAlerts_AlwaysPasses)
{
    JSON(c, R"({"dir":"even","min":255})");
    bool warn = false;
    EXPECT_TRUE(checkOneAlerts(c, warn));
    EXPECT_FALSE(warn);
}

TEST(ApiConstraint_Alerts, EmptyAlertsArray_AlwaysPasses)
{
    JSON(c, R"({"alerts":[]})");
    bool warn = false;
    EXPECT_TRUE(checkOneAlerts(c, warn));
}

// ── Integration: real API constraints from known SRD routes ──────────────────
//
// Each test encodes a single constraint exactly as the API serves it and
// verifies the full set of checks produces the right pass/fail outcome.
// Source: verification_report_20260315_140931.json + out.json (Feb 2026 AIRAC).

// CSV line 9291 — EGKB/GOMUP, dir=even, min=255, ban alert (note 440).
// A pilot filing EGKB→KLAX N0460F360 DCT DET N601 BPK UN601 ABEVI UN590 LORTA
// MUST be flagged as banned.
TEST(ApiConstraint_Integration, EGKB_GOMUP_Line9291_BanAlertFired)
{
    JSON(c, R"({
        "dir":     "even",
        "min":     255,
        "nodests": ["EG","EI"],
        "points":  ["GOMUP"],
        "route":   ["DET N601 BPK UN601 ABEVI UN590 LORTA"],
        "alerts":  [{"srd":440,
                     "note":"Only available when pre-approved during events. Reroute at all other times.",
                     "warn":false,"ban":true}]
    })");

    // Flight plan parameters:
    std::string destination = "KLAX";
    auto points  = vec({"GOMUP"});
    auto route   = vec({"DET", "N601", "BPK", "UN601", "ABEVI", "UN590", "LORTA"});
    int  rfl     = 36000;  // FL360 — even, above min 255

    // All checks pass the constraint INTO the alert stage:
    EXPECT_TRUE (checkOneDestination(c, destination));  // KLAX not in EG/EI
    EXPECT_TRUE (checkOneExitPoint  (c, points));       // GOMUP in ["GOMUP"]
    EXPECT_TRUE (checkOneRoute      (c, route));        // route prefix matches
    EXPECT_TRUE (checkOneMinMax     (c, rfl));          // FL360 >= min 255
    EXPECT_TRUE (checkOneDirection  (c, rfl));          // FL360 is even

    // Alert fires a ban — plugin must block this flight plan.
    bool warn = false;
    EXPECT_FALSE(checkOneAlerts(c, warn));
    EXPECT_FALSE(warn);
}

// CSV line 9291 — same constraint but wrong exit point (BALIX instead of GOMUP).
// The constraint should be skipped entirely: exit check fails → no ban.
TEST(ApiConstraint_Integration, EGKB_BALIX_Line9291_ConstraintSkipped)
{
    JSON(c, R"({
        "dir":     "even",
        "min":     255,
        "nodests": ["EG","EI"],
        "points":  ["GOMUP"],
        "route":   ["DET N601 BPK UN601 ABEVI UN590 LORTA"],
        "alerts":  [{"srd":440,"warn":false,"ban":true}]
    })");

    // Same route but exiting via BALIX — constraint must not apply.
    EXPECT_FALSE(checkOneExitPoint(c, vec({"BALIX"})));
}

// CSV line 19486 — EGSC/EGPF, dir=even, min=245, dests=["EGPF","EGPK"].
//
// IMPORTANT: dests=["EGPF","EGPK"] AND nodests=["EG","EI"] is an impossible combination.
// EGPF starts with "EG" so nodests blocks it before dests can allow it.
// This documents actual plugin behaviour: the destination check always returns false,
// making the dests field dead. This is a real plugin bug to be investigated.
TEST(ApiConstraint_Integration, EGSC_EGPF_Line19486_DestinationAlwaysBlocked)
{
    JSON(c, R"({
        "dir":     "even",
        "min":     245,
        "dests":   ["EGPF","EGPK"],
        "nodests": ["EG","EI"],
        "route":   ["EBOTO UN601 RIBEL"]
    })");

    // nodests["EG"] blocks EGPF before dests can match — dests field is dead.
    EXPECT_FALSE(checkOneDestination(c, "EGPF"));
    EXPECT_FALSE(checkOneDestination(c, "LFPG"));  // not in dests either

    // Other checks work correctly in isolation:
    auto route = vec({"EBOTO", "UN601", "RIBEL"});
    int  rfl   = 28000;
    EXPECT_TRUE(checkOneRoute    (c, route));
    EXPECT_TRUE(checkOneMinMax   (c, rfl));
    EXPECT_TRUE(checkOneDirection(c, rfl));
    bool warn = false;
    EXPECT_TRUE(checkOneAlerts   (c, warn));
}

// Same EGSC/EGPF constraint but filed FL240 — below min 245.
TEST(ApiConstraint_Integration, EGSC_EGPF_Line19486_TooLow_MinFails)
{
    JSON(c, R"({
        "dir": "even", "min": 245,
        "dests": ["EGPF","EGPK"], "nodests": ["EG","EI"],
        "route": ["EBOTO UN601 RIBEL"]
    })");
    EXPECT_FALSE(checkOneMinMax(c, 24000));  // FL240 < min 245
}

// CSV line 19488 — EGSC/EGPF, min=105, max=245 (low-level band).
TEST(ApiConstraint_Integration, EGSC_EGPF_Line19488_LowBand)
{
    JSON(c, R"({
        "dir": "even", "min": 105, "max": 245,
        "dests": ["EGPF","EGPK"], "nodests": ["EG","EI"],
        "route": ["EBOTO N601 POL N601 RIBEL"]
    })");

    auto route = vec({"EBOTO", "N601", "POL", "N601", "RIBEL"});
    EXPECT_TRUE (checkOneRoute  (c, route));
    EXPECT_TRUE (checkOneMinMax (c, 18000));  // FL180 — in band
    EXPECT_FALSE(checkOneMinMax (c, 25000));  // FL250 — above max
    EXPECT_FALSE(checkOneMinMax (c, 10000));  // FL100 — below min
}

// ── SidApplicability — anySidLevelRestrictionApplicable ──────────────────────
//
// Regression tests for the EGPH/TLA jet case (issue #174).
//
// The bug: jets on EGPH TLA outside 2300-0559 got RST instead of RTE/DST.
// Root cause: the only sidlevel=true restriction (types=["J"], 2300-0559)
// was not applicable outside its time window, but the code treated
// "no SID-level restriction applicable" as "SID-level restriction failed".
//
// Data mirror of in.json EGPH/TLA restrictions array (AIRAC 2603):
//   Restriction 0: types=["T","P","E"], no sidlevel  → SID-level: skip
//   Restriction 1: types=["J"], 2300-0559, sidlevel=true
//
// Day convention: Monday=0 … Sunday=6 (matches timedata[5] in production).

#include "SidApplicability.hpp"
using namespace SidApplicability;

// The restrictions JSON used in all EGPH/TLA tests below.
// Mirrors the live in.json EGPH TLA entry exactly.
static const char* EGPH_TLA_RESTRICTIONS = R"([
    {
        "types": ["T","P","E"],
        "alt":   ["GOSAM"]
    },
    {
        "types":    ["J"],
        "route":    ["N864","N57","L612","UN864","UN57","UL612"],
        "start":    {"time":"2300"},
        "end":      {"time":"0559"},
        "sidlevel": true
    }
])";

// ── Issue #174 core regression: jet outside 2300-0559 → no SID-level restriction applicable

TEST(SidApplicability_EgphTla, Jet_OutsideWindow_NoSidLevelApplicable)
{
    // A jet (engine type "J") on TLA at 14:00 UTC (well outside 2300-0559).
    // Restriction 0 is not sidlevel — skipped.
    // Restriction 1 is sidlevel=true, types=["J"] matches, but time window fails.
    // Expected: false — no SID-level restriction is applicable.
    // This is the exact scenario that produced false RST before the #174 fix.
    JSON(doc, EGPH_TLA_RESTRICTIONS);
    ASSERT_FALSE(doc.HasParseError());
    EXPECT_FALSE(anySidLevelRestrictionApplicable(doc, "J", "B738", "", 0, 14, 0))
        << "Jet on TLA at 14:00 should have no applicable SID-level restriction";
}

// ── Jet inside the night window → SID-level restriction IS applicable

TEST(SidApplicability_EgphTla, Jet_InsideWindow_SidLevelApplicable)
{
    // A jet (engine type "J") on TLA at 23:30 UTC (inside 2300-0559).
    // Restriction 1: sidlevel=true, types=["J"] matches, time window passes.
    // Expected: true — the SID-level night restriction applies.
    JSON(doc, EGPH_TLA_RESTRICTIONS);
    ASSERT_FALSE(doc.HasParseError());
    EXPECT_TRUE(anySidLevelRestrictionApplicable(doc, "J", "B738", "", 0, 23, 30))
        << "Jet on TLA at 23:30 should have an applicable SID-level restriction";
}

// ── Turboprop on TLA → restriction 0 is not sidlevel, restriction 1 type mismatch

TEST(SidApplicability_EgphTla, Turboprop_NoSidLevelRestrictions)
{
    // A turboprop (engine type "T") on TLA — at any time.
    // Restriction 0: types=["T","P","E"] matches, but it has NO sidlevel flag — skipped.
    // Restriction 1: sidlevel=true, types=["J"] — type mismatch.
    // Expected: false — no SID-level restriction applies to turboprops.
    JSON(doc, EGPH_TLA_RESTRICTIONS);
    ASSERT_FALSE(doc.HasParseError());
    EXPECT_FALSE(anySidLevelRestrictionApplicable(doc, "T", "DH8D", "", 0, 14, 0))
        << "Turboprop on TLA should have no applicable SID-level restriction";
}

// ── Empty restrictions array → never applicable

TEST(SidApplicability_EdgeCases, EmptyArray_ReturnsFalse)
{
    JSON(doc, R"([])");
    ASSERT_FALSE(doc.HasParseError());
    EXPECT_FALSE(anySidLevelRestrictionApplicable(doc, "J", "B738", "", 0, 14, 0));
}

// ── No sidlevel restrictions at all → never applicable

TEST(SidApplicability_EdgeCases, NoSidLevelRestrictions_ReturnsFalse)
{
    // Array with a non-sidlevel type restriction only.
    JSON(doc, R"([{"types":["J"]}])");
    ASSERT_FALSE(doc.HasParseError());
    EXPECT_FALSE(anySidLevelRestrictionApplicable(doc, "J", "B738", "", 0, 14, 0));
}

// ── sidlevel restriction with no selectors → always applicable

TEST(SidApplicability_EdgeCases, SidLevelNoSelectors_AlwaysApplicable)
{
    // A sidlevel restriction with no types, suffix, or time filter — applies to all flights.
    JSON(doc, R"([{"sidlevel":true,"alt":["OTHER"]}])");
    ASSERT_FALSE(doc.HasParseError());
    EXPECT_TRUE(anySidLevelRestrictionApplicable(doc, "J", "B738", "", 0, 14, 0));
    EXPECT_TRUE(anySidLevelRestrictionApplicable(doc, "T", "DH8D", "2C", 0, 3, 0));
}

// main() is defined in TimeWindowTests.cpp — one entry point for the whole suite.
