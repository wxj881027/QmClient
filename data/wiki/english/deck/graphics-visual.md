## What this controls

The interface's colors: interface surface, accent color, selected items, settings card background, buttons, input fields, expanded dropdown lists, secondary menus and the scoreboard — each adjustable in color and opacity. Plus body text color, background blur and card borders. It redraws only the interface, never the map or game world.

## In game

- The scoreboard shown while holding TAB takes its background color and transparency from "Scoreboard surface".
- "Enable backdrop blur" turns the area behind semi-transparent panels like the in-game chat box and Dynamic Island blurry, making the text on top easier to read.
- "Text color mode" picks white or black for all interface body text; Auto picks per panel darkness.
- With "Rainbow card titles" on, every settings card's title slowly cycles its color.

## How to tune it

1. Adjust "Interface surface" and "Settings card background" opacity until body text reads clearly.
2. Then "Interface accent color" and "Selected item color", keeping focus and selection apart.
3. Pick the blur level last.

| Blur mode | Character |
| --- | --- |
| Gaussian | Smoothest, highest GPU cost |
| Kawase | Cheapest, slightly blurry edges |
| Dual Kawase | Between the two, more pronounced (default) |

## Watch out

- At very low opacity the map underneath shows through and body text loses contrast fast; icons have an outline fallback, text does not.
- Without GPU or backend support, "Enable backdrop blur" is marked "Unavailable" and the toggle does nothing.
- "Text color mode" only affects text in the client interface, not player names or other in-game text.
- Card borders are off by default; on busy backgrounds they make card edges easier to see.
- Each row has a "Reset" button on the right restoring that item's default color and opacity — use it when you overdid it.

## Related

- [Icons](deck:graphics-icons)
- [HUD](deck:appearance-hud-main)
