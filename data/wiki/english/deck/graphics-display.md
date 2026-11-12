## What this controls

How the picture reaches the monitor: which graphics backend and card are used, whether V-Sync and antialiasing are on, whether map details are drawn, whether the top-right shows FPS, and the render frame cap. Window mode and resolution are not here — pick them in "Display mode".

## In game

- "V-Sync" off: tearing on fast horizontal movement, but input feels more direct; on: complete picture, slightly higher latency.
- "High Detail" off: decorative map layers stop drawing, some map sounds stop, the picture empties, FPS usually rises a little.
- "Refresh Rate" below the monitor's rate stutters visibly; default 0 is uncapped.
- "Show FPS" adds a top-right frame counter — see which section drops frames while running maps.

## How to tune it

| Item | When it takes effect |
| --- | --- |
| V-Sync, High Detail, Show FPS, Refresh Rate | Immediately |
| FSAA samples, Graphics mode, Graphics card | Client restart |

For restart items, a notice and a "Restart" button appear atop the settings page; while online, clicking asks you first.

For stutter: antialiasing off first, then V-Sync, frame cap last. For jagged edges, raise antialiasing alone.

## Watch out

- "Refresh Rate" caps the client's drawn frames per second, not the monitor refresh rate; that is picked in "Display mode".
- Antialiasing is a target multiple; unsupported GPUs retry at 4x, then the client disables it entirely.
- "Graphics mode": Performance mode prefers Vulkan, Compatibility mode falls back to OpenGL. Always restart after changing it.
- A discrete GPU missing from the dropdown means the backend did not enumerate it; on Windows, assign the client via "System graphics settings", then restart.
- "Current GPU", "Current driver" and "Current graphics backend" only show what is in use; they cannot be changed.

## Related

- [Display mode](deck:graphics-modes)
- [Visual effects](deck:graphics-visual)
