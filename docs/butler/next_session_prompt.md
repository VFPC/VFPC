# Next Session Prompt — VFPC

_Last updated: 2026-04-13 (v3.7.2.0 released; PR #182 merged; repo state reconciled)_

## Read the Project Hub First

Before starting work, read the project-wide butler:
`C:\Users\jkino\Documents\GitHub\vFPC-Hub\Documentation\butler\next_session_prompt.md`

## Read These First (repo-specific)

1. `docs/butler/project_status.md` — current branch, build baselines, open issues
2. `docs/butler/session_status_summary.md` — detailed session history
3. `docs/butler/assistant_performance_rules.md` — VFPC-specific constraints (read before doing anything involving Go/C# repos, EuroScope SDK, or time logic)

---

## Current State

- **Branch:** `main` locally, clean
- **Version:** `3.7.2.0`
- **Recent merge:** PR `#181` — `RuntimeConstraintTests` now accept both legacy array and `{cycle,airports}` envelope
- **Recent merge:** PR `#182` — rebased `#174` fix merged to `main`
- **Tests on held release branch:** `94/94` passing before merge
- **Release state:** `v3.7.2.0` DLL built and published from `main`

---

## What Needs Doing Next

### Immediate (when resuming work here)

1. **Start from clean `main`** at `v3.7.2.0` for any future production work here.
2. **Leave `Research-reported-issues` untouched**; it remains the preserved rewrite branch.
3. **Treat `fix/174-sidwide-type-exclusion-rebased` as historical only**; it has already been pruned and is no longer an active production branch.

### Soon (tracked as GitHub issues)

| Priority | Issue | Action needed |
|----------|-------|---------------|
| High | #174 | Shipped in `v3.7.2.0`; keep only as historical release traceability |
| High | #165 | Implement BST/GMT offset logic in time comparison |
| High | #169 | Replace server-polled UTC with `GetSystemTime()` |
| Medium | #170 | After #169, evaluate restrictions against EOBT |
| Medium | #171 | After wider data/API rollout, read `override` from Sid.json |

---

## Critical Technical Rules (quick reference)

1. **Day-of-week:** Monday=0 in all data. Single conversion in `versionCall()`: `(day+6)%7`.
2. **Times:** British local time in data (GMT/BST), currently compared against server UTC.
   Issue #165 tracks the fix.
3. **End-boundary:** Inclusive (`<=`). Production rules end at XX:59.
4. **`checkTimeWindow()`** — the testable entry point. Add new tests to `TimeWindowTests.cpp`
   whenever logic changes. Run via `VFPC_Tests.exe`.
5. **`SidApplicability.hpp`** now holds the pure applicability rule used by issue `#174`.
   Keep future `sidwide` logic changes test-backed there or in adjacent pure helpers.
6. **Building DLL:** Release Win32 via `msbuild VFPC.vcxproj /p:Configuration=Release /p:Platform=Win32`.
   Output goes to `%APPDATA%\EuroScope\UK\Data\Plugin\VFPC\VFPC.dll` (live install location).
   **Building tests:** must pass `/p:SolutionDir` when building `.vcxproj` directly (see above).
7. **Override field** — still a cross-repo dependency.
8. **Never recursively list Go/C# repo directories** — use `Glob` + `Grep` (see performance rules).
9. **PowerShell heredocs don't work** — write multi-line content to a file, use `--body-file`.

---

## Repository Map

| Repo | Language | Role |
|------|----------|------|
| `VFPC/VFPC` | C++ | EuroScope plugin (this repo) |
| `VFPC/UKVFPCAPI` | Go | REST API serving `Sid.json` to the plugin |
| `VFPC/New-SRDParser` | C# | Parses UK SRD → `out.json` → API |
| `VFPC/vFPC-Hub` | Docs | Cross-repo rules traceability |
| `VFPC/uk-controller-pack` | — | Sector files and controller pack |

---

## File Locations

| File | Purpose |
|------|---------|
| `src/TimeWindow.hpp` | Testable time-window logic |
| `src/analyzeFP.cpp` | Core flight plan analysis (calls `checkTimeWindow`) |
| `VFPC_Tests/TimeWindowTests.cpp` | 52 test cases |
| `VFPC_Tests/VFPC_Tests.vcxproj` | Test project file |
| `docs/butler/` | AI session continuity documents (this folder) |
| `docs/changes.md` | Branch change log |
| `docs/time-handling-bug-analysis.md` | Root cause analysis of the 5 bugs |
| `docs/test-harness.md` | Test specification and results |
