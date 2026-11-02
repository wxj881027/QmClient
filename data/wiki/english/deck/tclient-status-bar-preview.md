## What this controls

Which items the status bar shows, in what order. The preview strip at the top is the editing canvas; dragging it reorders.

## In game

The bottom bar's numbers reorder the moment anything changes. Colors, height and font size are not handled here; those live in "Status bar settings".

## How to tune it

1. Pick a type in the dropdown, press "Add Item" to append it.
2. Click a cell on the strip to select it, then press "Remove Item".
3. Hold a cell and drag sideways to move it.
4. With a cell selected, changing the dropdown swaps in that type.

The "Status Scheme:" field holds the letter string itself; editing it writes the order directly, live as you type. "Apply" rewrites the list into a canonical letter string and clears the selection.

| Code | Meaning | Code | Meaning |
|---|---|---|---|
| a | Angle | p | Ping |
| c | Position | v | Velocity |
| r | Race Time | l | Local Time |
| f | FPS | s | Points |

Spaces are items too: each space is one gap, and remaining width splits evenly among them. "Status Bar Codes" at the bottom lists all 22 types.

## Watch out

- Unrecognized characters are skipped silently; if a copied string is missing items, check the letters.
- 128 items max; once full, "Add Item" does nothing.
- Each item takes its content's width: more items, smaller gaps; at zero they stick together. Trim items first if you need room.
- With no type selected in the dropdown, "Add Item" does nothing.
- Preview cells are all equal width: they show order only, never real widths.
- The "Points" item needs one online query; it shows "..." at first, "?" forever on failure.

## Related

- [Status bar settings](deck:tclient-status-bar-settings)
- [Tee status bar](tclient:tee-status-bar)
