## What this controls

A hook strength marker beside each name: when two Tees hook each other, it shows who gets pulled away. Off by default, changes local visuals only, no gameplay.

## In game

Strong and weak are relative to you (or the player you follow): a higher strength ID than yours means strong hook — hooking them pulls you off. Change follow target and the same player's marker flips. It does not answer "can this Tee be hooked".

Your own nameplate shows a third icon, a plain hook with no strength lean.

"Show hook strength number indicator" adds a number beside every name: the raw strength ID, not a level.

## How to tune it

Turn the marker on with "Show hook strength icon indicator", add numbers as needed. "Hook strength scope" decides who gets drawn: "Self" and "Others" filter by player, "Strong hook" and "Weak hook" keep one state only, "All" draws everyone. If markers cover names, adjust "Size of hook strength icon and number indicator".

"Strong hook color" and "Weak hook color" recolor the icon and number, never the verdict.

## Watch out

Numbers ignore "Hook strength scope": once enabled, every nameplate in the room carries one; the scope filters icons only.

While following someone, that person becomes the comparison base. The server must send this data for both sides, or nothing displays at all.

With "Show name plates" set to "None", the icons still draw.

Replays ignore these settings; the Demo card has its own strong/weak hook levels.

## Related

- [Name Plate](deck:appearance-name-plate-settings)
- [Demo](deck:ddnet-demo)
