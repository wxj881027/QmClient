## What this controls

Per-category opacity for map entity tiles: death, freeze, unfreeze, deep freeze, deep unfreeze, teleport, CP, switch — one 0–100% slider each. It changes only what your client draws, not tile collision. "Freeze tile color" tints freeze, deep freeze and live freeze tiles; the opacity sliders still control how visible they are.

## In game

- A category set to 0 is not drawn at all; 100 means no extra fading for that category, and the global "Overlay entities" value still multiplies on top.
- The numbers on teleport tiles, checkpoint numbers and switch numbers fade together with their own opacity.
- Touching them still kills and still freezes — the collision has not changed one bit.

## How to tune it

| Layer | Suggestion | Reason |
|---|---|---|
| Death / Freeze | 60–80% | They block sight the most; faded, you can see the Tee behind |
| Unfreeze / Deep unfreeze | Keep in sync with freeze | The three are counted separately; tuning one alone looks patchy |
| Teleport / CP / Switch | Keep 100% | Missing them costs too much, and the edges must stay exact |
| Overlay entities | Only when you want the whole scene to recede | Terrain and entity tiles fade together; the per-category sliders scale their own category on top |

The last one, "Overlay entities", is DDNet's own setting: it dims the normal terrain and map image layers and scales entity tiles by the same percentage. Pushed to 100 it hides the entire terrain, leaving only entity tiles pressed onto the screen, and positions become easy to misread while running.

## Watch out

- Deep freeze and deep unfreeze are independent categories; tuning "Freeze opacity" alone does not affect them.
- Teleport and CP tiles set too faint are easy to miss at the critical moment.
- Once entity tiles fade out, the terrain textures underneath show through — don't take the showing-through patterns for new tiles.

## Related

- [Collision hitboxes](qm:collision_hitbox)
- [Tile outlines](tclient:tile-outlines)
