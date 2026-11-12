## What this controls

A dummy mini window in the top right corner of the HUD: it renders the scene of the Tee you are not controlling separately. Keys still act on your current Tee; the window only displays.

## In game

- It draws the dummy by default; after a dummy switch it swaps automatically to drawing the main Tee.
- Below 100% zoom the view is closer and details are clear; above 100% the range is wider and you can see the route.
- With "Only show when the other Tee is not on screen" ticked, the window collapses while the other Tee runs inside your main view and reappears when it leaves the screen.
- When no dummy is connected, the other Tee is not in game, or no snapshot has arrived yet, the window only shows a placeholder with the reason.

## How to tune it

| Setting | Effect | Good for |
|---|---|---|
| Size 50–80% | Small on screen, mostly outlines | Only confirming position |
| Size 150–200% | Actions clearly visible | Controlling both Tees in sync |
| Zoom below 100% | Closer view | Judging fine movement |
| Zoom above 100% | Wider range | Keeping the overview while travelling |

Size 50–200%, zoom 10–300%. The window sits at the top right by default and automatically moves down when a Dynamic Island or a frozen HUD is at the top; position and zoom can also be adjusted further in the HUD editor.

## Watch out

- It renders one whole extra scene every frame; if frames drop, shrink the size first instead of disabling the feature outright.
- Higher zoom values mean the view is further out — the opposite of "closer".
- When the graphics backend does not support this offscreen rendering, the window is left with a single message; switch backend or lower the resolution and try again.

## Related

- [Dummy controls](deck:controls-dummy)
- [HUD notifications](qm:hud_notifications)
