## What this controls

Friend detection and alerts: it scans the player lists of the server browser for friends and notifies you when one comes online; when a friend joins the map you are on, it can greet them in chat or flash a large text announcement. All runs locally — nothing touches the server, and the other player's online status settings do not matter.

## In game

- "Notify when friends come online" is not a push feed but a list scan: it processes 32 servers' info per frame and reports only once a full pass is done. The first time a server is seen, a silent baseline is built, so it does not pop everyone at once. Hits go out as chat Echo; with "Route Echo messages to notifications" on, the alert becomes an on-screen popup. If a friend is not seen again within 10 seconds they count as offline, and a later hit notifies again.
- "Auto greet friends entering map" watches the current server's player list, waits 3 seconds after a friend appears, then sends a public chat line in the form "name: greeting".
- "Large text announcement for friend joining" draws large text on screen, displayed only on your machine; %s in the text is replaced with the friend's name (multiple names joined with spaces).
- "Auto-follow delay" covers something else: after you choose to follow a friend, it is how long the client waits before auto-connecting when they switch servers (0–30 seconds, default 3 seconds).

## How to tune it

1. Turn on "Notify when friends come online" first, then "Auto refresh server list"; "Refresh interval" is 5–300 seconds, default 30 seconds.
2. If you want alerts but no talking, leave "Auto greet friends entering map" off; if you enable it, fill in "Greeting text" — empty means it stays silent.
3. On strictly moderated servers, use "Large text announcement for friend joining" instead; it never enters chat.

## Watch out

- Only servers present in the server browser list count; directly connected private servers are never scanned and produce no alerts.
- Greeting and online alerts are two independent switches, and greeting is not bound by the alert toggle. Greeting is a public message — use it carefully on busy servers.
- Keep your friend list maintained beforehand; anyone not on it is never matched.

## Related

- [HUD notifications](qm:hud_notifications)
- [Translate](qm:translate)
