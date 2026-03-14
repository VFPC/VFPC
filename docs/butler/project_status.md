# VFPC Project Status

_Last Updated: 2026-03-14 (time-handling patch session)_

> **Active task tracking is on GitHub Issues.**
> See: https://github.com/VFPC/VFPC/issues
> Cross-repo board: GitHub Project "vFPC Development Tracker" (VFPC org).

---

## Current State

**Branch:** `time-handling`  
**Status:** Ready to PR — awaiting upstream permissions

### What's Working ✅

- **Core plugin** — builds as a 64-bit Windows DLL (`VFPC.dll`) targeting the EuroScope SDK.
- **All flight plan checks** — route validity, SID validity, destination, altitude, type,
  day/time restrictions, odd/even rules, and SRD ban/warning alerts.
- **Time-handling patch** — 5 bugs fixed, all landed in `src/TimeWindow.hpp`:
  - Cross-midnight time windows now correctly evaluated (Change 1)
  - Same-day time windows use day-of-week correctly (Change 2)
  - Adjacent overnight windows (e.g. Mon night → Tue morning) now handled (Change 3)
  - Day-only restrictions (no time fields) now match all times on specified days (Change 4)
  - End-boundary changed from exclusive (`<`) to inclusive (`<=`) for all window types (Change 5)
- **Test project** (`VFPC_Tests`) — 52 parameterized Google Test cases, all passing.
  Build via `msbuild VFPC.sln /t:VFPC_Tests /p:Configuration=Debug /p:Platform=x64`

### What Needs Work

See https://github.com/VFPC/VFPC/issues for the full list. Key open items:

| Issue | Title | Dependency |
|-------|-------|-----------|
| #165  | BST/GMT timezone offset in time comparisons | Standalone |
| #166  | JSON sort-order sign-off from maintainer | Blocked on New-SRDParser#6 |
| #169  | Replace server-polled UTC with `GetSystemTime()` | Standalone |
| #170  | Evaluate time restrictions against EOBT | Blocked on #169 |
| #171  | Respect `override` field to suppress restrictions | Blocked on UKVFPCAPI#68 |

---

## Branch Strategy

`time-handling` contains the 5-bug patch plus the test infrastructure. It branches from the
current HEAD of whatever branch was active in January 2026 (pre-patch).

Do not merge until at least one other maintainer has reviewed and all tests pass in their
environment.

---

## Build Baselines

| Item | Value |
|------|-------|
| Language standard | C++17 (VFPC_Tests), C++14 (VFPC.dll) |
| Target platform | x64 |
| Toolset | v143 (VS 2022) |
| Test framework | Google Test v1.15.2 (submodule at `VFPC_Tests/third_party/googletest`) |
| Tests | 52 passing, 0 failing |

---

## Committed Work

| Commit | Description |
|--------|-------------|
| `01da6ef8` | time-handling: 5-bug patch, checkTimeWindow extraction, 52 tests, docs |

---

## Key Design Decisions

1. **`checkTimeWindow()` is a free function** in `src/TimeWindow.hpp`. It takes only
   primitive ints and a `rapidjson::Value` — no EuroScope dependency — making it
   independently testable.

2. **Test project is a standalone executable**, not a DLL. It compiles `gtest-all.cc`
   directly (no precompiled headers) and links nothing from the main VFPC project except
   the header `src/TimeWindow.hpp`.

3. **End-boundary is inclusive** because production rules are authored ending on minute 59.
   e.g. `"0759"` means "up to and including 07:59", not "up to but not including 07:59".

4. **Day-of-week is Monday=0** throughout the data pipeline. The single conversion point
   from Sunday=0 (Windows SYSTEMTIME) is in `versionCall()` using `(day+6)%7`.

5. **`checkRestriction()`** in `analyzeFP.cpp` remains unchanged from the user's perspective;
   it now delegates to `checkTimeWindow()` for the pure date/time logic.
