## What this controls

A set of camera feel options: speed-based camera drift, dynamic FOV that widens with speed, instant zoom reverse, overall UI scale and an aspect ratio preset. It only changes camera and interface mapping, not controls.

## In game

- With "Enable camera drift" on, the faster you run horizontally the more the camera leans into your direction of movement, and it smoothly returns to center when you stop; while turning, the camera swings slightly at the tail.
- With "Enable dynamic FOV" on, the view widens while accelerating so you see further, and pulls back while decelerating. Both are most visible while picking up speed.
- A wider aspect ratio preset shows more to the sides and less top and bottom.

## How to tune it

1. "Drift intensity" defaults to 50 — lower it if the camera feels floaty; "Drift smoothness" defaults to 80, lower values make the camera follow tighter. "Drift direction" flips the offset to the opposite side.
2. "Dynamic FOV intensity" defaults to 50 — lower it if deformation near walls is clearly visible; smoothness 0 means instant change.
3. "Instant zoom reverse" defaults to on: when the zoom key is pressed in the opposite direction, the zoom answers immediately instead of waiting for the smooth transition; turn it off if you dislike it.
4. Pick an aspect ratio preset or "Custom" (100–300); after changing, read the current ratio shown at the bottom of the card.

## Watch out

- Drift and dynamic FOV only work while you drive your own Tee and the client has local prediction; they are absent while spectating or watching someone else. Not seeing the effect in a demo does not mean the setting is broken.
- Aspect ratio changes only the game view; the HUD and menus still follow the window ratio. With a large mismatch, positioning feels awkward.
- "UI scale" is a global percentage; the in-game HUD and menus scale together.

## Related

- [Zen mode](qm:focus_mode)
- [Spectator mode](qm:spectator_mode)
