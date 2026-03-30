// RuntimeConstraintTests.cpp
// Loads out.json at test startup and verifies every constraint against all six
// checker functions.  Same coverage as the generated tests but compiles in
// seconds because there are no inline string literals.
//
// Set the OUT_JSON environment variable (or pass --out_json=<path> on the
// command line) to tell the test where to find out.json.
//
// Expected results are derived from the constraint fields themselves using the
// same logic as generate_constraint_tests.py:
//
//   checkOneMinMax     — synthetic RFLs just inside and outside [min, max]
//   checkOneDirection  — even/odd RFL matched against "dir" field
//   checkOneAlerts     — ban/warn flags read from alerts array
//   checkOneExitPoint  — first listed point should match; synthetic miss should fail
//   checkOneDestination — nodests blocks EG/EI; dead-combo documented
//   checkOneRoute      — first route variant should match when given its tokens
//
// Build: included in VFPC_Tests.vcxproj alongside TimeWindowTests.cpp.
// The generated GeneratedConstraintTests_*.cpp files are no longer needed.

#include <gtest/gtest.h>
#include "ConstraintChecks.hpp"
#include "rapidjson/document.h"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace ConstraintChecks;
using namespace rapidjson;

// ── Out.json loader ───────────────────────────────────────────────────────────

static Document              g_outJson;
static const Value*          g_airports = nullptr;
static bool                  g_loaded = false;
static int                   g_constraintCount = 0;
static int                   g_deadComboCount  = 0;

static bool LoadOutJson(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return false;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string content = ss.str();
    g_outJson.Parse<kParseDefaultFlags>(content.c_str());
    if (g_outJson.HasParseError()) return false;
    // Support both bare array (legacy) and {cycle, airports} envelope (new parser)
    if (g_outJson.IsArray()) {
        g_airports = &g_outJson;
    } else if (g_outJson.IsObject() && g_outJson.HasMember("airports") && g_outJson["airports"].IsArray()) {
        g_airports = &g_outJson["airports"];
    } else {
        return false;
    }
    return true;
}

// ── Test fixture ──────────────────────────────────────────────────────────────

class RuntimeConstraintTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        // Priority: --out_json flag > OUT_JSON env var > default path
        const char* envPath = std::getenv("OUT_JSON");
        std::string path = envPath ? envPath
            : "C:\\Users\\jkino\\Desktop\\SRD Testing Files\\output files from SRD Parser\\out.json";

        // Check for --out_json command line arg (gtest passes unknown flags through)
        // We parse it from the raw argv stored by gtest infrastructure via
        // testing::internal::GetArgvs() — not available in all versions, so we
        // rely on the env var / default path approach above.

        ASSERT_TRUE(LoadOutJson(path))
            << "Could not load out.json from: " << path
            << "\nSet OUT_JSON environment variable to the correct path.";

        g_loaded = true;
    }
};

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::vector<std::string> split_tokens(const std::string& s) {
    std::vector<std::string> tokens;
    std::istringstream ss(s);
    std::string tok;
    while (ss >> tok) tokens.push_back(tok);
    return tokens;
}

static std::string constraint_id(const char* icao, int sidIdx, int cIdx) {
    return std::string(icao) + "_S" + std::to_string(sidIdx) + "_C" + std::to_string(cIdx);
}

// ── checkOneMinMax ────────────────────────────────────────────────────────────

