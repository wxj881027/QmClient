## What this controls

The old entry point for font settings and in-game cursor scaling. Font features now live on the Font card and cursor scaling on the Visual: Cursor card; this card still exists and opens onto the font card.

## In game

- Fonts come in three slots — English font, Chinese font, icon symbols — each configured independently, and menu, HUD and chat text all switch together.
- With the Chinese slot unset, Chinese text falls back to the default font chain. Once the English font is swapped to one without Chinese glyphs, Chinese drops back to the default too, which can look like the setting "not working".
- The Chinese font dropdown only lists fonts that actually contain CJK glyphs; a purely Latin font picked there still cannot render Han characters.
- "Ingame cursor scale" scales the in-game weapon-following crosshair directly by percentage; at 0% it is not drawn.

## How to tune it

- Only want a different English look: pick in the English font slot and let Chinese keep falling back to the default.
- Blurry or missing Chinese: pick a CJK-capable font in the Chinese font slot, then set CJK font weight separately.
- The weight slider only appears for variable fonts; fixed-weight fonts get a dropdown of discrete styles instead.
- Start the cursor at 100–150%; past 200% it starts covering your landing spot.

## Watch out

- Cursor scaling does not affect the mouse pointer in menus.

## Related

- [Font](tclient:font)
- [Language](deck:general-language)
