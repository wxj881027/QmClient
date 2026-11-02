## What this controls

Redeems a sponsor code, writes a custom title, stores it on the title server. The title is a small icon badge beside your name, used in chat and on the name plate. This path carries identity data only; no in-game values are touched.

## In game

Once saved, the badge shows beside your name in chat and on the name plate above your head. Unredeemed players can still open the card; the status line just stays on "Enter your sponsor code".

"Chat message style" decides how your chat messages render: Normal or Platinum highlight. Others see the same style, and "Chat message preview" below uses the same rendering.

The title follows the nickname. With "Only show with this nickname" ticked, a name change — even just capitalization — hides the badge until you rebind and save.

## How to tune it

1. Paste the code into the input field, press "Redeem". The code is 48 hexadecimal characters; a wrong length is reported invalid at once.
2. Write under "Custom title"; the limit is display width: 12 half-width or 6 Chinese characters.
3. To lock the nickname, tick "Only show with this nickname" and put the exact name in "Exact bound nickname".
4. Pick a "Chat message style"; watch the "Chat message preview".
5. Press "Save": title, bound nickname, chat style and badge appearance go out together.

| Goal | How |
| --- | --- |
| Just a small mark beside the name | Two or three characters, not a full sentence |
| Reserve it for one alt account | Tick "Only show with this nickname", fill in the full nickname |
| Let others see the new style | Picking the style is not enough; press "Save" |

## Watch out

- Square brackets, control characters and zero-width characters are not allowed in the title; saving is blocked locally with a message.
- A code works once; claimed elsewhere you get "Sponsor code already claimed".
- The redemption credential is stored locally; back it up before switching computers or clearing the client directory, or the data cannot be recovered.
- "Redeem" only works before authentication; afterwards, changing codes requires the local credential to be gone first.
- "Refresh" rebuilds the draft from the server's saved copy; unsaved edits are overwritten.
- Saving submits the whole set — one changed item writes the rest back too; if the server does not yet support some fields, the save is rejected with a reason.
- The "Title display" button jumps to another card that tunes local drawing; nothing is saved there.

## Related

- [Title display](deck:qmclient-contributors-title-display)
- [Sponsor support](deck:qmclient-contributors-sponsors)
