## What this controls

Colors for the six chat message types: system, client echo, mentions, team, friends and normal. Each type gets a base color, plus an optional gradient; friend messages also get a heart color, and the system message prefix can be hidden.

## In game

These colors decide whether you can tell who is talking in a busy chat. Team green, friend red, mentions as an alert color, plain white for strangers in global chat.

Names stay single-colored, drawn in team color or default gray; only the message body takes the gradient. Linear pushes left to right, radial spreads from the center out, angular sweeps around the center, Reflected folds toward the middle, Diamond mirrors diagonally.

Changes apply immediately — even old lines in your chat history are recolored, and the whole chat block is rebuilt with the new palette.

## How to tune it

Start with base colors only, until name, body and background are clearly apart. Add a gradient when you want one: Linear is the safest on long lines; Radial suits short lines; Angular and Diamond get busy as text grows.

Leave "Chat gradient range percentage" near 100 — at 200 you are spreading the color steps thinner, not making them stronger. System, friend and client echo have a checkbox on the left; untick one and that whole message type disappears from chat.

| Type | Look |
| --- | --- |
| Linear gradient | One direction, holds up on long lines |
| Radial gradient | Spreads from the center, best on short lines |
| Angular gradient | Sweeps around the center, short lines |
| Reflected gradient | Mirrored around the center, most restrained |
| Diamond gradient | Diagonal mirror, accent use |

## Watch out

The longer the message, the coarser the gradient sampling — very long lines can look almost flat. That is not a broken setting.

Turning off system messages also swallows server warnings and join notifications.

"Friend heart" only recolors the heart glyph, and only while friend messages are on.

## Related

- [Chat display and layout](deck:appearance-chat-settings)
- [Chat preview](deck:appearance-chat-preview)