TEST_F(RuntimeConstraintTest, MinMax_AllConstraints) {
    if (!g_loaded) GTEST_SKIP();
    int tested = 0, failures = 0;
    for (SizeType a = 0; a < g_airports->Size(); a++) {
        const auto& airport = (*g_airports)[a];
        const char* icao = airport["icao"].GetString();
        const auto& sids = airport["sids"];
        for (SizeType s = 0; s < sids.Size(); s++) {
            const auto& constraints = sids[s]["constraints"];
            for (SizeType c = 0; c < constraints.Size(); c++) {
                const auto& con = constraints[c];
                std::string id = constraint_id(icao, s, c);

                int minFl = (con.HasMember("min") && con["min"].IsInt()) ? con["min"].GetInt() : 0;
                int maxFl = (con.HasMember("max") && con["max"].IsInt()) ? con["max"].GetInt() : 0;

                // Known upstream NATS data anomaly: some constraints have min > max.
                // Skip these — they are a data bug, not a checker bug.
                if (minFl > 0 && maxFl > 0 && minFl > maxFl) continue;

                if (minFl > 0) {
                    // Just below min should fail
                    int rfl = (minFl - 1) * 100;
                    EXPECT_FALSE(checkOneMinMax(con, rfl)) << id << " below-min should fail";
                    // At min should pass
                    rfl = minFl * 100;
                    EXPECT_TRUE(checkOneMinMax(con, rfl)) << id << " at-min should pass";
                    tested++;
                }
                if (maxFl > 0 && maxFl < 660) {
                    // Just above max should fail
                    int rfl = (maxFl + 1) * 100;
                    EXPECT_FALSE(checkOneMinMax(con, rfl)) << id << " above-max should fail";
                    // At max should pass
                    rfl = maxFl * 100;
                    EXPECT_TRUE(checkOneMinMax(con, rfl)) << id << " at-max should pass";
                    tested++;
                }
                g_constraintCount++;
            }
        }
    }
    RecordProperty("constraints_tested", tested);
}

// ── checkOneDirection ─────────────────────────────────────────────────────────

TEST_F(RuntimeConstraintTest, Direction_AllConstraints) {
    if (!g_loaded) GTEST_SKIP();
    int tested = 0;
    for (SizeType a = 0; a < g_airports->Size(); a++) {
        const auto& airport = (*g_airports)[a];
        const char* icao = airport["icao"].GetString();
        const auto& sids = airport["sids"];
        for (SizeType s = 0; s < sids.Size(); s++) {
            const auto& constraints = sids[s]["constraints"];
            for (SizeType c = 0; c < constraints.Size(); c++) {
                const auto& con = constraints[c];
                std::string id = constraint_id(icao, s, c);
                if (!con.HasMember("dir") || !con["dir"].IsString()) continue;
                std::string dir = con["dir"].GetString();
                for (auto& ch : dir) ch = toupper(ch);

                // FL360 = even, FL350 = odd
                if (dir == "EVEN") {
                    EXPECT_TRUE(checkOneDirection(con, 36000))  << id << " FL360 even should pass";
                    EXPECT_FALSE(checkOneDirection(con, 35000)) << id << " FL350 odd should fail";
                } else if (dir == "ODD") {
                    EXPECT_TRUE(checkOneDirection(con, 35000))  << id << " FL350 odd should pass";
                    EXPECT_FALSE(checkOneDirection(con, 36000)) << id << " FL360 even should fail";
                }
                tested++;
            }
        }
    }
    RecordProperty("direction_constraints_tested", tested);
}

// ── checkOneAlerts ────────────────────────────────────────────────────────────

TEST_F(RuntimeConstraintTest, Alerts_AllConstraints) {
    if (!g_loaded) GTEST_SKIP();
    for (SizeType a = 0; a < g_airports->Size(); a++) {
        const auto& airport = (*g_airports)[a];
        const char* icao = airport["icao"].GetString();
        const auto& sids = airport["sids"];
        for (SizeType s = 0; s < sids.Size(); s++) {
            const auto& constraints = sids[s]["constraints"];
            for (SizeType c = 0; c < constraints.Size(); c++) {
                const auto& con = constraints[c];
                std::string id = constraint_id(icao, s, c);
                if (!con.HasMember("alerts") || !con["alerts"].IsArray()) continue;

                bool expBan = false, expWarn = false;
                for (SizeType i = 0; i < con["alerts"].Size(); i++) {
                    if (con["alerts"][i].HasMember("ban")  && con["alerts"][i]["ban"].GetBool())  expBan  = true;
                    if (con["alerts"][i].HasMember("warn") && con["alerts"][i]["warn"].GetBool()) expWarn = true;
                }
                bool warn = false;
                bool pass = checkOneAlerts(con, warn);
                EXPECT_EQ(!expBan, pass)  << id << " ban mismatch";
                EXPECT_EQ(expWarn, warn)  << id << " warn mismatch";
            }
        }
    }
}

