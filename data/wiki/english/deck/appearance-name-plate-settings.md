## What this controls

Whether each Tee gets a nameplate overhead, for whom, how large, how far from the head, and what it carries: name, clan, client ID, friend icon, team colors.

## In game

"Show name plates" has six levels, not an on/off switch. The client sorts players into three groups: the character you control, your other character (the dummy), everyone else. The six levels are the visible combinations of these groups; "Current" keeps only the character you drive, "Others and own" hides your active character and keeps the dummy and other players.

"Name plates size" changes the font directly, about 28 px at the default level; "Name plates offset" is how far the plate sits above the head, 10 to 50 px. Clan and client IDs have their own font sizes, and client IDs can also move to a separate line of their own.

## How to tune it

| Goal | Pick/adjust | Cost |
| --- | --- | --- |
| Clean screen | Show name plates → Others | You and your dummy become invisible |
| Tell teammates apart in fights | Others and own + Use team colors for name plates | Longer name row |
| Identify players | Enable client IDs, separate line when needed | One extra row's width |

Font levels run from -50 to 100: level 0 is about 18 px, maxed out about 38 px. When the screen gets crowded, turn off info blocks instead of enlarging forever.

## Watch out

With "Show name plates" on "None", clan, client IDs and the friend icon vanish along with it; coordinates, hook strength and key presses are independent parts and stay.

"Show client IDs in name plates" here only affects the overhead plate; the ID switch on the HUD card also affects scoreboard and chat.

Team colors and the friend icon only tint and badge — they never add a row. Names fade at distance; that is distance falloff, not hiding.

## Related

- [Nameplate text](deck:appearance-name-plate-text)
- [Preview](deck:appearance-name-plate-preview)
