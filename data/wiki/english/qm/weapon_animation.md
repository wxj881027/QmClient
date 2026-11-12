## What this controls

The motion layer on weapon sprites: weapons slide out and swing back when you switch, guns do a full flip while reloading, and it also decides whether the hammer rotates with the cursor. It does not touch fire rate, switch speed or hits.

## In game

The instant you switch, the weapon slides in from the side while rotating; duration, distance and angle are all adjustable. During the reload time after a shot, the gun does one flip, played only on shotgun, grenade and laser.

## How to tune it

1. Turn on "Weapon switch animation"; start from "Weapon switch duration" 200–300ms, "Weapon switch distance" 30–40 and "Weapon switch rotation" 360°.
2. For the reload flip, also enable "Play a flip animation while reloading weapons"; "Weapon reload animation probability" defaults to 100%, and keeping it there makes the flip easiest to see.
3. If switching covers the cursor, shrink "Weapon switch distance" and "Weapon switch rotation" instead of only cutting the duration.
4. "Weapon switch easing" sets the pacing of the movement; Elastic back is the most attention-grabbing.
5. "Hammer Mode" is independent of both toggles: to keep switch animations as usual but stop the hammer following the cursor, leave it on "Normal".

"Animation range" decides whose weapons animate: Self only / Local (you and your dummy) / All players. Switching and reloading share this one range.

## Watch out

- With both animations off, the duration rows collapse, but "Hammer Mode" still applies.
- The reload flip only plays when the reload delay has not been tuned too long (no more than 1.5x the default); maps that tune weapons may never show it.
- In Gores with "Skip switch animation when hammering" on, hammer-gun cycling skips the switch animation.

## Related

- [Weapon trajectory](qm:weapon_trajectory)
- [Weapon](deck:controls-weapon)
