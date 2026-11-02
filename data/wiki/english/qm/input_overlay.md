## What this controls

A keyboard and mouse texture overlaid on the game view, lighting up with real key presses — an input display for stream viewers. It reads input and draws only; it never intercepts or rewrites keys.

## In game

Pressed keys switch to their pressed state on the texture. It draws both online and in replays, and hides while a menu or the scoreboard is open. The layout is read from `input_overlay.json` (OBS style), the file is checked for changes every 0.5 seconds and reloaded immediately; if parsing fails, the last working layout stays in use.

## How to tune it

1. Tick "Show inputs", then scale with "Keyboard size" and "Mouse size" (1–200%) separately; the mouse usually goes smaller.
2. Lower "Opacity" to calm the overlay, and move it into a corner with "Horizontal position" and "Vertical position" (0–100%); the default leans bottom right.
3. "Show key press counts" adds a counter on each key: you can change the key press count color, its font size (50–200%) and the prefix text ("Key press count label"); "Reset key press counts" zeroes everything.

## Watch out

- The overlay covers map and HUD, so start small before scaling up. Position is saved as screen percentages — recheck after changing resolution.
- A broken layout file makes the whole overlay disappear; the log carries the input_overlay parse errors. Keep a copy before editing.
- Counts only accumulate while the window is active and you are online: switching out, the editor, chat and the console do not count, held-key repeats do not count, replays never count — and only keys declared in the layout are counted.

## Related

- [Streamer mode](qm:streamer)
- [HUD settings](tclient:hud)
