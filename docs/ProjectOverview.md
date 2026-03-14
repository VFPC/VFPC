# VFPC Project Overview

**UK VFPC (UK VATSIM Flight Plan Checker)** is a C++ plugin for the EuroScope ATC client.
It validates IFR flight plans filed on the VATSIM network against pre-defined departure
restrictions drawn from the UK SRD (Standard Routes Document) and published at specific
UK airports.

---

## Purpose

When a pilot files a flight plan, EuroScope loads the plugin, which fetches restriction data
from the UKVFPC API and evaluates the filed plan against every relevant constraint. The result
is displayed as a colour-coded tag in the Departure List. A Delivery controller can click
the tag for a detailed breakdown.

The plugin is an aid to the controller, not a replacement for judgement. Many restrictions
involve nuance (e.g. CDR2 routes that become available by NOTAM) that the plugin cannot
fully model automatically.

---

## Data Pipeline

```
UK SRD (CSV)
    │
    ▼
New-SRDParser (C#)
    │  Parses routes, notes, restrictions, alerts
    │  Outputs out.json
    ▼
UKVFPCAPI (Go)
    │  Serves Sid.json via REST API
    ▼
VFPC plugin (C++, this repo)
    │  Fetches Sid.json on EuroScope startup
    │  Evaluates each flight plan on demand
    ▼
Departure List tag item in EuroScope
```

---

## What the Plugin Checks

| Code | Check | Description |
|------|-------|-------------|
| — | **Route** | Filed initial route valid for the departure SID |
| `SID` | **SID validity** | SID exists, suffix is correct, matches route |
| `ENG` | **Engine type** | Aircraft type permitted for this SID/route |
| `DST` | **Destination** | Destination is valid for this SID |
| `RTE` | **Route validity** | Route valid to the given destination |
| `LVL` | **Altitude block** | Filed altitude inside SID/route FL window |
| `OER` | **Odd/Even rule** | Altitude follows ODD/EVEN direction rule |
| `SUF` | **SID suffix ban** | Suffix not banned for this route |
| `RST` | **Day/Time restriction** | SID valid on current day and time |
| `CHK` | **Syntax** | No invalid characters, bad step climbs, etc. |
| `BAN` | **SRD ban** | Route withdrawn, CDR2/CDR3, or similar SRD ban |

---

## Architecture

The plugin is a single DLL (`VFPC.dll`) loaded by EuroScope via its plugin SDK. Key source files:

| File | Role |
|------|------|
| `src/VFPC.cpp` | Plugin registration, EuroScope callbacks |
| `src/analyzeFP.cpp` | Core flight plan analysis logic (`checkRestriction`, etc.) |
| `src/TimeWindow.hpp` | Testable free function `checkTimeWindow()` |
| `src/curl/` | HTTP client for API fetch |
| `lib/` | EuroScope SDK headers, RapidJSON, other libraries |

### Dependency Injection Boundary

Everything in `analyzeFP.cpp` that touches `CFlightPlan` or other EuroScope types cannot
be unit-tested without a running EuroScope session. Logic that depends only on pure data
(time windows, day-of-week checks) is extracted into free functions in separate headers
(e.g. `TimeWindow.hpp`) and tested via the `VFPC_Tests` project.

---

## Test Infrastructure

```
VFPC_Tests/
├── TimeWindowTests.cpp          — 52 parameterized cases for checkTimeWindow()
├── VFPC_Tests.vcxproj           — standalone test executable (x64, C++17)
└── third_party/googletest/      — Google Test v1.15.2 (git submodule)
```

Build command:
```bat
msbuild VFPC.sln /t:VFPC_Tests /p:Configuration=Debug /p:Platform=x64
```

Run:
```bat
VFPC_Tests\x64\Debug\VFPC_Tests.exe
```

---

## Key Conventions

### Day-of-week

All data (in.json, Sid.json, VFPC internal) uses **Monday = 0, Sunday = 6**. The only
conversion point is in `versionCall()`:

```cpp
int currentDay = (systemTime.wDayOfWeek + 6) % 7;   // Sunday=0 → Monday=0
```

Do not add a second conversion elsewhere.

### Time values

Restriction times are stored in **British local time** (GMT/BST), not UTC. The plugin
currently compares against server UTC, which causes a one-hour error during BST. This is
tracked in issue #165.

### End-boundary

All restriction time boundaries are **inclusive at both ends** — `>=` start, `<=` end.
Production rules end at XX:59 by convention. The `beforeEnd()` lambda in `checkTimeWindow()`
uses `<=`.

### Override field

`"override": true` on a constraint suppresses all restriction checks for that constraint.
This field is not currently stored in the API database (UKVFPCAPI#68) and is therefore not
available to the plugin (VFPC#171). Implementation is a three-repo coordinated change.
