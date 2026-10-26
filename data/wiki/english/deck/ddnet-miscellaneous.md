## What this controls

The remaining three items on the DDNet page: replacing the timeout code, running a chat command automatically after joining a server, and unregistering protocol and file type associations on Windows.

## In game

- The timeout code decides how servers recognize you as the same player. It is derived from a seed plus the server address, so every server has its own; on one server it never changes, and only this keeps your old ranks connected after a reconnect.
- "Run on join" sends that command automatically after you join — for locking your view, muting notifications, enabling practice and the like.
- After "Unregister protocol and file extensions", ddnet:// links in the browser and double-clicked .demo and .map files no longer open with this client.

## How to tune it

1. Only click "New random timeout code" when your name is being impersonated or a server treats you as a new player. Once clicked, every server's code changes at once, with no confirmation and no undo.
2. Keep "Run on join" to the one or two commands you use most, separated by semicolons, no slashes — for example showall 1.
3. Click unregister only when you truly don't want links and demos opening here anymore; the way back is reinstalling or re-registering.

## Watch out

- The seed starts empty; on your first server connection the client generates and stores one, and it never changes after.
- "Run on join" and the timeout code go out as one chat message and only run on servers that support the format; on servers that don't, the field is dead weight.
- The input box holds 100 characters, fewer when writing Chinese; pile up too much and it gets cut off.
- Unregistering only touches the Windows system registry — it deletes no client files and leaves config and existing demos alone.
- This item only appears on Windows; not seeing it on other systems is not a cut feature.

## Related

- [Player](deck:player-identity)
- [Client](deck:general-client)
