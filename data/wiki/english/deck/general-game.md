## What this controls

Four toggles: "Dynamic Camera" and "Smooth Dynamic Camera", plus "Switch weapon on pickup" and "Switch weapon when out of ammo". The first two only change your own camera; the last two only the weapon-switch requests the client sends.

## In game

With Dynamic Camera on, the camera leans toward the cursor: inside the dead zone (default 300) it never moves; beyond it, each extra pixel of cursor distance shifts the camera by 0.6 pixels. The cursor distance cap also rises from 400 to 1000, so your crosshair reaches farther.

Smooth Dynamic Camera adds slow following, and fast mouse flicks get dragged again by the stabilizing term — it looks floaty.

Empty weapons switch in laser, grenade, shotgun, pistol order, to whichever still has ammo — so an empty grenade launcher goes to the laser.

## How to tune it

Precision maps, motion sickness, or aim that must follow your hand: turn off both camera toggles.

Map running: turn off "Switch weapon on pickup" and keep "Switch weapon when out of ammo" — guns under your feet no longer replace the one in your hand.

## Watch out

When the follow ratio is greater than 0 elsewhere, Dynamic Camera still shows as ticked, and clicking it clears that ratio to zero too.

Turning smoothing off only zeroes the smoothness; the stability value stays, so it can still feel draggy.

The auto-switcher estimates ammo locally: picking up a gun records 10 rounds, and the real value updates only once you switch to it — you may land on an actually empty gun.

In infinite-ammo modes there is no auto-switching.

## Related

- [Camera and view](qm:camera_view)
- [Spectator modes](qm:spectator_mode)
