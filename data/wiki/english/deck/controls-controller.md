## What this controls

The controller takeover switch, two sensitivity values, dead zone and axis mapping. Controller and mouse both write position to the same crosshair, only through different doors.

## In game

Relative mode: the further you push the stick, the faster the crosshair moves; it stops when you recenter.

Absolute mode: the crosshair is glued to the stick — direction sets the angle, push distance sets how far from the Tee, at most out to the "Maximum cursor distance" ring; recentering into the dead zone freezes it where it is, no snap back to center. This mode ignores sensitivity, so the "Ingame controller sens." row disappears while Absolute is selected.

"UI controller sens." is a separate value, only for pushing the pointer and scrolling lists with the stick in menus and console.

## How to tune it

1. Tick "Enable controller". Without a gamepad you only see "No controller found. Plug in a controller." and the rest stays hidden; connected, the line becomes the pad's name, selectable when several are plugged in.
2. Stay on "Relative" first and tune "Ingame controller sens.", working up or down from 100 as the baseline.
3. For point-where-you-aim control switch to "Absolute"; a full push reaches the "Maximum cursor distance" ring — its size is changed on the Mouse card.
4. If the crosshair still drifts after you let go, add "Controller jitter tolerance": default 5, maximum 50, and the higher it is, the further you must push to count as movement.
5. If the stick does nothing or is inverted, use "Aim bind" to point X and Y at the two axes that actually move.

## Watch out

Absolute mode only takes over in game with a character and not while spectating; the moment you enter spectator it falls back to relative and sensitivity applies again.

When the window loses focus or input is not grabbed, the controller is unresponsive — click back into the game window first.

The two aim slots never point at the same axis: assign to X an axis already used by Y, and Y is moved to X's previous axis.

## Related

- [Mouse](deck:controls-mouse)
- [Movement](deck:controls-movement)
