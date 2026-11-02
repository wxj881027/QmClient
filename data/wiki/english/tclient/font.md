## What this controls

Where UI text gets its glyphs: "English font:", "Chinese font:" and "Icon symbols:" are three slots, each holding one font, plus two weight controls. It changes how glyphs are drawn, not the layout logic.

## In game

- Digits, buttons and Latin characters in chat use the English font; Chinese characters use the Chinese font, leaving it empty follows the English font; symbol icons use the icon slot, leaving it empty first follows Chinese, then falls back to English.
- The "Preview" box shows the actual rendering of English, Chinese and icons on three lines, visible the moment you pick.
- Weights only take effect on variable fonts, clamped to that font's own weight axis range; a static font does not react to the value, pick an entry with a style name instead.

## How to tune it

| Problem | Touch first | Also note |
| --- | --- | --- |
| Overall look | English font | Widest impact; digits and units change with it |
| Chinese shows boxes | Chinese font | One more font file, uses memory |
| Small text looks blurry | Raise the weight | Too bold and text inside buttons gets cramped |
| Missing symbols | Icon symbols | Covers symbol code points only |

- Candidates come from bundled fonts and files in your fonts directory; "Font store:" has "Browse font store…" for online downloads, and installed fonts join the list too; with "Show system fonts" on, the list grows step by step as the background scan progresses.
- When a configured font is deleted or fails to load, the default font is used as fallback; the console logs it and the configured value is not cleared.

## Watch out

- Changing fonts changes glyph widths, so line wrapping and truncation in the HUD and menus shift too; check the preview first for long text.
- A failed font store download can be retried.

## Related

- [Language](deck:general-language)
- [Chinese input](qm:ime)