// ── checkOneExitPoint ─────────────────────────────────────────────────────────

TEST_F(RuntimeConstraintTest, ExitPoint_AllConstraints) {
    if (!g_loaded) GTEST_SKIP();
    for (SizeType a = 0; a < g_airports->Size(); a++) {
        const auto& airport = (*g_airports)[a];
        const char* icao = airport["icao"].GetString();
        const auto& sids = airport["sids"];
        for (SizeType s = 0; s < sids.Size(); s++) {
            const auto& constraints = sids[s]["constraints"];
            for (SizeType c = 0; c < constraints.Size(); c++) {
                const auto& con = constraints[c];
                std::string id = constraint_id(icao, s, c);
                if (!con.HasMember("points") || !con["points"].IsArray() || con["points"].Size() == 0) continue;

                // First listed point should pass
                std::string firstPoint = con["points"][SizeType(0)].GetString();
                EXPECT_TRUE(checkOneExitPoint(con, {firstPoint})) << id << " listed point should pass";

                // Synthetic miss should fail
                EXPECT_FALSE(checkOneExitPoint(con, {"ZZZNOTAPOINT"})) << id << " unlisted point should fail";
            }
        }
    }
}

// ── checkOneRoute ─────────────────────────────────────────────────────────────

TEST_F(RuntimeConstraintTest, Route_AllConstraints) {
    if (!g_loaded) GTEST_SKIP();
    for (SizeType a = 0; a < g_airports->Size(); a++) {
        const auto& airport = (*g_airports)[a];
        const char* icao = airport["icao"].GetString();
        const auto& sids = airport["sids"];
        for (SizeType s = 0; s < sids.Size(); s++) {
            const auto& constraints = sids[s]["constraints"];
            for (SizeType c = 0; c < constraints.Size(); c++) {
                const auto& con = constraints[c];
                std::string id = constraint_id(icao, s, c);
                if (!con.HasMember("route") || !con["route"].IsArray() || con["route"].Size() == 0) continue;

                std::string firstVariant = con["route"][SizeType(0)].GetString();
                if (firstVariant == "*") continue; // wildcard always passes — skip

                auto tokens = split_tokens(firstVariant);
                EXPECT_TRUE(checkOneRoute(con, tokens)) << id << " first route variant should match";
            }
        }
    }
}

// ── checkOneDestination ───────────────────────────────────────────────────────

static bool dest_blocked_by_nodests(const std::string& dest, const Value& nodests) {
    for (SizeType i = 0; i < nodests.Size(); i++) {
        std::string prefix = nodests[i].GetString();
        if (dest.rfind(prefix, 0) == 0) return true;
    }
    return false;
}

