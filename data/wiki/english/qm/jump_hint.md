## What this controls

A fixed text board drawn on the HUD, used as a cheat sheet: the text lives in the config, and while ticked the same content is drawn every frame. It reads no player positions and takes no part in collision.

## In game

The default text is the three-tile edge jump reference values (left jump .34|.31|.16 and the like). The text is capped at 512 bytes, and newlines are written as a literal `\n` in the config; when the text is empty, nothing is drawn while the tick stays on — check the text first when "nothing shows".

## How to tune it

1. Tick "Position jump hint", move it with "Horizontal position" and "Vertical position" (0–100%), and size it with "Font size" (1–50).
2. "Text color" changes the color.
3. In the HUD editor, double-click the text board to edit it in place, with reset, cancel and save; on save, line breaks convert back to `\n`.

## Watch out

- It shows the text from config, not live coordinates; for live coordinates see the "Show Coordinates" card.
- Position is a screen percentage; recheck after resolution or window aspect changes. With a large font or a spot near the edge, the board is clamped back on screen, so the actual position differs from the numbers you entered.

## Related

- [Edge jump assist](qm:hj_assist)
- [Show Coordinates](qm:coords)
