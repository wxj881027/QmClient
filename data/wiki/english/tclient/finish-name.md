## What this controls

An automatic rename before finishing: tick the switch and fill in "Finish Name:", and when your controlled Tee comes near finish tiles the client renames it to the target value, so the finish is recorded under the name you want. Only the name changes; skin, clan and flag stay untouched.

## In game

- A check runs once per second; when a finish tile (front layer included) appears within ±10 tiles, the rename is sent and the console prints a line about the rename.
- No resend within 8 seconds of a send, at most 3 tries per stay; the counter resets if the name is already the same or you leave the finish area.
- The rename lasts only for this session; after reconnecting you are back on the original name. If the server rejects it, the client gives no reason.

## Watch out

- The name is capped at 15 bytes; one Chinese character takes 3, so long Chinese names get truncated.
- Leaving and re-entering the finish area grants 3 fresh tries, and just passing through the finish triggers it — turn it off when not needed.

## Related

- [Map finishing](qm:qiafen)
- [Player stats](qm:player_stats)
