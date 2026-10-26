## What this controls

Puts the current song's lyric line inside the Dynamic Island. Sources are local players (collected via Hook on Windows) or Spotify's web API. It captures no audio and does no song recognition.

## In game

Lyrics follow the song's timeline — the line being sung is the line shown. Of the five sources, the effective one is the first in the order "Netease Cloud Music → Soda/Kugou/QQ Music → Spotify" that has the current line; when a source's heartbeat times out (player closed or helper not running), it is treated as dead and the Dynamic Island falls back to no lyrics. When pause or track switching stalls, check the player itself first, then the client.

## How to tune it

1. "Enable lyrics" is the display master switch, and "Show lyrics inside Dynamic Island" decides whether the line appears at all — both must be on for text to show.
2. Source switches are mutually exclusive: turning one on automatically turns the other four off. Netease is on by default; switch to the matching toggle for another player.
3. For Spotify, paste `spotify_ck` (the sp_dc value from browser cookies) into the card. It uses the official lyrics API and falls back to LRCLIB when a song is not covered or you are offline; with neither, there are no lyrics.
4. For Kugou, run "Set up Kugou lyrics" first — the timeline is only available once the files are installed; "Restore Kugou files" undoes it. With Kugou or QQ Music enabled, a collection status line shows at the bottom of the card.

## Watch out

- "Enable Netease music Hook" and "Enable lyrics" are two separate things: the latter only controls display, and with it off the Hook keeps collecting in the background — to stop collection, turn off the source switch.
- Local hooks like Netease rely on an external helper process that reads the player window and reports song and progress; Windows only.
- Spotify's `spotify_ck` is a long-lived login credential stored only in your local config; when it expires, copy it fresh from the browser.
- Lyrics render inside the Dynamic Island; hide the island and the lyrics are gone with it.

## Related

- [Dynamic Island](qm:dynamic_island)
- [System media control](qm:system_media_controls)
