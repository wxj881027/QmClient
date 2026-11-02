## What this controls

A background brush: hold the bound key and the mouse leaves strokes on the map background, for marking jump spots or drawing routes. Strokes exist only on your machine; they never touch the map or hit registration, and nobody else sees them.

## In game

- Strokes are placed in world coordinates and stay pinned to the map; line width scales with camera zoom, so the on-screen thickness stays roughly constant as you zoom in and out.
- "Stroke fade time" at 0 keeps strokes forever; set a number of seconds and strokes fade out once they have been up long enough.
- Whatever line the erase key passes over is deleted as a whole.

## How to tune it

1. Bind a key to +bg_draw under "Draw where mouse is"; erasing has no key row on the card, bind +bg_draw_erase yourself in the key bindings settings.
2. Match "Color" to the map's base color, start "Width" at 3–5 and thicken if that is not enough.
3. Use a fade time of 10–60 seconds for temporary hints, 0 for permanent marks.
4. Strokes are saved per map automatically: they are read when you join a server or watch a replay, saved when you leave, and written to disk every 60 seconds after that.

## Watch out

- Drawing works only while connected to a server or watching a replay; in the main menu you can neither draw nor read strokes.
- Saves are keyed by map name plus map hash, so maps with the same name but different content each get their own copy. After switching maps the previous map's strokes are hidden; go back to the original map to read them.
- The item limit defaults to 128; past it, the oldest strokes drop first. The limit is not on the card — the only way to change it is the config qm_bg_draw_max_items; setting 0 stops strokes from being drawn at all.

## Related

- [Tile outlines](tclient:tile-outlines)
- [3D background](qm:background_3d)
