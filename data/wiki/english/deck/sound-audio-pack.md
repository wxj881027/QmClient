## What this controls

The source of the game's sound effects. Each list row is one audio pack; whichever is selected is where gunshots, jumps, deaths and the rest take their files. Volume and playback timing unchanged — only the set of sounds you hear.

## In game

Switching packs changes no gameplay logic, only what you hear. The number at the end of a row is the count of sound files in the pack — both `.wv` and `.opus` count — a rough hint at how complete it is.

Selecting a pack with missing files does not error; the missing entries fall back to the built-in sounds, and it sounds half-swapped.

## How to tune it

1. "Default" is the built-in sounds.
2. After you drop packs into the audio directory yourself, press refresh for them to appear; directories created while running do not enter the list automatically.
3. Selection applies immediately; gunshot, jump and hurt are easiest to tell apart.

"Edit audio pack" swaps one sound for your own .wv file, replacing only that entry; pick the original name again if you got it wrong.

## Watch out

- Switching packs stops every playing sound before a full reload, so the cut itself is silent.
- Selecting a pack while sounds are still loading skips the reload and the settings page shows a restart hint; select again once loading finishes.
- "Audio pack directory" opens the audio folder inside the save directory; packs in the program directory are scanned but do not appear in this folder.
- Import accepts .wv only; other formats are rejected at preview.

## Related

- [Sound](deck:sound-toggle)
- [Volume](deck:sound-volume)
