## What this controls

Camera smoothing for free spectator view: instead of snapping along, the camera glides toward the mouse position.

## In game

Applies in online spectating and demo playback. Moving the camera with the mouse no longer jumps; it approaches the target at a rate set by "Cinematic smoothness" — higher values lag behind your hand, 0 is instant. Following a player, scrubbing or pausing never carries old state over; the camera re-aligns.

## Watch out

- Only free mouse view is taken over: following players or spectating someone specific is not free view, so no effect is not a bug.
- High smoothness lags when hunting players in corners; raise it for video.
- With the toggle off, "Cinematic smoothness" does nothing; check both when troubleshooting.

## Related

- [Camera and view](qm:camera_view)
- [Demo player](deck:ddnet-demo)
