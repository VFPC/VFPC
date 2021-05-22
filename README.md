# VFPC

UK VFPC (UK VATSIM Flight Plan Checker) is a plugin for EuroScope that checks filed flight IFR plans against pre-defined departure restrictions. It displays the results of such checks in a user-friendly format, with the option to output detailed results.

**N.B.** - This plugin does negate or replace the need for Delivery controllers to thoroughly check each Flight Plan before issuing a clearance; nor will it provide a perfect solution to more serious issues (e.g. SID filed routes in completely the wrong direction).

## Features:
- `VFPC` tag item: Shows check-result and allows output of detailed checking data.
- Tag function 'Show Checks': Outputs detailed checking data.
- Checks validity of the filed initial route (to the extent specified by API - can be varied on a per-facility basis by local management).
- Checks that the aircraft type is valid for the filed SID.
- Checks that the filed initial route is valid to the given destination.
- Checks that the filed altitude follows any odd/even restrictions.
- Checks that the filed altitude is within the allocated altitude block for the filed route.
- Checks that there are no obvious syntax errors within the flight plan. (Invalid step climbs, Random symbol characters, etc.)

## Check Results

### Green
- `OK!`- All checks passed.

### Red
- `SID` - Assigned SID is invalid for some reason. (Not Set, Not Found, Bad Suffix, Mismatch with Route, etc.)
- `ENG` - Engine type is invalid for this SID/route.
- `DST` - Filed destination is invalid for this SID.
- `RTE` - Filed route is invalid for some reason.
- `NAV` - Navigation performance is invalid for this SID/route.
- Alternating `MIN` and `MAX` - Filed altitude is outside of the allocated altitude block for this route.
- `DIR` - Filed altitude is in violation of the Odd/Even altitude requirement for this route.
- `CHK` - Some kind of syntax error - (Bad characters in route, Invalid step climb instruction, etc.)

## Initial Setup:
- Load up the plugin
- Add a new Tag Item to the Departure List with the VFPC Tag Type & Function - Recommended item width of 3.

## Chat Commands:
`.vfpc` - Root command. Must be placed before any of the below commands in order for them to run.
`reload` - Attempts to reactivate automatic data loading if it has been disabled for some reason. (Server connection lost, etc.)
`debug` - Activates debug logging into a separate message box, named "VFPC Log"
`check <callsign>` - Equivalent of clicking the "Show Checks" button for an aircraft. Replace `<callsign>` with the logon callsign of the aircraft.

## Disclaimer
The plugin is currently in active development and you may encounter **unforseen bugs or other issues**. Please report them - we'll fix them as soon as we can. You run this plugin at your own risk - the developers are all volunteers and accept no liability for any problems or damage to your system.
