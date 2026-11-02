## What this controls

A small HUD panel listing how many times each teammate has drowned. Pure local statistics: it changes no detection and no scoring, and other clients' numbers may differ from yours.

## In game

The panel is titled "Drown deaths", one player per line, highest counts on top, ties sorted by ID. Not every freeze counts: only being rescued and hooking out again before freezing once more adds one, so staying stuck in the water never farms numbers.

## How to tune it

| Goal | Setting |
| --- | --- |
| Hide it | Turn off "Show Gores drown board" |
| See yourself in solo | Keep "Include Team 0" on |
| Fewer rows | "Players shown" (1–16); the rest collapse into "More teammates..." |
| Keep the view clear | "Card opacity" defaults to 80% and only dims the background and the Tee, not the text |
| No little Tees | Turn off "Show player Tee" |

Drag the position in the HUD editor.

## Watch out

- It only appears when the server declares Gores mode; toggling the Gores tool manually does not show it. With "Include Team 0" off while you are on team 0 yourself, the whole panel is not drawn.
- Each player's row resets to zero when they leave the start area; a new round of drownings counts up from zero again.
- Changing maps, disconnecting, and entering or leaving Gores mode clear the whole board; a teammate renaming, switching teams or leaving resets only their row.

## Related

- [Gores mode](qm:gores)
- [Gores actor](qm:gores_actor)
