## What this controls

The exit that uploads locally saved `.map` files to the map-testing endpoint. Uploads go over HTTP, not through the game connection, regardless of which server you are on.

## In game

When the map-testing flow is waiting on your submission, use this card to upload a map from the save directory. The status line tells you where you are: uploading, success, cancelled, or the exact error — file too large, invalid name, network failure, server rejection, unexpected response. Success only means the file reached the endpoint; the server does not switch maps because of it, that happens server-side.

## How to tune it

1. Press "Select map" to pick a `.map` from the save directory; the interface shows its full path.
2. Check the "Target server" and "Player" lines: the former is the upload endpoint address, the latter the name of the Tee you currently control on the server (your player name when not connected).
3. Press "Upload map". While uploading, the button turns into "Cancel" and can abort the transfer.
4. Read the result in the status line and the details below.

The endpoint address is set by `qm_map_upload_endpoint`; leaving it empty disables uploading.

## Watch out

- 64 MiB is the client's own limit; anything larger is rejected locally and no request is ever sent.
- The filename must end in `.map` and must not end with a space or contain special characters, otherwise it counts as invalid.
- A player name must be available before upload; if none can be fetched you get "A player name is required."
- The endpoint address must start with HTTP or HTTPS; a wrong value is also blocked locally.

## Related

- [Favorite maps](qm:favorite_maps)
- [Console](qm:console)
