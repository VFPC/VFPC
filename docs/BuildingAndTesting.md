# Building and Testing VFPC

---

## Prerequisites

- **Visual Studio 2022** with the **"Desktop development with C++"** workload installed.
  (Open Visual Studio Installer → Modify → select "Desktop development with C++".)
- **Git** with submodule support.
- No other runtime dependencies are required — the EuroScope SDK headers and all library
  headers are checked into the repo under `lib/`.

---

## Building the Plugin DLL

Open `VFPC.sln` in Visual Studio and select **Build → Build Solution**, or from a
Developer Command Prompt:

```bat
cd C:\Users\...\VFPC
msbuild VFPC.sln /p:Configuration=Release /p:Platform=x64
```

The output is `x64\Release\VFPC.dll`. Copy this file into your EuroScope plugins folder.

---

## Building and Running the Test Project

### One-time setup — initialise the Google Test submodule

```bat
cd C:\Users\...\VFPC
git submodule update --init --recursive
```

This populates `VFPC_Tests\third_party\googletest\`.

### Build the tests

Always target the solution file (not the `.vcxproj` directly), so that `$(SolutionDir)`
resolves correctly:

```bat
msbuild VFPC.sln /t:VFPC_Tests /p:Configuration=Debug /p:Platform=x64
```

### Run the tests

```bat
VFPC_Tests\x64\Debug\VFPC_Tests.exe
```

Expected output:

```
[==========] Running 52 tests from 7 test suites.
[----------] ...
[  PASSED  ] 52 tests.
```

### From Visual Studio

1. Set `VFPC_Tests` as the startup project.
2. Select **Debug → Start Without Debugging** (or press Ctrl+F5).

---

## Test Coverage

The test suite covers `src/TimeWindow.hpp::checkTimeWindow()`, the pure time-window logic
extracted from `checkRestriction()`.

| Test group | Cases | What is tested |
|-----------|-------|----------------|
| O01 — cross-midnight wrap | 8 | Monday 22:00 – Tuesday 06:00 window |
| O02 — same-day window | 8 | Monday 08:00 – 17:00 window |
| O03 — adjacent overnight | 8 | Monday night into Tuesday morning |
| O04 — date-only (no times) | 4 | Monday restriction with no time bounds |
| O05 — time-only (no days) | 8 | 08:00 – 17:00 with no day bounds |
| O06 — midnight-crossing time-only | 8 | 22:00 – 06:00 with no day bounds |
| O07 — edge cases | 8 | Boundary-on-the-dot (inclusive end), empty restriction |

All 52 tests pass on commit `01da6ef8`.

---

## Development Notes

### Adding new tests

Test cases live in `VFPC_Tests/TimeWindowTests.cpp`. Each case is a struct:

```cpp
{
    "O08a",                         // unique ID for failure messages
    R"({"startday":0,"endday":4,"starttime":[8,0],"endtime":[17,0]})",
    2,   // currentDay  (Wednesday = 2)
    12,  // currentHour
    0,   // currentMin
    true // expected
}
```

Add new entries to the appropriate `INSTANTIATE_TEST_SUITE_P` block and rebuild.

### Adding new testable logic

1. Extract the logic to a free function in a new `src/XYZ.hpp` header with no EuroScope
   dependencies.
2. `#include` the header in `VFPC_Tests/TimeWindowTests.cpp` (or create a new `XYZTests.cpp`).
3. Add the new `.cpp` to `VFPC_Tests.vcxproj` under `<ItemGroup><ClCompile>`.

---

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `Microsoft.Cpp.Default.props not found` | C++ workload not installed | Install "Desktop development with C++" via VS Installer |
| `TimeWindow.hpp not found` | Building `.vcxproj` directly | Use `msbuild VFPC.sln /t:VFPC_Tests` |
| `C2672 — cannot deduce template argument` | MSVC 2022 and `rapidjson::Parse()` | Use `Parse<rapidjson::kParseDefaultFlags>(json)` |
| LNK2019 on `GenericValue` | Returning `rapidjson::Document` by value | Parse JSON inline; do not return Document by value |
| Test output is blank / EXE not found | Build output directory mismatch | Check `VFPC_Tests\x64\Debug\` — binary is named `VFPC_Tests.exe` |
