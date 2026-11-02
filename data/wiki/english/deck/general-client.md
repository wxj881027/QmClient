## What this controls

A few client runtime settings and one entry block: the update rate, power saving, skipping the main menu, four buttons that open files or directories, and the menu background theme list.

## In game

"Update Rate" is the beat at which the client advances the game. Set 300 and it updates at most 300 times per second; input sampling and the picture both follow that beat. 0 means uncapped and is the default. Fill in less than your monitor's refresh rate and picture and feel go dull together.

Themes only swap the map and icons behind the main menu — invisible once in a match. Clicking one swaps immediately, no restart.

## How to tune it

1. Leave the rate at 0; to cut usage, fill a value near your monitor's refresh rate, never below the delay you can tolerate.
2. To save power, check "Save power by lowering update rate (higher input latency)" — it sets the value straight to 480; turn it off for racing and PvP.
3. If you play often, check "Skip the main menu" and you land on the page you last visited right after login.
4. Once a new theme is in the themes directory, it appears only after clicking "Refresh" above the list.

## Watch out

The power-saving item overrides hand-typed numbers: checked is 480, unchecked is 0 — your old 240 does not come back.

The rate also caps the frame rate: when render throttling is unset or set higher, the frame rate follows it, so a low value makes the picture itself stutter.

"Settings file" opens qmclient/settings.cfg, "Saves file" opens ddnet-saves.txt; "Config directory" opens the folder holding those files, "Themes directory" opens the themes folder and creates it first if missing.

When the window moves to the background, another beat kicks in (default 120); returning to the foreground jumps back to your value.

## Related

- [Config files](deck:tclient-info-files)
- [Profiles](deck:tclient-profiles)
