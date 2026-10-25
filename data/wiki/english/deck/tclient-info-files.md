## What this controls

Quick access to the four config files, plus export and import of configuration packages. Hand editing, backups and moving machines all start here.

## In game

This card does not touch the game. It manages "the same setup on another machine": pack your settings, profiles, chat binds and war list into a .qmconfig package and take it with you.

## How to tune it

| What you want | How |
|---|---|
| View or hand-edit | The four buttons in the top two rows open the matching file with the system default program |
| Keep a way back | Expand "Configuration packages" and pick "Export configuration package" |
| Move machines | Put the package into the package folder, switch to "Import configuration package", select it and press "Import and restart" |

The four files live under `qmclient/`: `settings.cfg` holds all settings (key binds included), the other three are `qmclient_profiles.cfg`, `qmclient_warlist.cfg` and `qmclient_chatbinds.cfg`. On export the current settings are saved to disk first, then the four files plus the extra cfg files ticked in the list are packed.

## Watch out

- Import replaces the listed files as a whole; files outside the list are untouched, and entries marked as deleted are removed.
- Before importing, the current files are backed up to `backups/` in the package directory; the backup stays on this machine.
- A single file is capped at 1 MiB, a package at 24 MiB; over the limit it fails outright.
- After a successful import the client restarts itself without saving the in-memory settings first, so they cannot overwrite what was just imported.
- A mistyped command in a hand-edited file raises no error; it is silently dropped on the next load.
- File edits need a config reload or a restart to apply; there is no hot reload.

## Related

- [Profiles](deck:tclient-profiles)
- [Config variables and reset](deck:tclient-configs-actions)
