# VFPC Changelog

All notable changes to the VFPC plugin are documented here.

---

## [Unreleased] — `time-handling` branch

### Fixed — Time restriction evaluation (5 bugs)

These changes affect `src/TimeWindow.hpp`, which was extracted from the inline time-checking
block in `analyzeFP.cpp::checkRestriction()`. See `docs/time-handling-bug-analysis.md` for
full root-cause analysis.

**Change 1 — Cross-midnight wrap detection**  
Time windows that span midnight (e.g. Monday 22:00 – Tuesday 06:00) were not being
recognised as wrap-around windows. The `startDay == endDay` guard was insufficient.
Fixed: wrap detection now correctly fires when the nominal start time is later than the
nominal end time, and evaluates the two halves of the window independently.

**Change 2 — Same-day window day comparison**  
Same-day restrictions were evaluating against the wrong day due to a logic error in the
original inline block. Fixed: correct day variable now referenced throughout.

**Change 3 — Adjacent overnight window**  
Restrictions spanning a single midnight boundary (e.g. Monday night into Tuesday morning)
were being treated as two disconnected half-windows instead of one contiguous window.
Fixed: adjacent-night case now handled as a unified span.

**Change 4 — Date-only restriction (no time fields)**  
When a restriction carried day-of-week fields but no `starttime`/`endtime`, it was
silently skipped or vacuously matched all times. Fixed: date-only restrictions now pass
for the entire 24 hours of the specified day(s) and fail on all other days.

**Change 5 — Inclusive end boundary**  
All restriction types now use `<=` for the end-boundary comparison (inclusive). The
previous inline code used `<` for cross-day restrictions and `<=` for same-day and
time-only restrictions. Production rules are authored with end times on the 59th minute
(e.g. `"0759"`) under the expectation that 07:59 itself is inside the window. Unified
to inclusive throughout.

### Added — `src/TimeWindow.hpp`

New header containing `checkTimeWindow(int day, int hour, int min, const rapidjson::Value& restriction) -> bool`.
This is a pure function with no EuroScope dependencies, making it independently testable.

### Added — `VFPC_Tests` project

New Visual Studio project containing 52 Google Test parameterized test cases covering all
five bug-fix scenarios. Build via `msbuild VFPC.sln /t:VFPC_Tests`.

See `docs/BuildingAndTesting.md` for build and run instructions.

### Changed — `src/analyzeFP.cpp`

The 97-line inline time restriction block inside `checkRestriction()` was replaced with a
single call to `checkTimeWindow()`. No behaviour change beyond the 5 bug fixes above.

---

## Prior Releases

_Pre-2026 release history is not yet documented in this file. See the Git log for details._
