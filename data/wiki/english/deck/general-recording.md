## What this controls

Toggles and keep-counts for four automatic file types — demos, the game-over screenshot, the statboard screenshot and the statboard CSV — plus save directories for three kinds of media and the screenshot watermark.

## In game

A demo starts a new file when each game begins; a map change closes the old one first. They land in demos/auto, named after the map and the time.

The statboard screenshot and CSV are written about 3 seconds after the game ends, when the statboard appears. The game-over screenshot is only taken when the server announces the round end — map-running servers usually never reach that state.

With "Automatically add a watermark to screenshots" on, as long as any of the items below has content, every game screenshot gets one corner stamped with a translucent black strip of white text: timestamp, map name and your custom text. Which items are used is decided by three toggles.

## How to tune it

1. Enable a type first, then set its "Max demos" or "Max Screenshots"; the count field only appears while the toggle is on.
2. The numbers are how many copies are kept; older ones beyond that are deleted, and only automatically generated files are deleted.
3. To relocate files, fill in "Demo directory", "Video directory" or "Screenshot directory"; leave empty for the default.

## Watch out

The caps only clear automatic files. Manually saved demos and screenshots do not count against the quota and are never deleted.

A wrong directory does not fall back to the default: demo saving reports a failure instead of storing somewhere else. Relative paths start from the config directory; anything containing .. is judged invalid.

The statboard CSV is written under screenshots/auto. It does not follow "Screenshot directory", and "Max CSVs" cannot delete it, so it keeps piling up.

## Related

- [Demos](deck:ddnet-demo)
- [Client](deck:general-client)
