# VFPC Session Status Summary

**Last Updated:** 2026-04-13

---

## Session 2026-04-13: #174 merged and shipped as v3.7.2.0

**Branch state:**
- local `main` clean and pushed
- tag `v3.7.2.0` pushed
- `fix/174-sidwide-type-exclusion-rebased` pruned locally and on origin
- preserved rewrite branch: `Research-reported-issues`

**Completed:**
- PR `#182` merged — the rebased `sidwide=false` fix landed on `main`
- plugin version bumped to `3.7.2.0`
- Win32/x86 release path cleaned enough to build the live EuroScope DLL successfully
- `VFPC.dll` built and published as the `v3.7.2.0` release asset

**Outcome:**
- issue `#174` is closed and no longer waiting on release timing
- next VFPC work, if any, should start from released `main`, not from the old fix branch

---

## Session 2026-03-30: envelope fix merged; #174 rebuilt cleanly; release timing hold chosen

**Branch state:**
- local `main` clean and normalized
- active fix branch: `fix/174-sidwide-type-exclusion-rebased`
- preserved rewrite branch: `Research-reported-issues`

**Completed:**
- PR `#181` merged — `RuntimeConstraintTests` now accept both legacy array `out.json` and the `{cycle,airports}` envelope
- Old `#174` branch replayed cleanly on top of current `main`
- PR `#182` opened for the rebased `sidwide=false` fix
- `VFPC_Tests` rebuilt and verified **94/94 passing**
- Superseded old branch `fix/174-sidwide-type-exclusion` deleted locally and on origin

**Cross-repo conclusion update:**
- New-SRDParser `#58` had already established that parser/output behavior was correct
- SRDData `#195` was re-checked against live desktop AIRAC 2603 data and closed as stale
- Result: issue `#174` is no longer blocked by the earlier data/passthrough narrative

**Decision made:**
- PR `#182` is intentionally being held until shortly before the next AIRAC / controller-pack rollout
- Reason: merging early would prompt users to update the plugin before the preferred release window

---

## Session 2026-03-15: Build Fixes, Version Bump, DLL to Peter

**Branch:** `time-handling-v2`

### What Was Completed

**Build unblocked — VFPC.dll now compiles cleanly:**

- **Boost removed** (3 remaining usages replaced with std):
  - `analyzeFP.hpp:181` — `boost::to_upper` → `std::transform` with `::toupper`
  - `analyzeFP.cpp:1028` — `boost::trim` → `string::erase` + `find_first/last_not_of`
  - `analyzeFP.cpp:1054` — `boost::erase_all` → erase-remove idiom on single char `'#'`
- **Curl headers added** — `lib/include/curl/` (12 headers, curl 8.11.0 from GitHub),
  matching the existing `lib/include/boost/` and `lib/include/rapidjson/` pattern.
  `lib/libcurl_a.lib` was already present; only headers were missing.
- **Pre-existing bug fixed** (uncovered when curl stopped masking it):
  `std::to_string(out.GetParseError())` — this rapidjson version returns `const char*`,
  not an enum. Fixed at lines 367 and 639 by concatenating directly as a string.

**Version bumped to 3.7.1.0:**
- `src/Constant.hpp` — `MY_PLUGIN_VERSION` `"3.7.0.0"` → `"3.7.1.0"`
- `Resource.rc` — `FILEVERSION`/`PRODUCTVERSION` `1,0,0,1` → `3,7,1,0`;
  `FileVersion` string → `"3.7.1.0"`; `ProductVersion` string → `"3.7.1"`
  (resource file version was placeholder `1,0,0,1` — now correct)

**Tests verified: 87/87 passing** (up from 52 — additional constraint + runtime tests
added in the previous session on `time-handling-v2`).

**DLL delivered** to Peter (tester) at:
`C:\Users\jkino\AppData\Roaming\EuroScope\UK\Data\Plugin\VFPC\VFPC.dll`

**Uncommitted in repo:**
- `src/analyzeFP.cpp`, `src/analyzeFP.hpp`, `src/Constant.hpp`, `Resource.rc` — all modified
- `lib/include/curl/` — untracked (12 headers)
- `vcpkg` submodule staged, `.gitmodules` staged
- `VFPC_Tests/VFPC_Tests.vcxproj` — modified

### What's Next

- Await Peter's integration test result
- Commit all changes and raise PR once Peter confirms the DLL works

---

## Session 2026-03-14: Time-Handling Patch + Test Infrastructure

**Branch:** `time-handling`  
**Commit:** `01da6ef8`

### What Was Completed

**Bug fixes in `src/TimeWindow.hpp` (extracted from `analyzeFP.cpp::checkRestriction()`):**

