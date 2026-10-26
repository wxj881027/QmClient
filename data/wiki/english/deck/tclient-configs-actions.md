## What this controls

Every configurable variable of the client in one table: filter by source and purpose, stack changes, apply them in one go, or reset items to defaults one by one.

## In game

Everything here acts directly on graphics, input and sound, but changes do not take effect at once. They are written the moment you press "Apply Changes", so you can stack a few and check once.

## How to tune it

1. Narrow with the three source toggles (DDNet, TClient, QmClient) and the ten tags (Visual, HUD, Input, Chat, Audio, Auto, Social, Camera, Gameplay, Misc), or type a variable name or description word into "Search".
2. A changed value only stages: the "Changes" counter goes up one and the row turns yellow.
3. Press "Apply Changes" to write, or "Clear Changes" to drop everything.

| What you want | How |
|---|---|
| Undo only one or two items | Turn on "Modified", press "Reset" on that row, then apply |
| Reset a color to default | Press "Reset" in the color picker, then apply |
| See more rows on screen | Turn on "Compact" |
| Find by purpose | Click tags; selecting several is a union |

## Watch out

- "Reset" only writes the default into staging; without applying, nothing changed.
- Color rows have no reset button at the end; their "Reset" lives in the color picker, also through staging.
- Staged changes survive paging; they are lost when the client exits.
- The variable list is collected once at startup; items added in a new build appear only after a restart.
- Variables limited to 0 and 1 are drawn as checkboxes, the rest as number fields; values above the maximum are silently ignored.
- Applying runs one console command per variable in name order, like typing them yourself.

## Related

- [Config files](deck:tclient-info-files)
- [Profiles](deck:tclient-profiles)
