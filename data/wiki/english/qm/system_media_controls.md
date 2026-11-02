## What this controls

Integration with the system media session: the song the system is currently playing is shown inside the game, along with Previous, Play/Pause and Next buttons.

## In game

Windows uses System Media Transport Controls, Linux uses MPRIS. Once enabled, the cover, title and playback state of the current song appear in the dynamic island; with "Use original style" ticked (in the dynamic island settings), they are drawn on the top-left line instead. The three buttons send their commands to the current media session, so a session change never sends anything to the wrong player. Turning off "Show song info in top-left corner" only stops the drawing; the buttons keep working.

## How to tune it

1. Turn on "Enable system media control", and play a song first to confirm a session really exists.
2. Keep "Show song info in top-left corner" if you want the track visible in game; turn it off if it blocks your view, keeping only the buttons.
3. "Previous", "Play/Pause" and "Next" in the card correspond to the system media buttons.
4. The display position (dynamic island or the top-left line) is switched via "Use original style" in the dynamic island settings, not in this card.

## Watch out

- Only the system's current session is used: with several players open at once, the one it attaches to may not be the one you think.
- With no media playing, the dynamic island shows no song content; first confirm something is actually playing.
- It only reads session state and forwards button commands; it never changes playlists or the player's own settings.

## Related

- [Lyrics](qm:lyrics)
- [Dynamic island](qm:dynamic_island)
