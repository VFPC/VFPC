# Change Log — time-handling branch

**Branch:** `time-handling`
**Date:** 2026-03-12
**Status:** Changes made locally. Not yet reviewed, tested, or submitted upstream.
**Upstream permissions:** Not yet granted. These changes are on a personal branch only.

See `docs/time-handling-bug-analysis.md` for full context and reasoning.

---

## File modified: `src/analyzeFP.cpp`

---

### Change 1 — Fix out-of-bounds loop bound

**Approx. line:** 477

**Severity:** Medium (undefined behaviour / potential crash)

**Before:**
```cpp
for (size_t i = 0; i < timedata.size(); i++) {
```

**After:**
```cpp
for (size_t i = 0; i < lastupdate.size(); i++) {
```

**Why:** `timedata` has 6 elements; `lastupdate` has 5. The loop comparing them to detect whether the API data has been updated was iterating one past the end of `lastupdate`, accessing `lastupdate[5]` which does not exist. Changed the loop bound to `lastupdate.size()` (5) so the comparison stays within bounds. The 6th `timedata` element (day-of-week) has no counterpart in `lastupdate` and should not be compared.

---

### Change 2 — Save day-of-week before the time-parse try/catch

**Approx. line:** 534 (after change; was inside try block around line 546)

**Severity:** Low

**Before:**
```cpp
if (time.size() == 8) {
    try {
        int hour = stoi(time.substr(0, 2));
        int mins = stoi(time.substr(3, 2));
        timedata[3] = hour;
        timedata[4] = mins;
        bufLog("Version Call: Time Data Read and Saved Successfully");

        timedata[5] = day;   // <-- inside try block
        bufLog("Version Call: Weekday Data Saved Successfully");
    }
    catch (...) { ... }
}
```

**After:**
```cpp
timedata[5] = day;   // <-- moved here, before try block
bufLog("Version Call: Weekday Data Saved Successfully");

if (time.size() == 8) {
    try {
        int hour = stoi(time.substr(0, 2));
        int mins = stoi(time.substr(3, 2));
        timedata[3] = hour;
        timedata[4] = mins;
        bufLog("Version Call: Time Data Read and Saved Successfully");
    }
    catch (...) { ... }
}
```

**Why:** The `day` variable is derived from `version["day"].GetInt()` which runs before the try block and cannot throw. Saving it inside the try block meant that if the time string parse failed, the day-of-week would silently not be updated even though it was valid. Moving it before the try ensures it is always saved.

---

### Change 3 — Add weekday guard to same-day restriction branch

**Approx. line:** 864

**Severity:** High

**Before:**
```cpp
else if (startdate == enddate) {
    if (!time) {
        valid = true;
    }
    else if ((timedata[3] > starttime[0] || (timedata[3] == starttime[0] && timedata[4] >= starttime[1]))
          && (timedata[3] < endtime[0]   || (timedata[3] == endtime[0]   && timedata[4] <= endtime[1]))) {
        valid = true;
    }
}
```

**After:**
```cpp
else if (startdate == enddate) {
    if (timedata[5] == startdate) {
        if (!time) {
            valid = true;
        }
        else if ((timedata[3] > starttime[0] || (timedata[3] == starttime[0] && timedata[4] >= starttime[1]))
              && (timedata[3] < endtime[0]   || (timedata[3] == endtime[0]   && timedata[4] <= endtime[1]))) {
            valid = true;
        }
    }
}
```

**Why:** When a restriction applies to a single weekday with a time window (e.g., "Monday 09:00–16:15 only"), `startdate` and `enddate` are the same. The original code checked the time window but never verified that today (`timedata[5]`) was actually that weekday. The fix wraps the existing logic in `if (timedata[5] == startdate)`.

---

### Change 4 — Rewrite wrap-around weekday branch

**Approx. line:** 889

**Severity:** High

**Before:**
```cpp
else if (startdate > enddate) {
    if (timedata[5] < startdate || timedata[5] > enddate) {
        valid = true;   // BUG: absorbs mid-week days AND boundary days
    }
    else if (timedata[5] == startdate) {   // dead code
        if (!time || timedata[3] > starttime[0] || ...) { valid = true; }
    }
    else if (timedata[5] == enddate) {     // dead code
        if (!time || timedata[3] < endtime[0] || ...) { valid = true; }
    }
}
```

