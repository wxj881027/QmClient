## What this controls

A second outlet for server system messages and Echo messages: this text becomes short on-screen popups, managed by a small queue on the render layer. The data still comes from the chat stream.

## In game

Taken-over sentences no longer appear in the chat box; each popup stays up for the "Notification hold time" (500–10000 ms, default 2500 ms) and leaves with the animation. Repeated identical text merges into an x2, x3 counter instead of stacking up. When the queue exceeds "Max visible notifications" (1–8, default 3), the oldest one is dropped outright. Echo colors are fixed when a popup is queued; changing chat colors later does not retroactively recolor old popups. Popup position and edge margins are dragged in the HUD editor.

## How to tune it

| Goal | Setting |
| --- | --- |
| Miss fewer announcements | Turn on "Show important server prompts as notifications" |
| See Echo too | Also turn on "Route Echo messages to notifications" |
| Keep them out of the way | Drop the hold time to 1500–2000 ms and "Max visible notifications" to 2 |
| Pick by category | In "Advanced options", enable "Use notification category filters", then tick basic info and help |

## Watch out

- Category filters are off by default: join, version, rules and help text also pop up and leave the chat box. To see only important prompts, enable category filters first and make sure "Show basic server information" and "Show server help and usage messages" are unticked.
- Popup and chat are either/or — a taken-over sentence is not in the chat box; look for the original in the console.
- "Detect compatible solo prompts from custom servers" is the fallback for solo-zone enter/leave prompts from non-DDNet servers.
- In demos and playback, popups follow the chat display settings; when chat is hidden, popups stay out of the recording too.

## Related

- [Zen mode](qm:focus_mode)
- [Word filter](qm:block_words)
