## What this controls

The look of the full-width status bar at the bottom of the HUD: master switch, height, item labels, local time format, background and text color, and two alpha levels. Which items appear is edited in the "Status bar preview" card.

## In game

When enabled, a translucent black strip appears at the very bottom of the screen, spanning the full width with the numbers evenly spaced. Each height step scales the bar and its text together.

## How to tune it

| Item | Suggestion | Cost |
| --- | --- | --- |
| Status bar height | 6–9 | The taller it is, the more it covers; font size follows |
| Status bar alpha | 60–80% | Too low and the text blurs into the background |
| Text alpha | Keep at 100% | Lower it and distant numbers become unreadable |
| Show labels on status bar items | Only for items you can't recognize | Each item gains a "name:" prefix that takes width |

"Use 12 hour clock" is on by default, "Show seconds on clock" is off by default; both affect only "Local Time".

## Watch out

- The status bar is off by default, so a fresh install shows nothing. Check "Show status bar" first before suspecting anything else.
- Font size has no separate setting — height is the only dial: text takes 60% of the height, 20% padding above and below. Smaller text means lower height.
- The blur behind the background follows the global Gaussian blur switch. Turn it off globally and the background stays, but the blur behind it is gone.
- At "Status bar alpha" 0, background and blur both vanish and only text remains.
- In Zen mode, "Show status bar" grays out with the hint "Controlled by Zen mode"; leave Zen mode to get control back.
- During recording and export the status bar follows the HUD switch — HUD off means it never reaches the video.

## Related

- [Status bar preview and items](deck:tclient-status-bar-preview)
- [Tee status bar](tclient:tee-status-bar)
