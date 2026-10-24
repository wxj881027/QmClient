// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmCardWiki.h"

#include <base/system.h>

#include <engine/shared/localization.h>

#include <algorithm>
#include <iterator>

namespace
{
	// 全量卡片 Wiki：条目按 stableId 升序排列（二分查找的前提，顺序错了测试会红）。
	// 三段文案都是 i18n source（Localizable 只做提取标记），展示前由面板本地化。
	// 引号内是界面上真实存在的控件名，改文案时必须和卡片里的标签保持一致。
	const SCardWikiCopy s_aCardWikiCopies[] = {
		{
			"deck:appearance-chat-messages",
			Localizable("Styles chat names and message colors, including gradients."),
			Localizable("Pick the parts to colorize, then choose a gradient mode and colors."),
			Localizable("Low contrast between name and message text makes chat hard to scan in fights."),
		},
		{
			"deck:appearance-chat-preview",
			Localizable("Renders a live preview of the current chat appearance."),
			Localizable("Change values in the chat cards and watch this preview update."),
			Localizable("The preview uses your own name and colors; other players may look different."),
		},
		{
			"deck:appearance-chat-settings",
			Localizable("Sets chat position, width, font size and visibility."),
			Localizable("Choose a position, then adjust width and size; the preview card shows the result."),
			Localizable("Wide chat lines cover the game; keep the width to what you actually read."),
		},
		{
			"deck:appearance-hook-collision-main",
			Localizable("Configures the hook range and collision guide lines drawn around your Tee."),
			Localizable("Enable the guide line, then set its length, style and colors."),
			Localizable("Guide lines are computed locally and do not guarantee that a hook will connect."),
		},
		{
			"deck:appearance-hook-collision-preview",
			Localizable("Previews hook collision colors and line styles."),
			Localizable("Change colors and style options, then compare the preview."),
			Localizable("Colors too close to the map background are hard to read while moving."),
		},
		{
			"deck:appearance-hud-ddrace",
			Localizable("Adjusts DDRace-specific HUD parts such as team state, freeze bars and race time."),
			Localizable("Enable the DDRace elements you use and set their scale."),
			Localizable("DDRace HUD is driven by server messages, so it stays empty on servers without DDRace."),
		},
		{
			"deck:appearance-hud-main",
			Localizable("Configures the main HUD: health, ammo, timer, race progress and their positions."),
			Localizable("Enable each element, then set its position and size; the HUD draws on top of the game world."),
			Localizable("Elements too close to the edges overlap on ultrawide screens."),
		},
		{
			"deck:appearance-info-messages",
			Localizable("Styles kill, finish and system messages in the chat area."),
			Localizable("Enable the message types you want and choose their colors."),
			Localizable("Disabling system messages also hides server warnings."),
		},
		{
			"deck:appearance-laser-colors",
			Localizable("Sets laser colors for weapons and entities."),
			Localizable("Choose a target, then set its color; entity lasers can follow team colors."),
			Localizable("Team-colored lasers are easier to read in matches."),
		},
		{
			"deck:appearance-laser-enhanced",
			Localizable("Controls laser shape, glow and animation."),
			Localizable("Pick a shape, then tune glow and animation speed."),
			Localizable("Heavy glow costs fill rate; disable it if frame time rises in big fights."),
		},
		{
			"deck:appearance-laser-preview",
			Localizable("Previews laser bodies, outlines and impacts."),
			Localizable("Change laser options and compare the preview."),
			Localizable("The preview does not show every weapon; check in game."),
		},
		{
			"deck:appearance-name-plate-hook-strength",
			Localizable("Shows whether each tee can still be hooked, as an icon or a number beside the name."),
			Localizable("Turn on \"Show hook strength icon indicator\", optionally add \"Show hook strength number indicator\", then choose a \"Hook strength scope\"."),
			Localizable("\"Hook strength scope\" limits which players get the marker, so pick \"All\" if it seems missing."),
		},
		{
			"deck:appearance-name-plate-key-presses",
			Localizable("Draws the keys each tee is pressing beside their name, so you can read their movement."),
			Localizable("Choose \"Show players' key presses\" as \"Own\", \"Others\" or \"All\", then adjust \"Size of key press icons\"."),
			Localizable("Big icons can overlap names and nearby tees, so shrink \"Size of key press icons\" when many players are close."),
		},
		{
			"deck:appearance-name-plate-preview",
			Localizable("Previews player and dummy nameplates with the current settings."),
			Localizable("Adjust nameplate options and compare the preview."),
			Localizable("Preview spacing does not match every map; check in game before a match."),
		},
		{
			"deck:appearance-name-plate-settings",
			Localizable("Chooses which name plates are drawn and adds clan, team color, friend icon, coordinates and client IDs to them."),
			Localizable("Pick a scope under \"Show name plates\", then set \"Name plates size\" and \"Name plates offset\"; clan and client ID lines have their own switches."),
			Localizable("\"None\" hides all name plates including your own team, so choose \"Others\" if you only want a cleaner screen."),
		},
		{
			"deck:appearance-name-plate-text",
			Localizable("Adds border, gradient, rainbow and glow effects to nameplate text, with separate scopes for play, spectating and demos."),
			Localizable("Turn on \"Show advanced options\", then enable \"Border\", \"Gradient\", \"Rainbow\" or \"Glow\" and set the matching ranges and colors."),
			Localizable("Effects are heavy on crowded servers, so keep \"Automatic effect LOD when crowded\" enabled."),
		},
		{
			"deck:controls-chat",
			Localizable("Keys that open chat, team chat and chat commands, plus voice chat and repeating your last line."),
			Localizable("Click the key field beside \"Chat\", \"Team chat\" or \"Chat command\" and press the key you want to use."),
			Localizable("Give \"Team chat\" its own key so a quick call to your team cannot reach everyone by mistake."),
		},
		{
			"deck:controls-controller",
			Localizable("Turns controller input on and maps stick axes to aiming, with speed settings for play and for menus."),
			Localizable("Turn on \"Enable controller\", choose the device, pick \"Relative\" or \"Absolute\" under \"Ingame controller mode\", then tick the axes you want under \"Aim bind\"."),
			Localizable("\"Absolute\" aims at a fixed point and hides the speed setting, so pick \"Relative\" if you want to tune it."),
		},
		{
			"deck:controls-custom",
			Localizable("Binds that use commands the client does not recognize, listed by their raw command text."),
			Localizable("The card only appears while such a bind exists; click its key field to change the key, and the row label is the command it runs."),
			Localizable("Rows here usually come from old configs, so test each command before you rely on it."),
		},
		{
			"deck:controls-dummy",
			Localizable("Keys for your second tee: switching to it, making it jump, fire or hook, and copying your moves."),
			Localizable("Click the key field beside \"Toggle dummy\", \"Dummy jump\" or \"Dummy copy\" and press the key you want."),
			Localizable("Learn \"Toggle dummy\" first; the other dummy keys only help once you already play with a second tee."),
		},
		{
			"deck:controls-miscellaneous",
			Localizable("All remaining binds: scoreboard, spectating, console, screenshots, emoticon and practice tools."),
			Localizable("Click the key field next to an action such as \"Scoreboard\", \"Spectate mode\" or \"Pie menu\" and press the key you want."),
			Localizable("\"Active disconnect\" leaves the server without closing the client, so bind it only for deliberate use."),
		},
		{
			"deck:controls-mouse",
			Localizable("Mouse speed for aiming and for menus, and how far your cursor may travel from the tee."),
			Localizable("Set \"Ingame mouse sens.\" for aiming and \"UI mouse sens.\" for menus, then limit the cursor with \"Minimum cursor distance\" and \"Maximum cursor distance\"."),
			Localizable("\"Ingame mouse sens.\" changes your aim, so adjust it in small steps and test it on a warm-up map."),
		},
		{
			"deck:controls-movement",
			Localizable("Keys for moving, jumping, firing and hooking, plus camera zoom and visibility shortcuts."),
			Localizable("Click the key field next to an action such as \"Jump\" or \"Hook\" and press the key you want; a waiting field takes your next key."),
			Localizable("A key you assign here replaces whatever action used it before, so check your other keys after changing one."),
		},
		{
			"deck:controls-voting",
			Localizable("The two keys that cast your vote: one for yes and one for no."),
			Localizable("Click the key field next to \"Vote yes\" and \"Vote no\" and press the key you want for each."),
			Localizable("Votes often appear while you are still moving, so keep both keys easy to reach."),
		},
		{
			"deck:controls-weapon",
			Localizable("Keys that select each weapon and step to the next or previous one."),
			Localizable("Click the key field beside \"Hammer\", \"Pistol\", \"Shotgun\" or \"Next weapon\" and press the key you want to use."),
			Localizable("Direct weapon keys are faster than cycling, so bind at least the weapons you use most."),
		},
		{
			"deck:credits-friend-links",
			Localizable("Links to related project and community websites."),
			Localizable("Open a link to visit the site."),
			Localizable("External sites are not controlled by the client."),
		},
		{
			"deck:ddnet-background",
			Localizable("Chooses the menu background map and the colors drawn behind the UI."),
			Localizable("Pick a map or a solid color, then adjust brightness and blur."),
			Localizable("Bright backgrounds reduce text contrast; keep the overlay dark enough."),
		},
		{
			"deck:ddnet-demo",
			Localizable("Controls demo recording and playback behavior."),
			Localizable("Choose when recording starts and how demos are named; playback speed and camera follow are separate."),
			Localizable("Demos store raw network data, so long sessions produce large files."),
		},
		{
			"deck:ddnet-gameplay",
			Localizable("Toggles DDNet gameplay indicators and assists such as race time, team markers and hints."),
			Localizable("Enable the indicators you need; each one draws on your own screen only."),
			Localizable("Indicators that read game state can lag behind the server; do not rely on them for frame-perfect input."),
		},
		{
			"deck:ddnet-miscellaneous",
			Localizable("Collects the remaining DDNet client preferences that do not belong to other cards."),
			Localizable("Review the entries and change only what you understand; defaults are tuned for normal play."),
			Localizable("Options here affect networking or identity; test on a server before using them in matches."),
		},
		{
			"deck:general-client",
			Localizable("Sets the client update rate, whether the main menu is skipped, and opens the folders for settings, saves and themes."),
			Localizable("Change \"Update Rate\" in Hz, tick \"Skip the main menu\", and use \"Settings file\" or \"Themes directory\" to open those locations."),
			Localizable("\"Save power by lowering update rate (higher input latency)\" adds input delay, so keep it off while racing."),
		},
		{
			"deck:general-game",
			Localizable("Toggles the dynamic camera, its smoothing, and automatic weapon switching on pickup or when out of ammo."),
			Localizable("Tick \"Dynamic Camera\" to turn it on; \"Smooth Dynamic Camera\" adds glide to its movement, and the weapon switches are separate rows below."),
			Localizable("\"Switch weapon on pickup\" grabs whatever you walk over, which can override the weapon you wanted to keep."),
		},
		{
			"deck:general-language",
			Localizable("Chooses the language used by every menu, setting and message in the client."),
			Localizable("Click a language in the list to apply it right away; the flag on the left shows which one is selected."),
			Localizable("Community translations can be unfinished, so a few lines may stay in English; switch back here at any time."),
		},
		{
			"deck:general-recording",
			Localizable("Automatic demos, game-end screenshots and CSV exports, their file limits and save folders, plus screenshot watermarks."),
			Localizable("Turn on \"Automatically record demos\" or a screenshot option, set \"Max demos\" or \"Max Screenshots\", and fill \"Demo directory\" or \"Screenshot directory\" to move the files."),
			Localizable("Old files are deleted once the save limit is reached, so copy any demo you want to keep somewhere else."),
		},
		{
			"deck:global-search-input",
			Localizable("Searches settings by card title, feature name or keyword."),
			Localizable("Type a word; results filter as you type across cards and config names."),
			Localizable("Search matches config names too, so qm_ queries find raw options."),
		},
		{
			"deck:global-search-results",
			Localizable("Lists cards matching the current search and jumps to them."),
			Localizable("Select a result to open its card."),
			Localizable("Results include cards on other pages; the tab switches automatically."),
		},
		{
			"deck:graphics-display",
			Localizable("Chooses windowed or fullscreen mode, the monitor and the resolution; switching recreates the swap chain, so the screen may flash briefly."),
			Localizable("Pick the mode first, then the monitor and resolution; the refresh rate follows the monitor's current mode."),
			Localizable("If exclusive fullscreen fails, switch back to windowed or borderless first."),
		},
		{
			"deck:graphics-icons",
			Localizable("Sets UI icon color and weight for the icon font shared by menus and cards."),
			Localizable("Pick a color preset or a custom color, then choose regular or bold weight."),
			Localizable("Custom colors too close to the surface make icons disappear; keep contrast."),
		},
		{
			"deck:graphics-interaction",
			Localizable("Sets how much motion the interface plays: transitions, card entrance, reorder animations and scroll feel."),
			Localizable("Choose off, simple or full; the individual switches below only apply to full."),
			Localizable("Turn motion off on weaker machines; cards then open and scroll immediately."),
		},
		{
			"deck:graphics-modes",
			Localizable("Lists the display modes the graphics backend reports for the selected monitor."),
			Localizable("Pick a mode from the list; unsupported modes are not offered."),
			Localizable("Unusual refresh rates can cause tearing; prefer the monitor's native rate."),
		},
		{
			"deck:graphics-visual",
			Localizable("Controls how UI surfaces are drawn: card surface opacity, border width, corner rounding, segment count and the rainbow title effect."),
			Localizable("Adjust one value at a time; corners and segments are drawn per frame, so large segment counts cost GPU time."),
			Localizable("Very thick borders or high segment counts make text look blurry; keep them near the defaults."),
		},
		{
			"deck:player-country",
			Localizable("Selects the country flag shown for you and for the dummy."),
			Localizable("Pick a country for each player."),
			Localizable("Flags are cosmetic and can be hidden by other clients."),
		},
		{
			"deck:player-identity",
			Localizable("Sets player and dummy name, clan and identity information."),
			Localizable("Edit the name and clan fields; the dummy keeps its own values."),
			Localizable("Names are sent to the server and shown to other players."),
		},
		{
			"deck:qmclient-contributors-community",
			Localizable("Lists QmClient community groups and project links."),
			Localizable("Open a link to join the community."),
			Localizable("Links open in the system browser."),
		},
		{
			"deck:qmclient-contributors-ddnet",
			Localizable("Lists DDNet contributors and staff credits."),
			Localizable("Scroll to browse the credits."),
			Localizable("Credits are read-only."),
		},
		{
			"deck:qmclient-contributors-sponsors",
			Localizable("Shows the people supporting QmClient development."),
			Localizable("Scroll the list to see sponsors and support options."),
			Localizable("Support is optional; nothing here changes client behavior."),
		},
		{
			"deck:qmclient-contributors-title",
			Localizable("Redeems a sponsor code, sets the custom title it unlocks and picks how that title looks in chat."),
			Localizable("Type your code into \"Sponsor code\" and press \"Redeem\", then edit \"Custom title\", choose a \"Chat message style\" and press \"Save\"."),
			Localizable("Press \"Save\" after any change: the title and chat style are stored on your account, and \"Refresh\" reloads them."),
		},
		{
			"deck:qmclient-contributors-title-display",
			Localizable("Where your title appears and how it looks: nameplate, style, motion, colors and glow."),
			Localizable("Turn on \"Show titles on nameplates\", pick a \"Title style\", then adjust \"Title motion\" and \"Wave amount\"; \"Advanced mode\" adds color and glow."),
			Localizable("Changes apply at once, so check \"Title preview\" to compare styles; press \"Save\" on the \"Sponsor title\" card to share one."),
		},
		{
			"deck:sound-audio-pack",
			Localizable("Lists and activates custom audio packs that replace game sounds."),
			Localizable("Pick a pack to activate it; samples are reloaded when the selection changes."),
			Localizable("A pack missing files falls back to the default sounds."),
		},
		{
			"deck:sound-toggle",
			Localizable("Enables audio output and selects the playback device."),
			Localizable("Turn sound on, then pick a device; the list comes from the audio backend at startup."),
			Localizable("If a device disappears after unplugging headphones, reselect it or restart the client."),
		},
		{
			"deck:sound-volume",
			Localizable("Sets master, music and game volume separately."),
			Localizable("Drag the three sliders; master scales the other two."),
			Localizable("Music and game volume are multiplied by master, so raising master alone can clip."),
		},
		{
			"deck:tclient-bind-wheel-editor",
			Localizable("Assigns commands and labels to the slots of the radial bind wheel."),
			Localizable("Pick a slot, type the command and its label, then hold the wheel key in game to test."),
			Localizable("Empty slots still take space, so clear the ones you no longer use."),
		},
		{
			"deck:tclient-bind-wheel-preview",
			Localizable("Shows the bind wheel layout and which slot is currently selected."),
			Localizable("Hold the wheel key to see the selection move; release it to run the highlighted slot."),
			Localizable("If the wheel feels crowded, remove unused slots in the editor card."),
		},
		{
			"deck:tclient-chat-binds-kaomoji",
			Localizable("Manages the kaomoji shortcuts used by chat binds."),
			Localizable("Add or edit entries, then use the bind in chat to send the text."),
			Localizable("Very long kaomoji are split by the chat length limit."),
		},
		{
			"deck:tclient-chat-binds-other",
			Localizable("Manages additional custom chat shortcuts that do not belong to the other groups."),
			Localizable("Add a shortcut, then use it in chat to send the text."),
			Localizable("Remove shortcuts you no longer use to keep the list readable."),
		},
		{
			"deck:tclient-chat-binds-warlist",
			Localizable("Manages chat shortcuts aimed at players on your war list."),
			Localizable("Add one shortcut per message; it is used when the target is on the list."),
			Localizable("Keep messages short so they fit in one chat line."),
		},
		{
			"deck:tclient-configs-actions",
			Localizable("Reviews configuration values that differ from their defaults and resets them."),
			Localizable("Filter the list by name, select the values to change back, then confirm before resetting."),
			Localizable("Resetting restores defaults immediately, so note down values you want to keep."),
		},
		{
			"deck:tclient-info-developers",
			Localizable("Lists TClient developers, contributors and project links."),
			Localizable("Open a link to reach the project."),
			Localizable("External links open in the browser."),
		},
		{
			"deck:tclient-info-files",
			Localizable("Shortcuts that open your QmClient settings, profiles, war list and chat binds, plus configuration packages."),
			Localizable("Click \"QmClient Settings\", \"Profiles\", \"War List\" or \"Chat Binds\" to open that file, then use \"Configuration packages\" to export or import a whole set."),
			Localizable("An import replaces settings and binds and then restarts the client, so export your current setup first."),
		},
		{
			"deck:tclient-profiles",
			Localizable("Saves and loads complete client profiles, including name, chat settings and bind wheel presets."),
			Localizable("Create a profile, edit it, then switch profiles before playing on a different server."),
			Localizable("Loading a profile overwrites current settings, so save first if unsure."),
		},
		{
			"deck:tclient-status-bar-preview",
			Localizable("Previews the composed status bar output with your current settings."),
			Localizable("Change values in the settings card and watch this preview update."),
			Localizable("The preview uses your own Tee; other players may look different."),
		},
		{
			"deck:tclient-status-bar-settings",
			Localizable("Sets the position, size and text style of the status bar."),
			Localizable("Choose a screen position and font size, then check the preview card for wrapping."),
			Localizable("A bar too close to the edge is cut on unusual aspect ratios."),
		},
		{
			"deck:tclient-warlist",
			Localizable("Maintains the list of hostile players and groups plus the rules used to display them."),
			Localizable("Add players or groups, then choose which markers and chat highlights apply to them."),
			Localizable("The list only affects your client and is not shared with other players."),
		},
		{
			"deck:tee-glow",
			Localizable("Colors a halo around each tee by its race team, so teammates and opponents are easy to tell apart."),
			Localizable("Turn on \"Team tee glow\", then set \"Team tee glow intensity\" and \"Team tee glow halo size\"; \"Team 0 glow\" handles players without a team."),
			Localizable("A big \"Team tee glow halo size\" can wash out outlines and map detail, so start low and raise it slowly."),
		},
		{
			"deck:tee-identity",
			Localizable("Shows your tee and dummy side by side so you can set skin, colors and default eyes for each of them."),
			Localizable("Click the \"Player\" or \"Dummy\" preview, type a name under \"Your skin\", then turn on \"Custom colors\" to pick \"Body\" and \"Feet\"."),
			Localizable("Only the selected tee changes, so switch to \"Dummy\" before editing the colors you want on your dummy."),
		},
		{
			"deck:tee-skin-list",
			Localizable("Searches every skin the client knows and keeps favorite and recently used ones in separate tabs."),
			Localizable("Type in the search field or switch between \"All\", \"Favorites\" and \"Recent\", then click a skin to apply it and use the plus icon to queue it."),
			Localizable("Every click applies the skin at once, so star the ones you like before you start browsing."),
		},
		{
			"deck:tee-skin-options",
			Localizable("Decides which skins the client downloads and lists, and can add a prefix to load prefixed skin variants."),
			Localizable("Tick \"Download skins\", \"Download community skins\" or \"Vanilla skins only\", then type a \"Skin prefix\" or use a preset such as \"kitty\"."),
			Localizable("\"Vanilla skins only\" hides every custom skin, so turn it off if your skin suddenly turns default."),
		},
		{
			"deck:tee-skin-queue",
			Localizable("Rotates your tee through a saved list of skins, either on a timer or at a random spot when a map starts."),
			Localizable("Turn on \"Enable rotation\", set \"Switch interval\" in milliseconds, then add skins with the plus icon in the skin list; \"Random skin on map join\" picks the start."),
			Localizable("Set \"Switch interval\" to 0 to stop timed switching and keep the queue for manual or map-join changes."),
		},
		{
			"deck:tee7-editor",
			Localizable("Edits individual Tee 7 skin parts and previews the result."),
			Localizable("Select a body part, change its sprite or color, then save the skin."),
			Localizable("Parts are stored in the skin file; unsaved changes are lost when leaving the page."),
		},
		{
			"qm:appearance_preset",
			Localizable("A ready-made look: the LXGW WenKai font, UI size, animations, panel transparency and UI colors, listed before you apply."),
			Localizable("Read the listed rows, then press \"Apply Qm recommended appearance\"; if the font is missing, the panel says so and your current fonts stay."),
			Localizable("Applying overwrites your font, UI scale, animation and panel colors at once, so note the values you like first."),
		},
		{
			"qm:background_3d",
			Localizable("Draws drifting 3D shapes such as cubes, hearts or rings in the background, with size, speed, color and glow."),
			Localizable("Tick \"Enable 3D background particles\", pick a \"Particle type\", then set \"Particle count\", \"Particle speed\" and \"Particle color\"."),
			Localizable("High particle counts and trails cost frame rate; lower \"Particle count\" if your FPS drops."),
		},
		{
			"qm:better_scoreboard",
			Localizable("Adds point lookup, an after-death scoreboard, scrolling for crowded servers and a name filter to the scoreboard."),
			Localizable("Turn on \"Better scoreboard\", then enable what you need: \"Scoreboard point check\", \"Show scoreboard after death\" or \"Scroll mode\"."),
			Localizable("\"Scoreboard filter\" keeps only players whose name or clan matches, so clear it when the scoreboard looks empty."),
		},
		{
			"qm:bind_editor",
			Localizable("Binds any command to a key, mouse button or wheel direction, including combinations with Ctrl, Alt, Shift and Win."),
			Localizable("Click a key on the keyboard picture or under \"Mouse\", then add a command with \"Add function\" or use the capture button to move an existing bind."),
			Localizable("One key can hold several commands, so remove single entries under \"Bound functions\" instead of clearing the key."),
		},
		{
			"qm:bind_overview",
			Localizable("Lists every bound key, grouped by purpose such as Movement, Weapon, Chat or Dummy."),
			Localizable("Type in the search field to filter, switch between \"Sort by key\" and \"Sort by command\", and press \"Copy all\" to copy the rows."),
			Localizable("\"Copy all\" only copies the filtered rows, so clear the search first if you want your whole list."),
		},
		{
			"qm:bind_status_hud",
			Localizable("Adds HUD rows for stuck keys, hammer state, dummy control and dummy copy status."),
			Localizable("Tick any of \"Show key stuck status\", \"Show hammer status\", \"Show dummy control status\" or \"Show dummy copy status\"."),
			Localizable("Start with one or two rows; all four at once crowd the corner during a run."),
		},
		{
			"qm:block_words",
			Localizable("Replaces or hides chat lines that match words on your own list before they are shown to you."),
			Localizable("Turn on \"Enable word filter list\", type the words into \"Word Filter\" separated by commas, then choose \"Replace mode\" or \"Hide player messages\" under \"Behavior\"."),
			Localizable("\"Hide player messages\" removes the whole line, including team chat that merely mentions a blocked word."),
		},
		{
			"qm:camera_view",
			Localizable("Camera effects for gameplay: camera drift, speed-based FOV, instant zoom reverse, UI scale and a fixed aspect ratio."),
			Localizable("Turn on \"Enable camera drift\" or \"Enable dynamic FOV\", then tune \"Drift intensity\" and \"Dynamic FOV intensity\"; use \"Aspect ratio preset\" to force a view shape."),
			Localizable("Drift and dynamic FOV are cosmetic and can make aiming feel floaty, so start with a low intensity in a warm-up map."),
		},
		{
			"qm:chat_bubble",
			Localizable("Shows each player's latest chat message in a bubble above their head, in addition to the chat box."),
			Localizable("Turn on \"Show chat bubbles above players\", then adjust \"Duration\", \"Bubble opacity\" and \"Font size\", or pick your own colors."),
			Localizable("Zen mode hides chat bubbles even when this is on, and a long \"Duration\" leaves old lines on screen."),
		},
		{
			"qm:chat_completion",
			Localizable("Colors and opacity of the popup list that suggests chat commands and emoticons while you type in chat."),
			Localizable("Set \"Background color\" and \"Background opacity\", then \"Text color\", \"Selected text color\" and \"Selection background color\"; a preview follows."),
			Localizable("A see-through panel looks fine in the menu but is hard to read in game, so check it on a bright map before keeping it."),
		},
		{
			"qm:collision_hitbox",
			Localizable("Draws collision and interaction ranges on the map: tee collision, pickups, hammer, hook, lasers and explosions."),
			Localizable("Tick \"Show hitbox mode\", choose the ranges you need, then set \"Player range\" to \"Local only\", \"Local + Dummy\" or \"All players\"."),
			Localizable("It stays on for every match, so switch it off before real races to keep the screen and your aim clean."),
		},
		{
			"qm:console",
			Localizable("Font size, background opacity, colors and command highlighting of the local console you open with F1."),
			Localizable("Set \"Console font size\" and \"Console background opacity\", tick \"Highlight console commands\", then choose a \"Console color scheme\"."),
			Localizable("Your own colors appear only under \"Custom\"; press \"Reset console appearance\" if the text becomes hard to read."),
		},
		{
			"qm:coords",
			Localizable("Shows X and Y coordinates on players' nameplates, for yourself, for other players, or both."),
			Localizable("Turn on \"Show own coordinates\" or \"Show other players' coordinates\", then choose \"Show X\" and \"Show Y\"."),
			Localizable("\"X alignment hint with me\" colors another player's X after it matches yours for the set \"Detection time\"."),
		},
		{
			"qm:debug_graph",
			Localizable("Graphs RTT, prediction, snapshot age and frame rate in a panel shown while the debug graph toggle is on."),
			Localizable("Bind \"Global toggle key\" to show or hide the panel, and raise \"Panel opacity\" if the graphs are hard to read."),
			Localizable("It is a diagnostic overlay, not gameplay data; lower the opacity when it covers the map."),
		},
		{
			"qm:debug_mode",
			Localizable("Logs slow frames and startup stutter timings so you can report why the game felt laggy."),
			Localizable("Tick \"Debug mode\" to turn all options on, then set \"Performance debug log threshold (ms)\" to choose which frames get logged."),
			Localizable("Switch it off when you are done; these logs are for bug reports, not daily play."),
		},
		{
			"qm:dummy_miniview",
			Localizable("Draws a small second view of your other tee in the HUD, so you can watch both tees at the same time."),
			Localizable("Turn on \"Enable dummy window\", then set \"Dummy window size\" and \"Dummy window zoom\"; \"Only show when the other Tee is not on screen\" hides it otherwise."),
			Localizable("It only appears with a connected dummy and renders an extra scene each frame, so lower \"Dummy window size\" if FPS drops."),
		},
		{
			"qm:dynamic_island",
			Localizable("Tweaks the top HUD pill and its extras: background color, team, song title, hook and switch countdowns."),
			Localizable("Untick \"Use original style\" to reach \"Background color\", the countdown options and \"Always show song title and artist\"; \"Show team\" works in both styles."),
			Localizable("\"Use original style\" hides the color and countdown options below it."),
		},
		{
			"qm:emoticons",
			Localizable("Show or hide other players' large and launched emoticons, and choose the key that opens the emote launcher."),
			Localizable("Untick \"Show other players' large emoticons\" or \"Show other players' launched emoticons\"; click \"Launch emote key\" to assign the key that turns launching on and off."),
			Localizable("The launcher key only switches launch mode, so you still choose the emote with your normal emote key."),
		},
		{
			"qm:entity_overlay",
			Localizable("Opacity of entity layers on the map such as death, freeze, unfreeze, teleport, checkpoint and switch tiles."),
			Localizable("Drag a slider like \"Death opacity\" or \"Teleport opacity\" from 0 to 100%, or use \"Tune layer opacity\" to fade the whole entity layer."),
			Localizable("Fading death and freeze tiles helps you see tees and weapons behind them, but the exact tile edge gets harder to judge."),
		},
		{
			"qm:favorite_maps",
			Localizable("Your list of favorite maps, each shown with its map type, with quick copy and remove actions."),
			Localizable("Click a map name to copy it and press the X button to remove it; add maps with the \"Favorite map\" checkbox in the in-game server info page."),
			Localizable("Favorites match the exact map name and also feed the \"Favorite map\" tab in the server browser."),
		},
		{
			"qm:focus_mode",
			Localizable("A calm-play mode that hides the HUD, names, effects, chat and sounds you pick, so only the game stays on screen."),
			Localizable("Turn on \"Enable Zen mode\", pick what to hide under \"Interface\", \"Players\", \"Visuals\", \"Audio\" and \"Chat\", then set the \"Zen mode key\" for quick switching."),
			Localizable("Bind \"Zen mode key\" before you need it: while Zen mode is on, hidden HUD, chat and scoreboard stay hidden."),
		},
		{
			"qm:friend_notify",
			Localizable("Watches your friend list and reports friends coming online or joining your map, with optional automatic greeting."),
			Localizable("Turn on \"Notify when friends come online\" and \"Auto refresh server list\", then tune \"Refresh interval\"; \"Auto greet friends entering map\" is optional."),
			Localizable("Auto greet posts a public chat line a few seconds after a friend joins, so leave \"Greeting text\" empty on strict servers."),
		},
		{
			"qm:gores",
			Localizable("Hammer and gun switching made for Gores (KOG) servers, with optional Axiom login and fast input."),
			Localizable("Turn on \"Enable Gores mode\" or let \"Auto enable in Gores mode\" do it, pick options like \"Auto weapon switch\", and bind \"Gores mode key\"."),
			Localizable("Leave it off on normal servers: the auto switch overrides your weapon keys, so your pick can change before you shoot."),
		},
		{
			"qm:gores_actor",
			Localizable("Writes a chat message, and optionally sends an emoticon, whenever your tee dies in water."),
			Localizable("Turn on \"Auto chat in water\", type the \"Chat message\" you want, and optionally enable \"Send emoticon in water\" with its ID."),
			Localizable("The message is sent on every water death, so keep it short; leaving \"Chat message\" empty disables sending."),
		},
		{
			"qm:gores_drown_board",
			Localizable("Shows a small board counting drown deaths for players on your team on Gores maps."),
			Localizable("Tick \"Show Gores drown board\"; set \"Players shown\" and \"Card opacity\", and add \"Show player Tee\" for avatars."),
			Localizable("It only appears in Gores mode; enable \"Include Team 0\" to count players without a team."),
		},
		{
			"qm:hj_assist",
			Localizable("Automates what happens right after a hammer wakes your frozen tee: unspec, tee switch, chat close and a hint."),
			Localizable("Tick the actions you want, such as \"Auto unspec on unfreeze\" or \"Auto team lock\"; \"Lock delay\" and \"Spectator opacity\" appear below."),
			Localizable("Turn on one option at a time: switching tee or closing chat can throw away a message you are still typing."),
		},
		{
			"qm:hud_notifications",
			Localizable("Turns important server prompts and Echo messages into short popups instead of plain lines in the chat box."),
			Localizable("Turn on \"Show important server prompts as notifications\" or \"Route Echo messages to notifications\", then set \"Notification hold time\" and \"Notification text size\"."),
			Localizable("\"Advanced options\" adds category filters and colors; join, version, rules and help texts still stay in chat."),
		},
		{
			"qm:ime",
			Localizable("The look and behavior of the IME candidate bar that appears when you type Chinese, Japanese or Korean in chat."),
			Localizable("Turn on \"New IME\" for the new bar and \"Auto manage IME while typing\" to switch input mode with the chat box; edit colors under \"Edit color\"."),
			Localizable("If the bar covers your chat, lower \"Opacity\" or \"IME font size\" first; \"Reset IME appearance\" brings back the defaults."),
		},
		{
			"qm:input_overlay",
			Localizable("Draws a keyboard and mouse on screen that light up with your presses, so viewers can follow your inputs."),
			Localizable("Tick \"Show inputs\", then size and place it with \"Keyboard size\", \"Mouse size\", \"Opacity\" and the position sliders."),
			Localizable("Keep it small and in a corner; a large overlay covers the map and hides what matters."),
		},
		{
			"qm:jump_hint",
			Localizable("Shows fractional position numbers for edge jumps as on-screen text, so you can time a jump at the right spot."),
			Localizable("Tick \"Position jump hint\", then move it with \"Horizontal position\" and \"Vertical position\" in percent and pick a readable \"Font size\"."),
			Localizable("Keep it small and near an edge: the hint draws over the map and can cover the tile you are aiming at."),
		},
		{
			"qm:key_binds",
			Localizable("Fast keys for common dummy, aim and rescue actions: HDF, DF, 45-degree aim, jumps and the trajectory line."),
			Localizable("Click a row such as \"HDF\" or \"Left jump\", press the key you want, and use the trash icon to remove a bind."),
			Localizable("A key that already has a command is overwritten, so recheck your other binds after assigning keys here."),
		},
		{
			"qm:lyrics",
			Localizable("Shows the current song's lyric line in the Dynamic Island and lets you pick which music app supplies it."),
			Localizable("Tick \"Enable lyrics\" and \"Show lyrics inside Dynamic Island\", then enable one source such as \"Enable Netease music Hook\" or \"Enable Spotify lyrics\"."),
			Localizable("Lyrics live in the Dynamic Island, so they stay hidden until that option is on."),
		},
		{
			"qm:map_upload",
			Localizable("Sends a saved .map file from your client to the public test server and reports progress and errors."),
			Localizable("Press \"Select map\" to choose a file, check the target server and player shown for the upload, then press \"Upload map\"; \"Cancel\" stops it."),
			Localizable("After a successful upload, log in with F2 and run the change_map command; files must stay under 64 MiB."),
		},
		{
			"qm:mini_features",
			Localizable("A grab bag of small convenience switches that do not deserve their own card: chat completion, the Qm scoreboard badge, tune zone colors, message merging, repeat and combo."),
			Localizable("Switch on only the ones you recognize; chat completion affects the chat input, and \"Show Qm badge\" adds the Qm marker next to names on the scoreboard."),
			Localizable("Leave \"Automatic updates\" off if you update by hand: it can restart the client to install a build."),
		},
		{
			"qm:pie_menu",
			Localizable("A radial menu that opens near a player and offers quick actions such as whisper, spectate or copy skin."),
			Localizable("Turn on \"Enable pie menu\", bind the \"Pie menu\" command on the Bind page, then hold that key near a player and release on an option."),
			Localizable("Options only exist within \"Detection distance\"; turn off the ones you never use to keep the wheel short."),
		},
		{
			"qm:player_stats",
			Localizable("Adds HUD lines for alive time, rescue and freeze counts and hook left/right ratio, plus a map progress bar."),
			Localizable("Enable \"Show player stats HUD\"; tick \"Map progress bar\", then pick \"Use embedded HUD progress bar\" or place the bar with the sliders."),
			Localizable("The map progress bar only fills on Gores maps; the other three lines work on any server."),
		},
		{
			"qm:qiafen",
			Localizable("Automatic chat replies when someone else writes a keyword you set, optionally sent by your dummy."),
			Localizable("Add rules in \"Rule list\", fill the answer in \"Reply content\", then under \"Trigger limits\" turn on \"Enable keyword reply\" and set \"Auto reply cooldown\"."),
			Localizable("Replies go to public chat and share one cooldown, so keep keywords specific or a common word will spam the server."),
		},
		{
			"qm:self_kill_protection",
			Localizable("Two keys that block or allow killing yourself with K, so you cannot die by accident while racing."),
			Localizable("Assign \"Self-kill off\" and \"Self-kill on\"; after pressing the off key, K is ignored until you press the on key."),
			Localizable("Only kills sent by your own client are blocked and server commands still work, so press your off key again after racing."),
		},
		{
			"qm:skin_appearance",
			Localizable("Adds an outline to skins, slowly cycles your custom Tee colors, and draws a shadow behind emoticons."),
			Localizable("Turn on \"Skin outline for self and dummy\" or \"Skin outline for other players\", then tune width and opacity; \"Cycle custom Tee hue\" animates custom colors."),
			Localizable("\"Cycle custom Tee hue\" only works with custom colors and does nothing while the TClient rainbow Tee option is on."),
		},
		{
			"qm:skin_transition",
			Localizable("Plays a short effect when a tee's skin changes, and can copy the skin of the tee you just hammered."),
			Localizable("Turn on \"Skin transition animation\", choose a \"Skin transition type\" and \"Animation range\", then set \"Skin transition duration\" and \"Skin transition intensity\"."),
			Localizable("Enable \"Hammer skin steal\" only if you want your skin to change on every hammer hit, since other players see it too."),
		},
		{
			"qm:solo_split",
			Localizable("Puts you and your dummy in two separate teams so each can run a solo map, with keys to enter and leave."),
			Localizable("Tick \"Auto-connect dummy\" to connect the dummy for you, then bind \"Solo split key\" to toggle, or use the enter and leave keys."),
			Localizable("Pressing the key again leaves the split and returns both Tees to the team set in \"Team when leaving solo split\"."),
		},
		{
			"qm:spectator_mode",
			Localizable("Smooths the camera while you free-spectate with the mouse in live games and demos, so pans glide instead of snapping."),
			Localizable("Turn on \"Smooth free spectator camera\" and set \"Cinematic smoothness\": low values stay snappy, high values drift slowly."),
			Localizable("Only free spectating is affected, not normal play; set \"Cinematic smoothness\" to 0 to keep instant camera response."),
		},
		{
			"qm:steam",
			Localizable("Starts Steam automatically when the client is launched from outside, so Steam records your playtime."),
			Localizable("Tick \"Launch Steam automatically when starting externally\"; if Steam is not found, type its path or press \"Detect Steam again\"."),
			Localizable("Leave the path empty to auto-detect; fill it in only when the status says \"Steam not found\"."),
		},
		{
			"qm:streamer",
			Localizable("Hides names, skins, flags and blocked words on screen, so a stream does not show other players' details."),
			Localizable("Tick \"Replace non-friend names with ID\", \"Replace non-friend skins with default\" or \"Use default flags on scoreboard\"; words come from a \"Word list file\"."),
			Localizable("It only changes what you see: your own name, chat and skin are still sent to the server and stay visible to others."),
		},
		{
			"qm:system_media_controls",
			Localizable("Links the music playing on your system with the system media controls, and can show its title in the top-left HUD."),
			Localizable("Tick \"Enable system media control\", then \"Show song info in top-left corner\"; the Previous, Play/Pause and Next buttons drive the track."),
			Localizable("Nothing appears until a media app is playing; use the buttons to check which app the system reports."),
		},
		{
			"qm:tooltip",
			Localizable("The look of the help bubbles that appear when you hover over buttons, settings and bind rows."),
			Localizable("Pick \"Tooltip background color\" and \"Tooltip text color\", drag \"Tooltip font size\", and keep \"Tooltip bounce animation\" on for a small pop."),
			Localizable("Keep the text light on a dark background; press \"Reset tooltip appearance\" to return to the defaults."),
		},
		{
			"qm:translate",
			Localizable("Shows a translated copy of received chat lines and can translate your own messages before they are sent."),
			Localizable("Turn on \"Auto translate received messages\", pick a \"Translation service\", set \"Translate received messages to\", then press \"Test translation\"."),
			Localizable("Each service needs its own API key and network access; run \"Test translation\" after changing keys."),
		},
		{
			"qm:translate_ui",
			Localizable("Colors of the translate button in the chat input and of the language menu that button opens."),
			Localizable("Set \"Button - Disabled\" and \"Button - Enabled\" for the button, then \"Menu background\" and the two menu option rows for the popup."),
			Localizable("\"Button - Enabled\" only shows while automatic outgoing translation is on; \"Reset translation colors\" undoes your changes."),
		},
		{
			"qm:voice",
			Localizable("Turns on voice chat and sets your microphone, playback volume, voice activation and audio devices."),
			Localizable("Tick \"Enable voice\", then open \"Advanced options\" to pick \"Input device\" and \"Output device\"; \"Auto unmute when speaking\" frees your mute key."),
			Localizable("Check your devices before tweaking thresholds; most silent-mic cases are simply the wrong input device."),
		},
		{
			"qm:water_hammer",
			Localizable("Marks teammates who hold hammer while stuck in death or freeze areas, so you can see who is hammering them."),
			Localizable("Tick \"Highlight teammates hammering in death or freeze areas\"; it works on your own team only and needs no key bind."),
			Localizable("Use it to find the teammate who needs a wake-up; enemies are never highlighted, so do not look for them here."),
		},
		{
			"qm:weapon_animation",
			Localizable("Animates the weapon in your hand when you switch or reload, plus an optional hammer rotation mode."),
			Localizable("Turn on \"Weapon switch animation\" or \"Play a flip animation while reloading weapons\", then pick an \"Animation range\" and tune duration, distance and rotation."),
			Localizable("\"Hammer Mode\" applies even with both animations off, so check it if your hammer keeps turning with the cursor."),
		},
		{
			"qm:weapon_trajectory",
			Localizable("Draws a guide line from a weapon to where a shot would go, and can preview the ninja flight path."),
			Localizable("Set \"Display mode\" to \"Show on key\" or \"Always show\", then pick \"Pistol guide line\" and \"Predict ninja path\", plus color, \"Line width\" and \"Opacity\"."),
			Localizable("\"Show on key\" needs a key bound to +showweapontrajectory; if nothing appears, switch to \"Always show\"."),
		},
		{
			"tclient:anti-latency-tools",
			Localizable("Adjusts helpers that hide latency, such as removing anti-ping and predicting margins inside freeze."),
			Localizable("Keep the defaults unless you play with high ping; then try one option at a time and compare."),
			Localizable("These options only change prediction on your client; what other players see is not affected."),
		},
		{
			"tclient:auto-reply",
			Localizable("Sends an automatic chat reply when someone whispers you while you are muted or away."),
			Localizable("Write the reply text; choose whether it triggers for muted players and whether the panel starts minimized."),
			Localizable("Do not use it to spam: servers may punish repeated automatic messages."),
		},
		{
			"tclient:background-draw",
			Localizable("Draws temporary strokes on your own screen background while a bound key is held and the mouse moves."),
			Localizable("Bind a key to \"Draw at mouse position\", hold it in game and move the mouse to draw, then set color, width and how long strokes stay visible."),
			Localizable("Strokes are local only and vanish when you leave the map; set the fade time to 0 to keep them until you draw again."),
		},
		{
			"tclient:cursor",
			Localizable("Sets the scale of the in-game cursor."),
			Localizable("Increase the scale if the pointer is hard to see; decrease it for a cleaner look."),
			Localizable("A very large cursor can hide small UI elements underneath it."),
		},
		{
			"tclient:execute-on-join",
			Localizable("Runs commands automatically when you connect to a server, split into a connect part and a join part."),
			Localizable("Fill in the connect and join command fields, separate several commands with semicolons, and raise the delay if the server is not ready yet."),
			Localizable("Commands run once per connection, not on every map change; remove the parts that print errors."),
		},
		{
			"tclient:finish-name",
			Localizable("Renames your Tee automatically when you get close to the finish, so the finish is recorded under the name you prepared."),
			Localizable("Enable \"Try to rename before finishing\", then fill in the finishing name and run to the finish to test it."),
			Localizable("It only changes your name, not your skin or clan; names that are too long are cut off."),
		},
		{
			"tclient:font",
			Localizable("Chooses the font family and weight used by the menus."),
			Localizable("Pick a family; if it offers several weights, choose one that stays readable at small sizes."),
			Localizable("Fonts without CJK glyphs show fallback squares for Chinese text."),
		},
		{
			"tclient:ghost-tools",
			Localizable("Shows the real position of other players as a translucent ghost next to the position the client predicts."),
			Localizable("Enable \"Show unpredicted ghosts of other players\", then tune the predicted and unpredicted ghost opacity."),
			Localizable("Ghosts only affect your own view; bind the toggle key or lower the opacity when the screen gets crowded."),
		},
		{
			"tclient:hud",
			Localizable("Adjusts TClient-specific HUD elements that are not part of the standard DDNet HUD."),
			Localizable("Open the card and enable only the elements you want; each option explains what it draws."),
			Localizable("A crowded HUD hides the game, so start with one element and add more if needed."),
		},
		{
			"tclient:improved-anti-ping",
			Localizable("Fine-tunes the improved anti-ping prediction that smooths other players' movement."),
			Localizable("Start from the default value; raise it slightly if other players look jumpy, lower it if you seem behind them."),
			Localizable("Extreme values make movement feel wrong, so change one step at a time."),
		},
		{
			"tclient:input",
			Localizable("Groups input helpers: fast input, automatic margin, sub-tick aiming and cursor behavior."),
			Localizable("Enable fast input if your key presses feel late; turn on auto margin to stop the cursor sticking to screen edges."),
			Localizable("Fast input changes how inputs are timed, so disable it if a server reports unusual behavior."),
		},
		{
			"tclient:pet",
			Localizable("Shows a small companion that follows your Tee and reacts to what you do."),
			Localizable("Enable the pet, pick its skin and size, then move around to watch how it follows."),
			Localizable("Pets are purely cosmetic; turn them off if the extra drawing hurts performance."),
		},
		{
			"tclient:player-indicator",
			Localizable("Draws a marker on selected players so you can find them in the world."),
			Localizable("Enable the indicator; choose its distance mode and whether war-list players get a different color."),
			Localizable("Markers are drawn on your screen only and never change the game itself."),
		},
		{
			"tclient:rainbow",
			Localizable("Applies an animated rainbow color to selected elements such as nameplates or the hook."),
			Localizable("Choose which parts get the rainbow; adjust speed and saturation to taste."),
			Localizable("Fast flashing colors are hard to read, so keep the speed moderate."),
		},
		{
			"tclient:tee-status-bar",
			Localizable("Shows how many Tees in your team are frozen and how many are still active, as a row of Tee icons."),
			Localizable("Enable the frozen Tee HUD, choose whether to count active or frozen Tees, then set the icon size and the maximum number of rows."),
			Localizable("It only counts your own team and only appears on your screen; lower the icon size when it covers the crosshair."),
		},
		{
			"tclient:tee-trails",
			Localizable("Shows a trail behind Tees so their recent movement stays visible."),
			Localizable("Enable trails, choose length and fade, then turn off other players' trails if the screen gets crowded."),
			Localizable("Trails add drawing work, so lower the length on low-end machines."),
		},
		{
			"tclient:tile-outlines",
			Localizable("Draws outlines around map tiles so block edges are easier to see."),
			Localizable("Enable the outlines, choose color and thickness, then test on a map with many different tiles."),
			Localizable("Outlines can be distracting in fast maps; lower the opacity instead of disabling them."),
		},
		{
			"tclient:visual-effects",
			Localizable("Turns on extra visual effects such as tiny Tees and the jelly-like Tee animation."),
			Localizable("Enable the effects you like; turn tiny Tees off if distant players become hard to see."),
			Localizable("These effects are cosmetic and can cost a little performance on weaker GPUs."),
		},
		{
			"tclient:visual-font-cursor",
			Localizable("Changes the font used by the menus and how large the in-game cursor is drawn."),
			Localizable("Pick a font family and weight here; use the cursor scale to make the pointer easier to see on large screens."),
			Localizable("Custom fonts only cover the characters they include, so pick a font with CJK support if Chinese text looks different."),
		},
		{
			"tclient:visual-nameplates",
			Localizable("Controls what appears next to other players' names, such as clan tags, friend marks and client identifiers."),
			Localizable("Enable the badges you want; tune the nameplate size where it overlaps the game view."),
			Localizable("Too many badges make busy servers hard to read, so keep only the ones you actually use."),
		},
		{
			"tclient:voting",
			Localizable("Changes how the vote menu looks and how you interact with running votes."),
			Localizable("Adjust the display options; open a vote to check the layout before joining a busy server."),
			Localizable("This only affects your own screen; votes still follow the server rules."),
		},

	};
}

const SCardWikiCopy *FindCardWikiCopy(const char *pStableId)
{
	if(pStableId == nullptr || pStableId[0] == '\0')
		return nullptr;
	// 卡片标题栏每帧都要判断有没有 ⓘ，所以按 id 升序表做二分，避免逐条线性扫描。
	const SCardWikiCopy *pBegin = std::begin(s_aCardWikiCopies);
	const SCardWikiCopy *pEnd = std::end(s_aCardWikiCopies);
	const SCardWikiCopy *pFound = std::lower_bound(pBegin, pEnd, pStableId, [](const SCardWikiCopy &Copy, const char *pKey) {
		return str_comp(Copy.m_pStableId, pKey) < 0;
	});
	if(pFound == pEnd || str_comp(pFound->m_pStableId, pStableId) != 0)
		return nullptr;
	return pFound;
}

bool HasCardWikiCopy(const char *pStableId)
{
	return FindCardWikiCopy(pStableId) != nullptr;
}

size_t CardWikiCopyCount()
{
	return std::size(s_aCardWikiCopies);
}

const SCardWikiCopy *CardWikiCopyAt(size_t Index)
{
	return Index < std::size(s_aCardWikiCopies) ? &s_aCardWikiCopies[Index] : nullptr;
}
