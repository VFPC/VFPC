# UK VFPC

UK VFPC (UK VATSIM Flight Plan Checker) is a plugin for EuroScope that checks filed IFR flight plans against pre-defined departure restrictions. It displays the results of such checks in a user-friendly format, with the option to output detailed results.

**N.B.** - This plugin does not negate or replace the need for Delivery controllers to thoroughly check each Flight Plan before issuing a clearance; nor will it provide a perfect solution to more serious issues (e.g. SID filed routes in completely the wrong direction).

## Community
You can join the community Discord Server at: https://discord.gg/xucfd2K523

## READ BEFORE USE
This plugin only works with the latest sector files and navigation data. Please ensure that your EuroScope sector files are always up-to-date.

Additionally, please regularly update your navigation data (`EuroScope/DataFiles` and `EuroScope/UK/Data/DataFiles`) to the latest AIRAC, found in the `NavData` folder of the `EDGG EDGG_FULL` profile at: http://files.aero-nav.com/EDXX.

After updating your sector files or navigation data, please remember to restart EuroScope before using the plugin.

## Initial Setup
- Load up the plugin
- Add a new Tag Item to the Departure List (Click the `S` on the left of the list header.)
    - Tag Item Type: `VFPC (UK)/VFPC`
    - Mouse Button Function: `VFPC (UK)/Options`
    - Recommended Header: `FPC`
    - Recommended Item Width: `3`
    - Align To Center : `Off` (Unticked)
    - Colour: `Default Other Item`
- Ensure that the new Tag Item is Enabled (In the `F` menu on the left of the Departure List header.)

## Features
- `VFPC` tag item: Shows check-result and allows output of detailed checking data.
- Tag function `Show Checks`: Outputs detailed checking data.
- Tag function `Toggle Checks`: Enables/Disables checks for individual flight plans.
- Checks validity of the filed initial route (to the extent specified by API - UK routes are as specified in the SRD).
- Checks that the aircraft type is valid for the filed SID.
- Checks that the filed initial route is valid to the given destination.
- Checks that the filed altitude follows any odd/even restrictions.
- Checks that the filed altitude is within the allocated altitude block for the filed route.
- Checks that the assigned SID is valid for the aircraft type operating the flight.
- Checks that the assigned SID is valid on the current day/time.
- Checks that there are no obvious syntax errors within the flight plan. (Invalid step climbs, Random symbol characters, etc.)
- Checks for any SRD warnings/bans regarding the specific route in use.

## Check Results

### Green - Success
- `OK!` - All checks passed.

### Yellow - Warning
- `OK!` - All checks passed but one or more warnings were generated. This generally occurs in cases where the SRD contains ambiguous notes, which can't be coded for automatic interpretation.

### Red - Fail
- `SID` - Assigned SID is invalid for some reason. (Not Found, Bad Suffix, Mismatch with Route, etc.)
- `ENG` - Engine type is invalid for this SID/route.
- `DST` - Filed destination is invalid for this SID.
- `RTE` - Filed route is invalid for some reason.
- `LVL` - Filed altitude is outside of the allocated altitude block for this route.
- `OER` - Filed altitude is in violation of the Odd/Even altitude requirement for this route.
- `SUF` - Assigned SID suffix is banned for this route.
- `RST` - Assigned SID (and suffix) is invalid for this aircraft type and/or for the current day and time.
- `CHK` - Some kind of syntax error (Bad characters in route, Invalid step climb instruction, etc.)
- `BAN` - Route has ban attached. This can be for one of 2 reasons
    - Failed Restriction
        - In cases where a destination restriction would cause the filed route to be ignored entirely (e.g. M145 filed by a non-Dublin inbound), a banned form of the route is provided, without the destination requirement. This allows the restriction to be displayed correctly. Alternative routes are also displayed.
    - SRD-Imposed Ban
        - Routes withdrawn until further notice
        - CDR2 Routes - Plannable when NOTAMed out (may be used during events).
        - CDR3 Routes - Not plannable. Available only with prior approval from the Area Control Supervisor (generally during events).
        - **N.B.** CDR1 Routes (those plannable at specific times only) are not included in this category. Instead, their published time restrictions are included in the standard API data set and will be enforced automatically. However, many CDR1 routes are also CDR3 and may be utilised during events, when officially "inactive".

## Chat Commands
- `.vfpc` - Root command. Must be placed before any of the below commands in order for them to run.
- `.vfpc load` - Attempts to reactivate automatic data loading if it has been disabled for some reason. (Server connection lost, loading data from file, etc.)
- `.vfpc debug` - Activates debug logging into a separate message box, named "VFPC Log"
- `.vfpc file`- Deactivates loading from the API, and conducts a one-time load from the `Sid.json` file instead. Can also be used to reload from `Sid.json` after making changes.
- `.vfpc check` - Equivalent of clicking the "Show Checks" button for an aircraft. Ensure that the aircraft in question is highlighted in the departure list.

**N.B.** Disabling automatic data loading (or choosing to load from a file) will only last until the plugin is unloaded (including when EuroScope is closed). When the plugin is next loaded, it will always attempt to load from the API.

## Configuration File
- Certain data required by the plugin is now held within a file `vfpc_config.json`.
- The plugin will create this file the first time it is loaded after being downloaded.  Thereafter a default will be created if it cannot be located within the same folder
as the plugin.
- Currently this file specifies the Data Server URL used by the plugin, and the RGB values for the RED, GREEN and YELLOW colours as used by the results of checks (see above).
- The Data Server URL should not be amended by the user, unless instructed to do so by a member of the VFPC team, as this is likely to cause the plugin to be disabled.
- The format of the JSON file must be maintained if amended by the user to avoid unexpected behaviour.
- The configuration file default values are as follows;
  
| Key | Value |
| --- | --- |
| `base_url` | https://vfpcplugin.org |
| `red` | R = 190 G = 0 B = 0 |
| `green` | R = 0 G = 190 B = 0 |
| `yellow` | R = 255 G = 165 B = 0 |


## Disclaimer
The plugin is currently in active development and you may encounter **unforseen bugs or other issues**. Please report them - we'll fix them as soon as we can. You run this plugin at your own risk - the developers are all volunteers and accept no liability for any problems encountered or damage to your system.

## Acknowledgements
This plugin was originally created by **@DrFreas** and significantly expanded by **@hpeter2** and **@svengcz**. **@lennycolton** took over development in January 2021 and has rewritten almost all of the plugin's code. However, a few elements of the original code base (most notably, part of the UI) are still in use.

The API was built from scratch by **@GeekPro101**, specifically for use with v3 of UK VFPC. The SRD Parser was written by **@lennycolton** and is used to provide regular AIRAC updates to the API. Both projects are closed source.
