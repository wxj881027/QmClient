## What this controls

The full diagnostic panel that appears on the right side of the screen once dbg_graphs is on: a connection rating badge with a one-line reason, five curves — RTT, prediction margin, snapshot age, prediction jitter and game time headroom — plus an FPS graph and metric cards. The panel is fixed to the right side of the screen; it does not enter HUD editing and cannot be dragged.

## In game

- The badge is normally green "Normal"; when the network worsens it turns "Elevated" or "Critical" and gives a one-line reason, such as snapshots arriving late, visible prediction jitter or abnormal local frame times.
- If RTT and snapshot age spike together, the line or the server is the likely cause; if only the frame rate collapses while the other curves stay flat, local rendering is struggling.
- A disconnect or reconnect clears the history, and the curves regrow from the left.

## How to tune it

1. On the card, bind a key to "Global toggle key" (default Ctrl+Shift+G), then use it to switch the panel on and off.
2. If the panel blocks too much of the view, lower "Panel opacity"; it only fades the backing plate, text and curves are unaffected.
3. Turn it off when you are done: dbg_graphs is not written to the config, so it is off anyway after a client restart.

## Watch out

- History spans only about nine seconds; the curves you see are the recent moment, not the whole round.
- The curves reflect only the state between this machine and the current server; read them again after switching servers.
- The readings are diagnostic data, not scores or times — don't use them to race anyone.

## Related

- [Anti-latency tools](tclient:anti-latency-tools)
- [Debug mode](qm:debug_mode)
