## What this controls

Display settings for the input method candidate bar: the composition string and candidates from the system IME are drawn as the client's own candidate bar. It only handles looks and switches — the dictionary and the IME itself stay whatever your system has.

## In game

When an input box has focus, the candidate bar pops up next to the box's composition position; it does not show when there are no candidates (plain English typing) or the input box is not rendered. With "Auto manage IME while typing" on, Chinese input works the moment focus enters an input box and stops automatically on blur.

## How to tune it

1. Turn "New IME" off to fall back to the old style: fixed font size, a vertical dark candidate box, and all colors below stop working.
2. Under "Edit color", four roles are tuned separately: IME background color, IME text color, IME selected text color and IME selection background color, and each role can also carry a gradient.
3. "Opacity" applies to the background color role as the whole bar's transparency; the other roles adjust their own transparency. "IME font size" is a percentage (75–200%, default 100%). If it gets messy, "Reset IME appearance" covers the four colors and the font size in one go.

## Watch out

- Only on Windows does the client draw the candidate bar; on other platforms the system candidate window is used, and "New IME" and the colors have no effect there.
- A very large font size squeezes candidates narrower when the bar hits the screen edge; it never spills off-screen.
- If no usable input method is installed on the system, nothing here can help.

## Related

- [Chat completion](qm:chat_completion)
- [Chat message appearance](deck:appearance-chat-messages)
