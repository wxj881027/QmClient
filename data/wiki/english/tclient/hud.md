## What this controls

A few HUD pieces: a compact vote bar, a position-and-angle readout, a free-spectate cursor, a last-survivor notification and screen center lines. They only add drawing, never change game state.

## In game

- "Show compact vote HUD" replaces the vanilla vote bar with a small box: description, reason, seconds left and progress, vote bars, your yes/no keys. Scoreboard votes go compact too. Off returns the vanilla bar; never both at once.
- "Show position and angle (mini debug)" shows top-left coordinates (in 32-unit tiles), facing, horizontal/vertical speed. Spectating follows the spectated player; free view shows the mouse position. With the DDRace panel or health/armor bar on, the block shifts down.
- "Show the cursor while free spectating" draws one fixed cursor in free view, always at the exact screen center, independent of the weapon.
- "Notify when only one tee is still alive:" pops your custom text with your color, position and font size when the team has more than one player and exactly one remains unfrozen.
- "Show the screen center line" draws horizontal and vertical lines through the screen center; width 0 is a thin line, above 0 a colored block. Hidden while the scoreboard is open.

## How to tune it

1. Enable one piece at a time; confirm position and size in game.
2. For the last-survivor notification, start with defaults; if it covers the health/armor bar, adjust the horizontal/vertical position or shrink the font.
3. The vote bar's position can be dragged in the HUD editor.

## Watch out

- The last-survivor notification needs others in your team; solo never pops — not a bug.
- The free-spectate cursor ignores "Ingame cursor scale"; its opacity is adjusted separately.

## Related

- [Tee status bar](tclient:tee-status-bar)
- [Visual nameplates](tclient:visual-nameplates)
