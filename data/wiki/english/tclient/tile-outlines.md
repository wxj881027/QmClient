## What this controls

Outlines the outer edges of hazardous tiles in the map: hookable and unhookable walls, freeze, unfreeze, death and teleport tiles. Only the outer contour is drawn — adjacent tiles of the same kind have no line between them. Rendered locally only.

## In game

- It answers "can I stand here, will this freeze me, will my hook catch" — faster than trial and error on unfamiliar maps.
- The outlines are a drawn hint only; collision still follows the map as usual. Lines not lining up with the tile texture is normal — the line is the real boundary.
- Line width is in world units; zooming out makes the lines thinner with it.

## How to tune it

- "Show all enabled outlines" is the master switch (off by default); each type also needs its own checkbox on its color row before it appears.
- Five types each get their own color and width (1–16, default 2): Solid, Freeze, Unfreeze, Kill, Tele. "Deep freeze color" and "Deep unfreeze color" only recolor deep freeze/unfreeze; their toggle and width follow Freeze/Unfreeze.
- "Outline opacity" multiplies everything once more; walls additionally have their own "Solid tile outline opacity" level.
- With "Only show outlines in the entities layer" on, outlines only draw while the overlay entities layer is also on.
- If you only care about hookability, enable Solid alone; start at width 1–2 — thicker lines blur into a mush in dense tile work.

## Watch out

- Tile types are cached once when the map loads; the tele, game and front layers keep one type per tile, and layers written later override earlier ones.
- Decorative tiles in the entities layer are not included; only tiles in those three layers get outlines.

## Related

- [Hook collision guides](deck:appearance-hook-collision-main)
- [Collision hitboxes](qm:collision_hitbox)
