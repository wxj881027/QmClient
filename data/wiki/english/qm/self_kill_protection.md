## What this controls

Client-side self-kill blocking: with the toggle off, the K key and the kill command stay on your machine and are never sent to the server. Only client-initiated self-kill is blocked; server commands and game-driven deaths are unaffected.

## In game

Pressing K does nothing, and the notification bar echoes "Client self-kill is disabled (server commands are not affected)" at most once per second — the key is not broken. With dummy input copy on, a normal self-kill would kill both Tees; blocked, neither dies.

## How to tune it

1. In the card, bind one key each to "Self-kill off" and "Self-kill on", placed away from K.
2. Before a serious run, press the off key and only start after seeing the echo "Client self-kill is disabled (K key / kill command will be ignored)".
3. When you really want to restart, press the on key first, then K.

## Watch out

- The toggle is written to the config; after a map change, reconnect or restart it is still in its last state and does not restore itself.
- With fast practice on, K performs the practice reset instead and never reaches this path.
- Demos are not blocked.

## Related

- [Key binds](qm:key_binds)
- [HUD notifications](qm:hud_notifications)
