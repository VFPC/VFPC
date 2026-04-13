# VFPC Project Status

_Last Updated: 2026-04-13 (v3.7.2.0 released; PR #182 merged and shipped)_

> **Active task tracking is on GitHub Issues.**
> See: https://github.com/VFPC/VFPC/issues
> Cross-repo board: GitHub Project "vFPC Development Tracker" (VFPC org).

---

## Current State

**Branch:** `main`  
**Version:** `3.7.2.0`  
**Status:** production plugin released; PR `#181` envelope compatibility is on `main`; PR `#182` is merged and the `#174` plugin fix is shipped.

### Current Branch Picture

- `main` — clean, current default branch
- `fix/174-sidwide-type-exclusion-rebased` — merged release branch; pruned after `v3.7.2.0`
- `Research-reported-issues` — Peter's separate refactor branch; keep preserved, do not merge into production

### What's Working ✅

- **Production plugin** — v3.7.2.0 released and in use
- **Time-handling patch** — the earlier 5-bug `TimeWindow.hpp` fix set is already part of the shipped plugin
- **Envelope-compatible runtime tests** — PR `#181` merged; `RuntimeConstraintTests` now accepts both legacy array `out.json` and the `{cycle,airports}` envelope
- **#174 fix shipped** — PR `#182` merged the rebased `sidwide=false` fix plus `SidApplicability.hpp`
- **Tests verified** — `VFPC_Tests` passed **94/94** on the held `#182` branch before merge; the release DLL then built successfully for `Release|x86`

### Key Open Items

| Issue | Title | Current status |
|-------|-------|----------------|
| #174 | `sidwide=false` overrides constraint results | Fixed in merged PR `#182`; shipped in `v3.7.2.0` |
| #165 | BST/GMT timezone offset in time comparisons | Open |
| #169 | Replace server-polled UTC with `GetSystemTime()` | Open |
| #170 | Evaluate time restrictions against EOBT | Blocked on #169 |
| #171 | Respect `override` field to suppress restrictions | Blocked on wider data/API rollout |

### Recent Closures

- `#177` closed via merged PR `#181`
- Earlier blocker narrative from SRDData `#195` / New-SRDParser `#58` is no longer active:
  `#58` was closed as parser-correct, and `#195` was closed as stale after re-checking the live AIRAC 2603 desktop data

---

## Build Baselines

| Item | Value |
|------|-------|
| Language standard | C++17 (`VFPC_Tests`), C++14 (`VFPC.dll`) |
| DLL target platform | Win32 |
| Test target platform | x64 |
| Toolset | v143 (VS 2022) |
| Test framework | Google Test v1.15.2 |
| Tests | 94 passing, 0 failing on the held `#182` branch before merge |
| Version | 3.7.2.0 |

---

## Immediate Guidance

1. Treat `main` + tag `v3.7.2.0` as the current production baseline.
2. Leave `Research-reported-issues` untouched; it is a preserved rewrite branch, not a cleanup target.
3. Treat issue `#174` and the old `fix/174...` branch as historical only; both release-side cleanup actions are complete.

---

## Key Design Notes

1. **`SidApplicability.hpp`** is now the clean standalone expression of the #174 applicability rule and is useful for any future rewrite work.
2. **`checkTimeWindow()`** remains the pure, testable entry point for time-window logic.
3. **`RuntimeConstraintTests`** are once again meaningful on current parser output because PR `#181` taught the loader to accept the envelope format.
