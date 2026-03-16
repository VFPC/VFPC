# Time Constraint Bug Analysis

**Branch:** `time-handling`
**Date of analysis:** 2026-03-12
**Analyst:** Cursor AI (session starting from cold read of codebase)

---

## What this document is

This is a working note written before any upstream permissions are granted. It records the findings from a read-only analysis of the codebase and the sample API data, so the context is not lost between sessions.

---

## Project overview

VFPC (UK VATSIM Flight Plan Checker) is a C++ Windows DLL that loads into EuroScope as a plugin. It validates IFR flight plans filed on the VATSIM network against departure restrictions pulled from a live REST API (`https://vfpc.tomjmills.co.uk/`). The result appears as a tag item in the controller's departure list: `OK!`, `SID`, `RTE`, `LVL`, `RST`, or `BAN`.

All core logic lives in `src/analyzeFP.cpp` (~3,000 lines). The plugin API, JSON parsing, and HTTP are handled by library code in `lib/`.

---

## The inherited suspicion

The person who inherited the codebase believed that time/day-of-week constraint checking had never worked correctly. This analysis confirms that suspicion — there are four distinct bugs in that logic.

---

## How time data flows through the system

1. `OnTimer()` fires every second. Every 10 seconds it spawns a background thread running `runWebCalls()`.
2. `runWebCalls()` calls `versionCall()`, which hits the `version` API endpoint and extracts:
   - Current UTC date → `timedata[0..2]` (year, month, day)
   - Current UTC time → `timedata[3..4]` (hour, minute)
   - Current UTC day-of-week → `timedata[5]` (0=Monday … 6=Sunday, after conversion)
3. The API returns `day` as a 0-based integer where **Sunday=0**. The code converts this to internal format (Monday=0) with: `day = (day + 6) % 7`.
4. When a flight plan is validated, `checkRestriction()` compares `timedata[3..5]` against `start`/`end` objects in each constraint's `restrictions` array.

---

## The API data format (from `out.json`)

Each restriction object looks like:

```json
{
  "start": { "date": 4, "time": "1600" },
  "end":   { "date": 0, "time": "0745" }
}
```

`date` values use the **internal** Monday=0 convention. `time` is a 4-digit string (HHMM).

Three real-world restriction patterns appear in the data:

| Pattern | Example | Frequency |
|---------|---------|-----------|
| Weekend wrap-around | Fri 16:00 → Mon 07:45 (`start.date=4, end.date=0`) | Most common by far |
| Overnight adjacent days | Mon 17:00 → Tue 08:15, ..., Fri 17:00 → Mon 08:15 | Very common |
| Same-day window | Mon 09:00 → Mon 16:15 (`start.date=0, end.date=0`) | Common |
| Date range only (no time) | Mon → Fri (`start.date=0, end.date=4`) | Occasional |

---

## Bugs found

### Bug 1 — Wrap-around weekday case is wrong (HIGH)

**Location:** `src/analyzeFP.cpp`, original lines 887–900

**Triggered by:** Every "weekend" restriction (Friday→Monday). The most common pattern in the entire dataset.

The condition used to identify interior days of the wrap range was:
```cpp
if (timedata[5] < startdate || timedata[5] > enddate) { valid = true; }
```

For `startdate=4 (Fri), enddate=0 (Mon)`, this evaluates to true for days 0, 1, 2, 3 — i.e., Monday **and** Tuesday, Wednesday, Thursday. Mid-week days were incorrectly allowed.

Additionally, the two `else if` branches that were supposed to apply time checks on the boundary days (Friday and Monday) were unreachable dead code, because Fri and Mon were also absorbed by the general condition.

**Effect in practice:** Any route with a weekend-only CDR1 restriction would show `OK!` on weekdays, giving controllers no warning that the route is not available Monday–Thursday.

---

### Bug 2 — Same-day restriction never checks the actual day (HIGH)

**Location:** `src/analyzeFP.cpp`, original lines 864–870

**Triggered by:** Any restriction where `start.date == end.date` (e.g., "Monday 09:00–16:15 only").

The `startdate == enddate` branch checked the time window but contained no check that `timedata[5] == startdate`. On any day of the week, if the controller's session happened to run during the time window (e.g., 09:00–16:15 UTC), the restriction would pass.

**Effect in practice:** A "Monday daytime only" restriction would pass on Tuesday, Wednesday, etc. as long as the clock showed a time within the window.

---

### Bug 3 — Out-of-bounds vector access (MEDIUM)

**Location:** `src/analyzeFP.cpp`, original line 477

`timedata` has 6 elements (indices 0–5). `lastupdate` has 5 elements (indices 0–4). The update-detection loop iterated `timedata.size()` times (6 iterations), causing `lastupdate[5]` to be accessed out of bounds. This is undefined behaviour in C++ — silent data corruption or a crash are both possible.

---

