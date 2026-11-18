## What this controls

How the picture reaches the monitor: which graphics backend and card are used, whether V-Sync and antialiasing are on, whether map details are drawn, whether the top-right shows FPS, plus two independent pacing caps — the render frame rate and the client update rate. Window mode and resolution are not here — pick them in "Display mode".

## In game

- "V-Sync" off: tearing on fast horizontal movement, but input feels more direct; on: complete picture, slightly higher latency.
- "High Detail" off: decorative map layers stop drawing, some map sounds stop, the picture empties, FPS usually rises a little.
- "Render frame rate limit" caps how many frames are drawn per second when V-Sync is off. It does not change how often the game logic runs; 0 is uncapped.
- "Client update rate" caps how often game logic and input are processed. Rendering follows this beat unless the render cap is lower, so lowering it also lowers the frame rate. 0 is uncapped and is the default.
- "Cap update rate to save power" is a shortcut: checked sets the update rate to 480 Hz, unchecked returns it to 0. It adds input latency, so leave it off while racing.
- "Show FPS" adds a top-right frame counter — see which section drops frames while running maps.

Two caps, one rule: the render cap only matters when it is **lower** than the update rate (or the update rate is 0). If the render cap is set equal to or above the update rate, the client ignores it and renders on the update beat.

## How to tune it

| Item | When it takes effect |
| --- | --- |
| V-Sync, High Detail, Show FPS, render frame cap, client update rate | Immediately |
| FSAA samples, Graphics mode, Graphics card | Client restart |

For restart items, a notice and a "Restart" button appear atop the settings page; while online, clicking asks you first.

For stutter: antialiasing off first, then V-Sync, frame cap last. For jagged edges, raise antialiasing alone. To cut CPU/GPU usage without hurting input, lower the **render** cap, not the update rate.

## Watch out

- Neither cap is the monitor refresh rate; that is picked in "Display mode".
- Antialiasing is a target multiple; unsupported GPUs retry at 4x, then the client disables it entirely.
- "Graphics mode": Performance mode prefers Vulkan, Compatibility mode falls back to OpenGL. Always restart after changing it.
- A discrete GPU missing from the dropdown means the backend did not enumerate it; on Windows, assign the client via "System graphics settings", then restart.
- "Current GPU", "Current driver" and "Current graphics backend" only show what is in use; they cannot be changed.

## Related

- [Display mode](deck:graphics-modes)
- [Visual effects](deck:graphics-visual)
- [Client](deck:general-client)