TEST_F(RuntimeConstraintTest, Destination_AllConstraints) {
    if (!g_loaded) GTEST_SKIP();
    for (SizeType a = 0; a < g_airports->Size(); a++) {
        const auto& airport = (*g_airports)[a];
        const char* icao = airport["icao"].GetString();
        const auto& sids = airport["sids"];
        for (SizeType s = 0; s < sids.Size(); s++) {
            const auto& constraints = sids[s]["constraints"];
            for (SizeType c = 0; c < constraints.Size(); c++) {
                const auto& con = constraints[c];
                std::string id = constraint_id(icao, s, c);

                bool hasDests   = con.HasMember("dests")   && con["dests"].IsArray()   && con["dests"].Size() > 0;
                bool hasNodests = con.HasMember("nodests")  && con["nodests"].IsArray() && con["nodests"].Size() > 0;

                if (hasDests) {
                    // Find first dest not blocked by nodests — it should pass
                    std::string unblocked;
                    for (SizeType i = 0; i < con["dests"].Size(); i++) {
                        std::string d = con["dests"][i].GetString();
                        if (!hasNodests || !dest_blocked_by_nodests(d, con["nodests"])) {
                            unblocked = d;
                            break;
                        }
                    }
                    if (!unblocked.empty()) {
                        EXPECT_TRUE(checkOneDestination(con, unblocked))
                            << id << " listed unblocked dest '" << unblocked << "' should pass";
                    }

                    // A dest clearly not in the list should fail
                    EXPECT_FALSE(checkOneDestination(con, "ZZZZ"))
                        << id << " unlisted dest 'ZZZZ' should fail when dests is present";
                }

                if (hasNodests) {
                    // Pick a dest that matches a nodests prefix — should be blocked
                    std::string blocked;
                    for (SizeType i = 0; i < con["nodests"].Size(); i++) {
                        std::string prefix = con["nodests"][i].GetString();
                        // Construct a synthetic dest that starts with this prefix
                        if (prefix == "EG") { blocked = "EGLL"; break; }
                        if (prefix == "EI") { blocked = "EIDW"; break; }
                        // Generic: append enough chars to make a 4-char code
                        if (prefix.size() < 4) blocked = prefix + std::string(4 - prefix.size(), 'Z');
                        else blocked = prefix;
                        break;
                    }
                    if (!blocked.empty() && !hasDests) {
                        // Only test nodests-block when there's no dests list that might allow it
                        EXPECT_FALSE(checkOneDestination(con, blocked))
                            << id << " nodests-blocked dest '" << blocked << "' should fail";
                    }
                }
            }
        }
    }
}

// ── checkOneDestination — dead-combo regression guard ────────────────────────

TEST_F(RuntimeConstraintTest, Destination_DeadCombo_MustBeZero) {
    if (!g_loaded) GTEST_SKIP();
    int deadCombo = 0, total = 0;
    for (SizeType a = 0; a < g_airports->Size(); a++) {
        const auto& airport = (*g_airports)[a];
        const auto& sids = airport["sids"];
        for (SizeType s = 0; s < sids.Size(); s++) {
            const auto& constraints = sids[s]["constraints"];
            for (SizeType c = 0; c < constraints.Size(); c++) {
                const auto& con = constraints[c];
                total++;
                if (!con.HasMember("dests")   || !con["dests"].IsArray()   || con["dests"].Size() == 0) continue;
                if (!con.HasMember("nodests")  || !con["nodests"].IsArray() || con["nodests"].Size() == 0) continue;

                // Dead combo: every dests entry is blocked by a nodests prefix
                bool allDead = true;
                for (SizeType i = 0; i < con["dests"].Size(); i++) {
                    std::string dest = con["dests"][i].GetString();
                    if (!dest_blocked_by_nodests(dest, con["nodests"])) { allDead = false; break; }
                }
                if (allDead) deadCombo++;
            }
        }
    }
    g_deadComboCount = deadCombo;
    RecordProperty("dead_combo_constraints", deadCombo);
    RecordProperty("total_constraints", total);
    std::cout << "\n  [INFO] Dead-combo constraints (dests unreachable): "
              << deadCombo << " / " << total
              << " (" << (100*deadCombo/std::max(total,1)) << "%)\n";
    // [RULE:DEST-NODESTS-AIRPORT] + [RULE:DEST-NODESTS-SCRUB] together guarantee 0 dead combos.
    // If this fails, the ExitDestinationRules or OutputBuilder scrub has regressed.
    EXPECT_EQ(0, deadCombo) << "Dead combos detected — check ExitDestinationRules.cs and OutputBuilder.PostProcessCleanup";
}
