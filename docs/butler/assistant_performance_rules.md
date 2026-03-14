# Assistant Performance Rules — VFPC

**Purpose:** VFPC-specific rules learned from working sessions. Supplement the general rules
in the New-SRDParser repo with plugin-specific constraints.

---

## CRITICAL RULES

### Rule 1: Never recursively list directory trees on Go or C# repos

Go repos have `vendor/` directories; C# repos have `bin/`, `obj/`, and NuGet package caches.
Recursive `dir /s`, `Get-ChildItem -Recurse`, or `find` commands on those repos produce
output so large they crash Cursor. **Always use `Glob` with specific patterns or `Grep`
with a known search term.**

Bad:
```powershell
Get-ChildItem -Path "C:\...\UKVFPCAPI" -Recurse   # crashes Cursor
```

Good:
```powershell
# Find specific files
Glob("*.go", target="C:\...\UKVFPCAPI")
# Search for a term
Grep("override", path="C:\...\UKVFPCAPI", glob="*.go")
```

### Rule 2: The plugin cannot be tested without EuroScope — use VFPC_Tests instead

VFPC builds as a Windows DLL loaded by EuroScope. `CFlightPlan`, `CRadarTarget`, and all
other SDK types cannot be instantiated outside a running EuroScope session. Any logic that
can be extracted to a free function (no EuroScope dependencies) should be extracted and
tested via `VFPC_Tests`. Logic that cannot be extracted must be verified manually inside
EuroScope.

### Rule 3: Day-of-week convention — Monday = 0 everywhere in the data

The `in.json` schema, `out.json` / `Sid.json` output, and VFPC's internal `timedata[5]`
all use **Monday = 0, Sunday = 6**. The single exception is the live API's `version` endpoint,
which returns the current day as a raw system value (Sunday = 0). `versionCall()` converts
this with `(day + 6) % 7` before storing in `timedata[5]`. Do not add a second conversion
anywhere else.

### Rule 4: Restriction times are British local time, not UTC

The `time_of_day` field in the schema is documented as "British time, NOT UTC". The plugin
currently compares these times against UTC from the server, which is wrong by one hour during
BST. This is tracked in issue #165. Do not silently "fix" it by adjusting times — the correct
solution requires BST/GMT offset logic and is a separate tracked change.

### Rule 5: End-boundary semantics — inclusive (<=) throughout

All restriction time boundaries are inclusive at both ends. Start: `>=`, End: `<=`.
Production rules are authored ending on the 59th minute of an hour (e.g. `"0759"`) with the
expectation that 07:59 itself is inside the window. The `checkTimeWindow()` function in
`src/TimeWindow.hpp` enforces this consistently via the `beforeEnd()` lambda.

### Rule 6: The override field is a three-repo dependency

`"override": true` on a constraint suppresses all restriction checks. It exists in:
- `New-SRDParser` — reads from `in.json`, passes to `out.json` ✓
- `UKVFPCAPI` — **not stored in MongoDB** (issue UKVFPCAPI#68) ✗
- `VFPC` — **not read** (issue #171) ✗

Do not implement the VFPC side until the API side is fixed, or the field will never arrive
at the plugin.

### Rule 7: Build the test project via the solution file, not the vcxproj directly

`VFPC_Tests.vcxproj` uses `$(SolutionDir)` to locate `src/` and `lib/include/`. When
MSBuild targets the `.vcxproj` directly, `$(SolutionDir)` is not set and includes fail.
Always build via `VFPC.sln`:

```bat
msbuild VFPC.sln /t:VFPC_Tests /p:Configuration=Debug /p:Platform=x64
```

### Rule 8: PowerShell does not support heredoc syntax

`$(cat <<'EOF' ... EOF)` fails in PowerShell. Write multi-line content to a temp file
and use `--body-file` or `git commit -F` instead.

---

## EFFECTIVE PRACTICES

- Use `Grep` with a specific term before reading an entire file — VFPC's `analyzeFP.cpp`
  is ~3,000 lines and finding the right block is faster with a targeted search.
- When touching `checkRestriction()`, always run the full 52-case test suite afterwards.
- When filing issues across multiple repos, write body content to temp `.md` files and
  use `gh issue create --body-file` to avoid shell quoting failures.
