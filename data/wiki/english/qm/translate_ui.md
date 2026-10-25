## What this controls

Colors of the translate button in the chat input row: the button's background for when automatic outgoing translation is off and on, plus three menu color groups and a one-click reset. It does not change translation behavior.

## In game

The button takes its color from its state every frame: "Button - Enabled" while automatic outgoing translation is on, "Button - Disabled" otherwise. While disabled, hovering the mouse over it or opening the menu temporarily switches to built-in gray and built-in blue, which do not read these settings — move the mouse away to see your actual colors. Colors carry alpha, and changes apply on the next frame.

## How to tune it

1. Set "Button - Enabled" first — that is the state you see most of the time.
2. Then set "Button - Disabled", darker and more recessed.
3. Do not push alpha too low, or the chat box background bleeds through the button.
4. To start over, use "Reset translation colors", which clears all five items at once.

## Watch out

- "Menu background", "Menu option - selected" and "Menu option - normal" currently take no part in any drawing — changing them does not alter the menu's look; the language menu uses the UI theme's panel and option colors.
- "Reset translation colors" cannot keep only some items.

## Related

- [Translate](qm:translate)
- [Language](deck:general-language)
