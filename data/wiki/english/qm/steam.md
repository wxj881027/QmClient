## What this controls

A one-time external integration during startup: when the client starts, it brings up the Steam client so Steam can record playtime and online status.

## In game

With "Launch Steam automatically when starting externally" ticked, every client start first tries to bring Steam up (silent, in the background). Only when Steam is running and its interface initializes successfully can Steam friends see that you are playing, plus map and server info. If initialization fails — Steam not running, not logged in, account not authorized — the client runs as usual and the Steam side shows nothing.

## How to tune it

1. Tick "Launch Steam automatically when starting externally".
2. Check the status line: "Steam is running" means nothing to do; on "Steam is installed but not running", tick it and the next start will bring Steam up.
3. When auto detection finds nothing (status shows "Steam not found"), enter the location of steam.exe under "Steam client path (leave empty to detect automatically)", without launch arguments.
4. After moving your Steam installation, press "Detect Steam again", or clear the path to return to auto detection.

## Watch out

- It tries only once, at the moment the client starts; opening Steam mid-session does not log that playtime — restart the client.
- When a manual path is invalid, it falls back to auto detection and the status shows "Manual Steam path is invalid; using automatic detection".
- On some platforms the client cannot tell whether Steam is running and the status only shows "Steam is installed"; that does not mean launching will fail.

## Related

- [General client settings](deck:general-client)
- [Mini features](qm:mini_features)