### Bug 4 — Day-of-week not saved if time parse fails (LOW)

**Location:** `src/analyzeFP.cpp`, original lines 528–547

`timedata[5] = day` was inside the `try` block for parsing the time string. If `stoi()` threw (malformed time from API), the day-of-week would not be updated even though it had been successfully read from the `day` field moments earlier.

---

## What was NOT investigated yet

- The `overrideRestrictions` field appears in the data (always empty `[]` in `out.json`) but the code may reference it — worth checking if it has any effect.
- The `notes` array on each constraint (contains SRD note numbers) — not clear if the plugin does anything with these.
- The `sidlevel` restriction mechanism — restrictions marked `sidlevel: true` apply at SID level rather than constraint level; the switching logic uses a pointer (`bool *fails`) to select which fail-flags array to write into. This has not been audited.
- No testing has been possible: the repo is not yet compiled locally and upstream PR rights are not yet in place.
- The API server itself has not been queried directly — all analysis was based on `out.json` from the SRD parser.

---

## Files changed

Only `src/analyzeFP.cpp` was modified. See `docs/changes.md` for line-by-line detail.

---

## Strategic position

The four bug fixes are the only changes on this branch. They are a tight, self-contained, defensible unit — one file, demonstrably wrong logic, real data proving the bugs exist, no behaviour changed that was previously working.

Everything below is future work. It is documented here for continuity but has **no coding synergy** with the current fixes. None of it should be included in the PR for the bug fixes. Each item is a separate conversation with the maintainer after the fixes are merged.

---

## Future work (separate from bug fixes)

### F1 — Replace server time with PC clock

The plugin currently gets the current UTC time from the server's version endpoint. This is unnecessary — Windows `GetSystemTime()` always returns UTC regardless of the PC's local timezone setting. The server call should be stripped of its time fields. The plugin should use `GetSystemTime()` instead, which eliminates up to 10 seconds of staleness in the time comparisons and removes a spurious dependency on the API just to know what time it is.

This does not change the validation logic — it only changes where the input values come from.

### F2 — BST/GMT offset handling

CDR restriction times in the SRD are published in UK local time, which shifts by one hour between GMT (winter, UTC+0) and BST (summer, UTC+1). The plugin currently makes no adjustment for this, meaning all time-boundary comparisons are one hour wrong during the BST period (last Sunday of March 01:00 UTC to last Sunday of October 01:00 UTC).

Recommended approach: store restriction times as-published (UK local) in the JSON, add a `dst_aware` flag to the schema, and have the plugin calculate the current UK offset from the UTC date it already holds. The offset calculation is a dozen lines of arithmetic — no external dependency needed. The UTC date sufficient to determine DST status is already available once F1 is implemented.

This change requires a JSON schema update on the server side as well as plugin changes.

### F3 — Slow down polling / Discord notification on data update

The plugin polls the version endpoint every 10 seconds. Data changes a few times a month. This should be slowed to 30–60 minutes. When data is updated, the server-side upload pipeline sends a Discord webhook message notifying controllers they can reload immediately or wait for the automatic refresh. The plugin should display a brief notification in EuroScope when it detects and applies a data update.

No plugin logic changes — just a constant change (`API_REFRESH_TIME`) and a small addition to the update-detected code path.

### F4 — FIR entry point support

The server side has been refactored and entry point data is available. The plugin currently only requests and validates against airport/SID data. En-route controllers need validation against FIR entry point restrictions.

Key design questions to resolve before any coding:
- What does the entry point data structure look like — same restriction schema as SID data?
- How does the plugin determine which entry points are relevant for the logged-on controller position? Options: derive from visible flight plan routes; server exposes a `/sector?callsign=...` endpoint; controller declares sector via a `.vfpc` command.

If the restriction schema is the same, the time-constraint logic (including the fixes already made) applies directly. The main engineering work is the "what do I request?" discovery mechanism.

### F5 — Refactor

`src/analyzeFP.cpp` is ~3,000 lines with no separation of concerns. Everything — HTTP, JSON parsing, business logic, EuroScope UI callbacks, logging — lives in one class. The code works but is very difficult to test, reason about, or extend safely.

Natural split:
- **API client** — curl wrapper, version call, SID fetch, timer/async refresh
- **Data model** — proper structs for `Restriction`, `Constraint`, `SID`, `Airport` replacing magic-index vectors and raw `Value&` references
- **Time checker** — standalone pure function taking plain data types, fully testable without EuroScope
- **Flight plan validator** — the seven-round pipeline, testable without EuroScope if data model is clean
- **EuroScope adapter** — thin bridge layer, the only part that cannot be tested outside EuroScope
- **Logger** — already almost separate, just needs making explicit

F5 is the enabler for F4. Adding entry point support cleanly requires clean foundations. The recommended sequence is: fixes merged → test harness → F1 → F2 → F3 → F5 (with F4 as the first feature built on the refactored foundations).
