## What this controls

How the window is placed, plus a resolution, refresh rate and color depth combination picked from a list. Resolution can only be changed here.

## In game

- Changing the window mode takes effect immediately; the picture blanks for a moment and comes back.
- Changing the resolution rebuilds the picture once; in fullscreen it also switches the display mode.
- The same resolution occupies one row per refresh rate, and the row you pick decides how many times per second the picture updates.
- After resizing by dragging the window edges, no row is highlighted anymore, because the current size is not in the list.

## How to tune it

1. Settle the window mode first: for quick Alt+Tab use Windowed borderless or Desktop fullscreen; for the lowest latency use Fullscreen.
2. Pick the list entry matching your monitor's native aspect ratio, ideally the resolution the monitor advertises.
3. For the refresh rate, pick the one the monitor advertises; unfamiliar entries are easily rejected by the backend.

## Watch out

- The "Current" row is the size, refresh rate and color depth the client actually accepted. If you picked a list entry but this row did not change, the switch did not happen.
- With display scaling enabled by the system, the numbers on the "Current" row are multiplied by the scale factor; not matching the list numbers is normal.
- In windowed mode, changing the resolution only resizes the window and leaves the display alone.
- Switching out of Fullscreen tends to black out or hang; when something goes wrong, switch back to Windowed borderless first.
- This card handles display modes; the refresh rate in "Display" is the render frame cap — two different things.

## Related

- [Display](deck:graphics-display)
- [Camera and view](qm:camera_view)
