## What this controls

Client-level switches and shortcuts: skipping the main menu, community map tags, showing pending crash reports on startup, four buttons that open the settings/saves files and their directories, and the menu background theme list.

## In game

Themes only swap the map and icons behind the main menu — invisible once in a match. Clicking one swaps immediately, no restart.

## How to tune it

1. If you play often, check "Skip the main menu" and you land on the page you last visited right after login.
2. Once a new theme is in the themes directory, it appears only after clicking "Refresh" above the list.
3. "Settings file" opens qmclient/settings.cfg, "Saves file" opens ddnet-saves.txt; "Config directory" opens the folder holding those files, "Themes directory" opens the themes folder and creates it first if missing.

## Watch out

The update rate, the power-saving switch and the render frame cap are no longer here — they moved to "Graphics display", next to the rendering settings where the difference between the two caps is explained.

When the window moves to the background, the background frame rate from the Graphics display page takes over: it follows the monitor refresh rate by default, can use a custom cap, or stay unlimited; returning to the foreground jumps back to your update rate.

## Related

- [Graphics display](deck:graphics-display)
- [Config files](deck:tclient-info-files)
- [Profiles](deck:tclient-profiles)
