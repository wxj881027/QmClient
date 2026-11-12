## What this controls

Colors and opacity of the chat completion list: four color groups — background, body text, selected text, selection background — plus a background opacity slider and a one-click reset. This card only handles styling; completion itself is controlled by the "Show command completion in chat" and "Chat emoji completion" toggles on the Function page.

## In game

When `/` pops up command candidates or `:` pops up emoji candidates, the rounded panel you see is the one you styled here: candidate names with a gray description, the selected row layered with the selection background color, and hovered rows using the same color one step more transparent.

## How to tune it

1. Set "Background color" first, then use "Background opacity" (0–100, default 96) to dial the panel down until the map behind stays readable.
2. Keep the body text color clearly apart from the background; make "Selected text color" one step brighter still.
3. Do not pick a "Selection background color" from the same family as the body text, or the highlighted row smears together.
4. Check the live preview below the card, then type something in game to confirm; "Reset chat completion appearance" reverts everything.

## Watch out

- The alpha channel inside "Background color" is overridden by the slider — panel transparency is decided by the slider alone, so changing the color's A channel appears to do nothing.
- With Gaussian blur enabled, a translucent panel gets an extra blur layer behind it; this is not free on low-end machines.
- Hovered rows use the "Selection background color" — one color shared by both places.

## Related

- [Chat bubble](qm:chat_bubble)
- [Console](qm:console)