- **Change 1 — Cross-midnight wrap** (`startDay == endDay` check replaced): restrictions
  that span midnight (e.g. Mon 22:00 – Tue 06:00) now correctly detect the wrap and split
  evaluation across both calendar days.
- **Change 2 — Same-day time window** (day comparison corrected): same-day restrictions
  were evaluating against the wrong day due to a logic error in the old inline block.
- **Change 3 — Adjacent overnight window** (two-day spanning restriction): restrictions
  that span exactly one midnight boundary (e.g. Mon night into Tue morning) are now
  handled as a single contiguous window, not two disconnected half-windows.
- **Change 4 — Date-only restriction** (no time fields): when a restriction has day-of-week
  fields but no `starttime`/`endtime`, it now passes for the entire 24 hours of the
  specified day(s) rather than vacuously matching everything (old behaviour) or always
  failing (would-be new regression).
- **Change 5 — Inclusive end boundary** (`<` → `<=`): all restriction types unified to
  inclusive end boundary. Production rules end at XX:59; the `<` comparison was silently
  excluding that minute. Confirmed user practice and corrected.

**New files:**
- `src/TimeWindow.hpp` — free function `checkTimeWindow(int day, int hour, int min, const rapidjson::Value& restriction) -> bool`
- `VFPC_Tests/TimeWindowTests.cpp` — 52 parameterized Google Test cases
- `VFPC_Tests/VFPC_Tests.vcxproj` — VS 2022 test project (x64, C++17, standalone executable)
- `VFPC_Tests/third_party/googletest/` — Google Test v1.15.2 as a git submodule

**Modified files:**
- `src/analyzeFP.cpp` — replaced 97-line inline time block with call to `checkTimeWindow()`
- `VFPC.sln` — added `VFPC_Tests` project
- `docs/changes.md` — documented all 5 changes
- `docs/test-harness.md` — updated to reflect implementation (Option A) and test results

**Issues filed during this session:**

| Issue | Repo | Title |
|-------|------|-------|
| #167 | VFPC | Feature: VATSIM UK registration confirmation |
| #168 | VFPC | Feature: UK controller pack version check and update enforcement |
| #169 | VFPC | Enhancement: replace server-polled UTC time with PC clock (`GetSystemTime()`) |
| #170 | VFPC | Feature: evaluate time restrictions against EOBT |
| #171 | VFPC | Feature: respect `override` field on constraints |
| #68  | UKVFPCAPI | Feature: store and serve the `override` field on constraints |
| #16  | New-SRDParser | Feature: document and adopt `override` field as the official suppression mechanism |

**Investigations completed:**
- Confirmed day-of-week convention: Monday=0 throughout (`in.json` schema, `out.json`,
  `Sid.json`, and VFPC internal `timedata[5]`). Single conversion point in `versionCall()`.
- Traced `override` field across New-SRDParser → UKVFPCAPI → VFPC. Field is parsed by
  the parser and emitted to `out.json`, but UKVFPCAPI drops it (not in MongoDB schema),
  and VFPC never reads it. Three inter-dependent issues filed.

**Compilation issues resolved:**
- `Microsoft.Cpp.Default.props` not found — fixed by installing "Desktop development
  with C++" workload via Visual Studio Installer.
- `$(SolutionDir)` not set when building `.vcxproj` directly — fixed by building via
  `msbuild VFPC.sln /t:VFPC_Tests`.
- `rapidjson::Document::Parse` template deduction (C2672 under MSVC 2022) — fixed by
  using `Parse<rapidjson::kParseDefaultFlags>(json)`.
- `WarningLevel` `Level0` invalid (MSVC v170) — changed to `TurnOffAllWarnings` in vcxproj.
- `rapidjson::GenericValue` private copy constructor (LNK2019) — eliminated
  `parseRestriction()` helper and parsed JSON inline in `TEST_P`.
- `git submodule add --depth 1 --branch v1.15.2` failure (shallow clone) — used
  `git submodule add --force` to reactivate existing local `.git` directory.

---

## Previous Sessions

_Earlier sessions pre-date this butler document. Key prior context:_

- **v3 rewrite** (`@lennycolton`, January 2021) — almost complete rewrite of original plugin
  by `@DrFreas`, `@hpeter2`, `@svengcz`. New-SRDParser created to provide AIRAC updates.
- **API** — built from scratch by `@GeekPro101` as `UKVFPCAPI` (Go, private).
- **New-SRDParser** — `@lennycolton`, C#, parses UK SRD CSV → JSON. Currently on
  `mc-preprocessor` branch with 804 tests passing, not yet merged to main.
