# Next Session Prompt — VFPC

_Generated: 2026-03-15 (build fixes + version bump session)_

## Read the Project Hub First

Before starting work, read the project-wide butler:
`C:\Users\jkino\Documents\GitHub\vFPC-Hub\Documentation\butler\next_session_prompt.md`

## Read These First (repo-specific)

1. `docs/butler/project_status.md` — current branch, build baselines, open issues
2. `docs/butler/session_status_summary.md` — detailed session history
3. `docs/butler/assistant_performance_rules.md` — VFPC-specific constraints (read before doing anything involving Go/C# repos, EuroScope SDK, or time logic)

---

## Current State

- **Branch:** `time-handling-v2`
- **Version:** `3.7.1.0` (Constant.hpp + Resource.rc)
- **Status:** DLL given to Peter for integration testing — awaiting result before committing + PR
- **5 bug fixes** in `src/TimeWindow.hpp` (Changes 1–5)
- **87/87 tests passing**
- **All changes uncommitted** (boost removal, curl headers, version bump, ParseError fix)

---

## What Needs Doing Next

### Immediate (this session)

1. **Await Peter's test result.** If he confirms the DLL works, commit all pending changes:
   - `src/analyzeFP.cpp`, `src/analyzeFP.hpp`, `src/Constant.hpp`, `Resource.rc`
   - `lib/include/curl/` (untracked — `git add lib/include/curl/`)
   - `vcpkg` submodule + `.gitmodules` (already staged)
   - `VFPC_Tests/VFPC_Tests.vcxproj`

2. **Verify tests still pass** before committing:

   ```powershell
   $msbuild = "D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
   & $msbuild "VFPC_Tests\VFPC_Tests.vcxproj" /p:Configuration=Release /p:Platform=x64 /p:SolutionDir="C:\Users\jkino\Documents\GitHub\VFPC\" /t:Rebuild
   & "bin\Release\tests\VFPC_Tests.exe"
   ```

   Expected: `[  PASSED  ] 87 tests.`

3. **Raise the PR** for `time-handling-v2` → main on `VFPC/VFPC`.

### Soon (tracked as GitHub issues)

| Priority | Issue | Action needed |
|----------|-------|---------------|
| High | #165 (BST/GMT offset) | Implement BST/GMT offset logic in time comparison |
| High | #169 (PC clock) | Replace server-polled UTC with `GetSystemTime()` |
| Medium | #170 (EOBT) | After #169, evaluate restrictions against EOBT |
| Medium | #171 (override field) | After UKVFPCAPI#68, read `override` from Sid.json |
| Low | #166 (JSON sort order) | Await New-SRDParser#6 sign-off |
| Low | #167 (VATSIM reg check) | New feature — separate from time-handling work |
| Low | #168 (version enforcement) | New feature — separate from time-handling work |

---

## Critical Technical Rules (quick reference)

1. **Day-of-week:** Monday=0 in all data. Single conversion in `versionCall()`: `(day+6)%7`.
2. **Times:** British local time in data (GMT/BST), currently compared against server UTC.
   Issue #165 tracks the fix.
3. **End-boundary:** Inclusive (`<=`). Production rules end at XX:59.
4. **`checkTimeWindow()`** — the testable entry point. Add new tests to `TimeWindowTests.cpp`
   whenever logic changes. Run via `VFPC_Tests.exe`.
5. **Building DLL:** Release Win32 via `msbuild VFPC.vcxproj /p:Configuration=Release /p:Platform=Win32`.
   Output goes to `%APPDATA%\EuroScope\UK\Data\Plugin\VFPC\VFPC.dll` (live install location).
   **Building tests:** must pass `/p:SolutionDir` when building `.vcxproj` directly (see above).
6. **Override field** — three-repo dependency. VFPC cannot consume it until UKVFPCAPI#68 lands.
7. **Never recursively list Go/C# repo directories** — use `Glob` + `Grep` (see performance rules).
8. **PowerShell heredocs don't work** — write multi-line content to a file, use `--body-file`.

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
