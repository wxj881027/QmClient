## What this controls

Draws normally invisible collision volumes as lines: death and freeze tiles in the terrain, collision bodies between Tees and between a Tee and tiles, plus the reach of pickups, the hammer, projectiles, lasers and the hook. It only adds one layer of lines; the collision itself is not changed at all.

## In game

It answers "why": why a hook that looked like it would land did not catch, why standing at this spot freezes you, why this angle cannot hammer anyone. Walking a practice map with it on beats reading a text guide.

Lines trace the outer outline of each volume, not the whole tile square. So a line not lining up with the map texture is normal — that line is the real boundary.

## How to tune it

After turning on "Show hitbox mode", every range below is its own toggle: Map danger border, Tee collision (Tee to Tee), Tee freeze probe (Tee to Freeze), Tee death probe, Pickup range, Hook interaction, Hammer interaction, Projectile / explosion range, Freeze projectile collision volume, Laser / shotgun interaction, Freeze laser collision volume.

Start with only the category you are practicing and turn everything else off. Past three categories the screen turns muddy and the line you actually need gets lost.

"Player range" decides whose volumes are drawn:

| Option | Draws | When to use |
| --- | --- | --- |
| Local only | The Tee you control | Practicing positions alone |
| Local + Dummy | You and your dummy | Running dummy maps |
| All players | Everyone in the room | Spectating; the fuller the room, the messier |

"Opacity" defaults to 80%; lower it when the map swallows the lines, rather than raising it until it covers the terrain.

## Watch out

Drawn only in online play and demo playback; nothing in the menus.

Recordings and exports carry no hitboxes by default: when HUD is disabled in the replay display, these lines stay out of the video too. To get them into the final cut, turn the HUD on in the replay display.

Zoomed far out, the screen edges lose their lines. That is not the map missing data — the client only draws the middle region to keep the frame rate up.

With "All players" on, there are a lot more lines. If frames drop, switch off the ranges you are not using before touching opacity.

## Related

- [HJ assist](qm:hj_assist)
- [Weapon trajectory](qm:weapon_trajectory)
