## What this controls

A visual compensation switch for freeze: while frozen, it pulls the rendered position back from the predicted position toward the server position, reducing the position snap at the moment of unfreezing. It only changes the picture on your screen; input and hit registration are untouched.

## In game

- With "Reduce prediction while frozen" on, the longer you stay frozen the closer the view sticks to the server position, pulled back by up to "Maximum reduction"; how long you must stay frozen before the pull starts is set by "Delay before reduction". Full prediction returns immediately after unfreezing.
- "Dont predict other players if you are frozen" pins everyone else to their server positions with no smoothing; while you are frozen they look choppy.
- "Use a fixed prediction margin while frozen" and "Frozen prediction margin" are currently not wired into any calculation; turning them on changes nothing.

## How to tune it

1. Turn on "Reduce prediction while frozen", starting with Maximum reduction 100–300ms and Delay before reduction around 500ms; if the view hugs the server position too hard while frozen, raise the delay.
2. Delay before reduction cannot go below Maximum reduction; dragging the slider lower makes the card push it back up to the same value.
3. "Base prediction margin" is the floor of your prediction buffer. It is also used as the reference by the auto margin in the Input card; raise it manually only on a persistently high ping.

## Watch out

- These only change what you see; the server's judgment does not favor you any more for it.
- If you only want to save performance: the reduction part computes the pull-back with a prediction pass containing only you, so the load drops slightly when many players are around.

## Related

- [Input](tclient:input)
- [Improved anti-ping](tclient:improved-anti-ping)
