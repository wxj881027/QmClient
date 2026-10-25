## What this controls

The sponsor roster: the three authors' Tees, an expandable sponsor QR code, and below them sponsor nicknames laid out in gold.

## In game

You will not see this card in a match. After donating, checking whether you made the list means looking at the nickname rows here.

## How to tune it

1. Click "Show sponsor QR code" to expand the QR code, click again to collapse. The expanded state lasts until the client closes; leave and come back and it is still open.
2. "Reload" next to "Sponsors:" asks the server for a fresh list, not a re-read of the local cache.
3. After an update the card re-lays itself out; no manual refresh needed.

| What you see | Meaning |
| --- | --- |
| Loading | Neither a local cache nor a push received yet |
| A row of gold names | The list is normal |
| None | Data arrived, but contains no names |
| An error line where the QR code should be | The embedded image could not be decoded |

## Watch out

"Reload" depends on the realtime connection; while it is down, clicking gives no feedback, and nothing is re-sent once the connection recovers.

The list is read from the local cache first, then overwritten by the server push, so offline you see the previous content.

Once QR decoding fails there is no retry; restarting the client decodes again.

## Related

- [Sponsor title](deck:qmclient-contributors-title)
- [QmClient Community](deck:qmclient-contributors-community)