**After:**
```cpp
else if (startdate > enddate) {
    if (timedata[5] == startdate) {
        if (!time || timedata[3] > starttime[0] || (timedata[3] == starttime[0] && timedata[4] >= starttime[1])) {
            valid = true;
        }
    }
    else if (timedata[5] == enddate) {
        if (!time || timedata[3] < endtime[0] || (timedata[3] == endtime[0] && timedata[4] < endtime[1])) {
            valid = true;
        }
    }
    else if (timedata[5] > startdate || timedata[5] < enddate) {
        valid = true;
    }
}
```

**Why:** This branch handles restrictions that wrap around the end of the week (e.g., Friday→Monday, where `startdate=4 > enddate=0`). The original general condition `timedata[5] < startdate || timedata[5] > enddate` is true for days 0, 1, 2, 3 when `startdate=4, enddate=0` — incorrectly including Tuesday, Wednesday, and Thursday. It also absorbed Friday (4) and Monday (0) into the general `valid = true` path, making the two `else if` boundary-day time checks unreachable dead code.

The fix checks boundary days first (with their respective time guards), then uses `timedata[5] > startdate || timedata[5] < enddate` for the interior days of the wrap range. For `startdate=4, enddate=0` this correctly evaluates to true only for days 5 (Sat) and 6 (Sun).

---

### Change 5 — Unify end-boundary semantics to inclusive (<=) across all branches

**Approx. lines:** 884, 896 (before refactor into TimeWindow.hpp)

**Severity:** Low (pre-existing inconsistency, not introduced by the patch)

**Before (forward cross-day end boundary):**
```cpp
if (!time || timedata[3] < endtime[0] || (timedata[3] == endtime[0] && timedata[4] < endtime[1])) {
```

**Before (wrap-around end boundary):**
```cpp
if (!time || timedata[3] < endtime[0] || (timedata[3] == endtime[0] && timedata[4] < endtime[1])) {
```

**After (both branches, now in `TimeWindow.hpp` as `beforeEnd()`):**
```cpp
return currentHour < endtime[0]
    || (currentHour == endtime[0] && currentMin <= endtime[1]);
```

**Why:** The same-day and time-only branches already used `<=` (inclusive end boundary). The two cross-day branches used `<` (exclusive). The inconsistency meant a restriction coded to end at `XX:59` (a common pattern — rules are authored ending on the 59th minute to occupy a whole hour) was evaluated as NOT active at exactly `XX:59` in cross-day scenarios. Fixing to `<=` throughout makes all four branches consistent and matches the author's intent. Confirmed by test cases W07, A05, and O05b.

---

## What to do next

1. **Get a build environment working** — Visual Studio with the project's library dependencies. The project builds as a Windows DLL targeting EuroScope.
2. **Build the test harness** — See `docs/test-harness.md` for the full spec. This must exist before the PR is raised so the fixes can be demonstrated to work. Without it, the maintainer has only our word that the logic is correct.
3. **Run the test cases** — All cases in `docs/test-harness.md` must pass before the PR is raised.
4. **Confirm the day-of-week convention** — Verify with the upstream maintainer that the live API sends `day` as Sunday=0, which is what the `(day + 6) % 7` conversion assumes. The `out.json` sample data is consistent with this but the live API has not been queried directly.
5. **Check `overrideRestrictions`** — The field is always empty in the sample data but the code may reference it. Audit whether it interacts with the restriction logic.
6. **Raise a PR** — Once permissions are granted. Keep the PR strictly to these four bug fixes. Do not include any of the future work items from `docs/time-handling-bug-analysis.md`. The PR description should reference the bug analysis document and the test results.

---

## Approach to the maintainer

The pitch is simple and narrow:

- The time-constraint logic has never worked correctly for the two most common restriction types in the UK dataset
- Here are the bugs, here is the `out.json` data that proves they exist
- Here are four small changes that fix it, and here are the test results showing they work
- Nothing else was changed

Everything else (PC time, BST, slower polling, entry points, refactor) is a separate conversation for after the fixes are trusted and merged. Conflating them risks turning a small focused fix into a large controversial change that is hard to review.
