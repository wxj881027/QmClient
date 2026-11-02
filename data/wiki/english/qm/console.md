## What this controls

Appearance settings for the F1 console: font size, background opacity, command syntax highlighting and a color scheme list. It does not parse commands or decide what can run — it only draws the panel.

## In game

Open F1 and the panel renders with your font size and background color. With "Highlight console commands" on, the input line colors commands, parameters, strings, numbers and links separately, with dedicated colors for search hits and the selected hit.

## How to tune it

1. Set "Console font size" first (1–24, default 10) — it decides how many lines fit on one screen.
2. If the panel swallows the map, push "Console background opacity" (20–100%, default 96) toward 100.
3. "Console color scheme" is a single list: Modern terminal, Classic PowerShell, Custom, plus 16 Windows Terminal built-in schemes (Campbell, Solarized, Tango and others). Each row renders in its own background and foreground, with that scheme's 16-color swatch on the left and an accent dot on the selected row; the list scrolls, and the selected row scrolls into view. Only Custom expands the nine color items; the others use built-in colors — your colors are kept but inactive, and come back when you switch to Custom.
4. A preview line is embedded in the card, so changes show at once. If it gets messy, "Reset console appearance" covers font size, opacity, highlighting and all nine colors in one go.

## Watch out

- The F1 panel toolbar has a "Console settings" entry; the popup and this card share one config, and edits on either side apply.
- Highlighting only adds color; a wrong parameter still errors out. Do not use it as a syntax checker.
- Large font sizes mean fewer lines per screen, making long logs tiring to scroll.

## Related

- [Debug mode](qm:debug_mode)
- [Chat completion](qm:chat_completion)
