## What this controls

Where the in-match "entity layer background" comes from: a map, an image, a video, the current map, or nothing at all — plus two base colors. It is only visible once "Overlay entities" is on.

## In game

- With "Overlay entities" at 100, normal terrain is not drawn at all — only entity tiles remain, and the background becomes whatever you pick here.
- Whenever "Overlay entities" is not 0, the screen base color becomes "Entities background color"; it is what shows where the terrain fades out.
- But the background map, image or video actually draws only once "Overlay entities" reaches 100.
- With "Show tiles layers from BG map" on, the background map's own tile layers draw too, covering part of the entities.

## How to tune it

1. Push "Overlay entities" to 100 first, or nothing you change here shows.
2. Fill a map name under "Map", without maps/ or .map; the folder button on the right picks from local files.
3. Hard-code the name to see the same background every time; check "Use current map as background" to follow the current map.
4. Too bright or too busy — lower "Entities background color" to push the base color down.

## Watch out

- An empty "Map" means no entity background, only one flat color.
- An image or video filename is played as one; the first load hitches while the file is read.
- A wrong name gives no error, just no background. Fix the spelling first, then confirm the file really is in this client's storage.
- "Regular background color" is the base color when the entity layer is off; once it is on, "Entities background color" takes over. Each governs one case — change the right one.

## Related

- [Entity overlay](qm:entity_overlay)
- [Gameplay](deck:ddnet-gameplay)
