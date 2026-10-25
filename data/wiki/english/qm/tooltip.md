## What this controls

Background color, text color, font size and entrance animation of tooltips (the small info boxes that pop up when the mouse rests on a control). Only how these bubbles are drawn.

## In game

Any control with a description in the menus pops a bubble on hover: background and text follow your colors, font size scales with the settings. With "Tooltip bounce animation" on, the bubble fades in while scaling up from slightly smaller, over about 0.2 seconds; with it off, the bubble just appears — the same when the interface motion level is Off. The bubble prefers the area above the anchor, flips below when it does not fit, then sticks to either side, always on screen.

## How to tune it

1. Set "Tooltip background color" first so it separates from your current interface background; keep opacity above half.
2. Then set "Tooltip text color" with a clear light-dark gap against the background; dark background with light text is the safest.
3. Raise "Tooltip font size" from 14 in steps of two, watching the preview below as you go.
4. If the entrance feels flashy, turn off the bounce; to start over, press "Reset tooltip appearance", which resets all four items at once.

## Watch out

- Bubbles in the settings pages ignore this font size and animation: they use a fixed instant small-text style, so no visible reaction does not mean nothing was saved.
- When the background is too transparent or too close to the text color, text smears into the background; fix the background before picking a text color.
- The preview is a single line; long descriptions still wrap or get cut off. Do not judge long sentences by it.

## Related

- [Key binds](qm:key_binds)
- [Qm appearance preset](qm:appearance_preset)
