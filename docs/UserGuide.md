# VFPC User Guide

**UK VFPC (UK VATSIM Flight Plan Checker)** is a plugin for EuroScope that automatically
checks filed IFR flight plans against UK departure restrictions. It is intended as an aid
to Delivery controllers and does not replace the need to review each flight plan carefully.

---

## Contents

1. [Installation](#1-installation)
2. [Initial Setup in EuroScope](#2-initial-setup-in-euroscope)
3. [Understanding the Tag](#3-understanding-the-tag)
4. [Check Codes Reference](#4-check-codes-reference)
5. [Chat Commands](#5-chat-commands)
6. [Working with Specific Check Results](#6-working-with-specific-check-results)
7. [Known Limitations](#7-known-limitations)
8. [Getting Help](#8-getting-help)

---

## 1. Installation

1. **Download** the latest `VFPC.dll` from the Releases page on GitHub
   (https://github.com/VFPC/VFPC/releases).
2. **Copy** `VFPC.dll` into your EuroScope plugins folder
   (typically `C:\Users\...\EuroScope\Plugins\`).
3. **Keep your sector files and navigation data up to date.** The plugin relies on current
   navigation data. Update `EuroScope/DataFiles` and `EuroScope/UK/Data/DataFiles` to the
   current AIRAC (available from the `NavData` folder of the EDGG FULL profile at
   http://files.aero-nav.com/EDXX).
4. **Restart EuroScope** after updating sector files or navigation data.

---

## 2. Initial Setup in EuroScope

After loading the plugin you need to add a tag item to the Departure List.

1. In the Departure List, click the **`S`** button on the left of the list header.
2. Add a new item with these settings:

   | Setting | Value |
   |---------|-------|
   | Tag Item Type | `VFPC (UK)/VFPC` |
   | Mouse Button Function | `VFPC (UK)/Options` |
   | Recommended Header | `FPC` |
   | Recommended Item Width | `3` |
   | Align To Center | Off (unticked) |
   | Colour | `Default Other Item` |

3. Make sure the new tag item is **enabled** using the **`F`** menu on the left of the
   Departure List header.

The `FPC` column will now appear in your departure list with a colour-coded result for
each aircraft.

---

## 3. Understanding the Tag

The tag item shows a short code and is colour-coded:

| Colour | Meaning |
|--------|---------|
| **Green** | All checks passed |
| **Yellow** | All checks passed, but one or more warnings were generated |
| **Red** | One or more checks failed |

Click the tag to open the **Options** menu. From there you can view a detailed breakdown
of every check result for that aircraft.

---

## 4. Check Codes Reference

### Success

| Code | Meaning |
|------|---------|
| `OK!` | All checks passed (green or yellow depending on whether warnings exist) |

### Failure Codes

| Code | Check | What it means |
|------|-------|---------------|
| `SID` | SID validity | The assigned SID does not exist, has an incorrect suffix, or does not match the filed route |
| `ENG` | Engine type | The aircraft type is not permitted to use this SID or route |
| `DST` | Destination | The filed destination is not valid for this SID |
| `RTE` | Route validity | The filed initial route is not valid to the given destination |
| `LVL` | Altitude block | The filed altitude is outside the allocated FL window for this route |
| `OER` | Odd/Even rule | The filed altitude violates the ODD or EVEN altitude requirement for this route |
| `SUF` | SID suffix ban | The assigned SID suffix is banned on this route |
| `RST` | Day/Time restriction | The assigned SID or suffix is not valid at the current day and time |
| `CHK` | Syntax error | Invalid characters in the route, an incorrect step-climb instruction, or similar |
| `BAN` | SRD ban | The route has a ban attached (see below) |

### About `BAN`

A `BAN` result can have two causes:

1. **Destination restriction** — The route is only valid to certain destinations, but the
   pilot has filed to a different destination. The plugin shows a "banned" version of the
   route so the restriction is still visible. Alternative routes are also shown.
2. **SRD-imposed ban** — One of the following applies:
   - The route has been withdrawn until further notice.
   - **CDR2 route** — Plannable when NOTAMed available (may be used during events).
   - **CDR3 route** — Not plannable; available only with prior Area Control Supervisor
     approval (generally during events).

   Note: CDR1 routes (plannable at specific times only) are not `BAN`-coded. Their time
   restrictions are coded in the API and enforced automatically.

---

## 5. Chat Commands

All commands begin with `.vfpc` followed by an optional sub-command.

| Command | Effect |
|---------|--------|
| `.vfpc` | Displays the root help message |
| `.vfpc load` | Re-enables automatic data loading if it was disabled (e.g. after a lost server connection or a `.vfpc file` load) |
| `.vfpc debug` | Opens a separate "VFPC Log" message box with debug-level diagnostic output |
| `.vfpc file` | Disables API loading and performs a one-time load from the local `Sid.json` file. Can also be used to reload after editing `Sid.json`. |
| `.vfpc check` | Equivalent of clicking "Show Checks" for the aircraft currently highlighted in the Departure List |

> **Note:** Disabling API loading (via `.vfpc file` or due to a connection failure) only
> lasts until the plugin is reloaded. The next time EuroScope starts, the plugin will always
> attempt to load from the API.

---

## 6. Working with Specific Check Results

### SID (`SID`)

Check the assigned SID:
- Is it published for this airport?
- Does the SID suffix match what is expected for the filed route? (Some departure routes
  are only valid with a specific suffix, e.g. `CPT1F` vs `CPT1G`.)
- Does the route depart via the fix in the SID name?

### Level (`LVL`)

The filed altitude is outside the published FL range for this route. Ask the pilot to refile
at a level within the allocated block, or issue a conditional clearance if appropriate.

### Odd/Even (`OER`)

The track of the route requires an ODD or EVEN altitude and the pilot has filed the wrong
parity. Ask the pilot to refile at the correct semi-circular level.

### Day/Time restriction (`RST`)

The assigned SID or suffix is not available at the current time. Check the published
restrictions and advise the pilot if necessary.

### SRD Ban (`BAN`)

Review the detailed check output to understand which note is attached and whether the ban
applies to the specific situation (e.g. CDR2 routes may be allowed by event NOTAM).

---

## 7. Known Limitations

- **BST/GMT offset** — Time restriction checks currently use server UTC time. During
  British Summer Time (BST), the plugin is one hour behind. Work is in progress to fix
  this (issue #165). As a workaround, be aware that restrictions near the hour boundary
  may be off by one hour in summer.
- **EOBT-based restrictions** — Time restrictions are evaluated against the current
  session time, not the flight's Estimated Off-Block Time. This means a flight filed for
  a future departure slot may receive an incorrect result at the time of filing (issue #170).
- **Complex notes** — Some SRD notes involve conditions (e.g. danger area activation,
  special ATC procedures) that cannot be modelled automatically. These produce a yellow
  `OK!` warning rather than a red failure, and require manual judgement.
- **Inbound oceanic routes** — The plugin only validates departure routes with a UK ICAO
  departure airport. Inbound oceanic routes are not checked.

---

## 8. Getting Help

- **Discord:** https://discord.gg/xucfd2K523
- **GitHub Issues:** https://github.com/VFPC/VFPC/issues (for bugs and feature requests)
- **Disclaimer:** This plugin is maintained by volunteers and is provided as-is.
  The developers accept no liability for any issues encountered. Always apply your own
  judgement as a controller.
