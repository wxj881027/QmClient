By DDNet Wiki contributors. [Source revision 25135](https://wiki.ddnet.org/wiki/Special:PermanentLink/25135) · [Contributors and edit history](https://wiki.ddnet.org/w/index.php?title=Settings%20and%20Commands&action=history) · [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/). QmClient reformatted the article and omitted site navigation and embedded media.

## Map Settings

| Setting | Description | Default | Min | Max |
| --- | --- | --- | --- | --- |
| `sv_hit` | Whether players can hammer/grenade/laser each other or not | 1 | 0 | 1 |
| `sv_endless_drag` | Turns endless hooking on/off | 0 | 0 | 1 |
| `sv_freeze_delay` | How many seconds the players will remain frozen (applies to all except delayed freeze in switch layer & deepfreeze) | 3 | 1 | 30 |
| `sv_save_worse_scores` | Whether to save worse scores when you already have a better one | 1 | 0 | 1 |
| `sv_pauseable` | Whether players can pause their char or not | 0 | 0 | 1 |
| `sv_team` | Teams configuration (0 = off, 1 = on but optional, 2 = must play only with teams, 3 = forced random team only for you) | 1 | 0 | 3 |
| `sv_min_team_size` | Minimum team size (finishing in a team smaller than this size gives you no teamrank) | 2 | 1 | 64 |
| `sv_max_team_size` | Maximum team size | 64 | 1 | 64 |
| `sv_old_laser` | Whether lasers can hit you if you shot them and that they pull you towards the bounce origin (0 for all new maps) or lasers can't hit you if you shot them, and they pull others towards the shooter | 0 | 0 | 1 |
| `sv_no_weak_hook` | Whether to use an alternative calculation for world ticks, that makes the hook behave like all players have strong. | 0 | 0 | 1 |
| `sv_reset_pickups` | Whether the weapons are reset on passing the start tile or not | 0 | 0 | 1 |
| `sv_show_others_default` | Whether players see others by default (2 for own team) | 0 | 0 | 2 |
| `sv_plasma_range` | How far will the plasma gun track tees | 700 | 1 | 99999 |
| `sv_plasma_per_sec` | How many shots does the plasma gun fire per seconds | 3 | 0 | 50 |
| `sv_dragger_range` | How far will the dragger track tees | 700 | 1 | 99999 |
| `sv_solo_server` | Set server to solo mode (no player interactions, has to be set before loading the map) | 0 | 0 | 1 |
| `sv_old_teleport_weapons` | Teleporting of all weapons (deprecated, use special entities instead) | 0 | 0 | 1 |
| `sv_old_teleport_hook` | Hook through teleporter (deprecated, use special entities instead) | 0 | 0 | 1 |
| `sv_teleport_hold_hook` | Hold hook when teleported | 0 | 0 | 1 |
| `sv_teleport_lose_weapons` | Lose weapons when teleported (useful for some race maps) | 0 | 0 | 1 |
| `sv_deepfly` | Allow fire non auto weapons when deep | 1 | 0 | 1 |
| `sv_destroy_bullets_on_death` | Destroy bullets when their owner dies | 1 | 0 | 1 |
| `sv_destroy_lasers_on_death` | Destroy lasers when their owner dies | 0 | 0 | 1 |

| Command | Arguments | Description |
| --- | --- | --- |
| `tune` | s\[tuning\] ?i\[value\] | Tune variable to value or show current value |
| `toggle_tune` | s\[tuning\] i\[value 1\] i\[value 2\] | Toggle tune variable |
| `tune_zone` | i\[zone\] s\[tuning\] i\[value\] | Tune in zone a variable to value |
| `tune_zone_enter` | i\[zone\] r\[message\] | which message to display on zone enter; use 0 for normal area |
| `tune_zone_leave` | i\[zone\] r\[message\] | which message to display on zone leave; use 0 for normal area |
| `mapbug` | s\[mapbug\] | Enable map compatibility mode using the specified bug (example: grenade-doubleexplosion@ddnet.tw) |
| `switch_open` | i\[switch\] | Whether a switch is deactivated by default (otherwise activated) |

## Server Settings

| Setting | Description | Default | Min | Max |
| --- | --- | --- | --- | --- |
| `password` | Password to the server | "" |  |  |
| `events` | Enable triggering of events, (eye emotes on some holidays in server, christmas skins in client). | 1 | 0 | 1 |
| `logfile` | Filename to log all output to | "" |  |  |
| `logappend` | Append to logfile instead of overwriting it every time | 1 | 0 | 1 |
| `loglevel` | Adjusts the amount of information in the logfile (-3 = none, -2 = error only, -1 = warn, 0 = info, 1 = debug, 2 = trace) | 0 | -3 | 2 |
| `stdout_output_level` | Adjusts the amount of information in the system console (-3 = none, -2 = error only, -1 = warn, 0 = info, 1 = debug, 2 = trace) | 0 | -3 | 2 |
| `console_output_level` | Adjusts the amount of information in the local/remote console (-3 = none, -2 = error only, -1 = warn, 0 = info, 1 = debug, 2 = trace) | 0 | -3 | 2 |
| `console_enable_colors` | Enable colors in console output | 1 | 0 | 1 |
| `sv_name` | Server name | "unnamed server" |  |  |
| `bindaddr` | Address to bind the client/server to | "" |  |  |
| `sv_ipv4only` | Whether to bind only to ipv4, otherwise bind to all available interfaces | 0 | 0 | 1 |
| `sv_port` | Port to use for the server (Only ports 8303-8310 work in LAN server browser, 0 to automatically find a free port in 8303-8310) | 0 | 0 | 0 |
| `sv_hostname` | Server hostname (0.7 only) | "" |  |  |
| `sv_map` | Map to use on the server | "Sunny Side Up" |  |  |
| `sv_max_clients` | Maximum number of clients that are allowed on a server | 64 | 1 | 64 |
| `sv_max_clients_per_ip` | Maximum number of clients with the same IP that can connect to the server | 4 | 1 | 64 |
| `sv_high_bandwidth` | Use high bandwidth mode. Doubles the bandwidth required for the server. LAN use only | 0 | 0 | 1 |
| `sv_register` | Register server with master server for public listing, can also accept a comma-separated list of protocols to register on, like 'ipv4,ipv6' | "1" |  |  |
| `sv_register_extra` | Extra headers to send to the register endpoint, comma separated 'Header: Value' pairs | "" |  |  |
| `sv_register_url` | Masterserver URL to register to | "[https://master1.ddnet.org/ddnet/15/register](https://master1.ddnet.org/ddnet/15/register)" |  |  |
| `sv_rcon_password` | Remote console password (full access) | "" |  |  |
| `sv_rcon_mod_password` | Remote console password for moderators (limited access) | "" |  |  |
| `sv_rcon_helper_password` | Remote console password for helpers (limited access) | "" |  |  |
| `sv_rcon_max_tries` | Maximum number of tries for remote console authentication | 30 | 0 | 100 |
| `sv_rcon_bantime` | The time a client gets banned if remote console authentication fails. 0 makes it just use kick | 5 | 0 | 1440 |
| `sv_auto_demo_record` | Automatically record demos | 0 | 0 | 1 |
| `sv_auto_demo_max` | Maximum number of automatically recorded demos (0 = no limit) | 10 | 0 | 1000 |
| `sv_tee_historian` | Activate the tee historian that writes complete gameplay data to disk (WARNING: This will use a lot of disk space) | 0 | 0 | 1 |
| `sv_vanilla_antispoof` | Enable vanilla Antispoof | 1 | 0 | 1 |
| `sv_dnsbl` | Enable DNSBL (DNS-based Blackhole List) | 0 | 0 | 1 |
| `sv_dnsbl_host` | Hostname of DNSBL provider to use for IP Verification | "" |  |  |
| `sv_dnsbl_key` | Optional Authentication Key for the specified DNSBL provider | "" |  |  |
| `sv_dnsbl_vote` | Block votes by blacklisted addresses | 0 | 0 | 1 |
| `sv_dnsbl_ban` | Automatically ban blacklisted addresses | 0 | 0 | 1 |
| `sv_dnsbl_chat` | Don't allow chat from blacklisted addresses | 0 | 0 | 1 |
| `sv_rcon_vote` | Only allow authed clients to call votes | 0 | 0 | 1 |
| `sv_player_demo_record` | Automatically record demos for each player | 0 | 0 | 1 |
| `sv_demo_chat` | Record chat for demos | 0 | 0 | 1 |
| `sv_server_info_per_second` | Maximum number of complete server info responses that are sent out per second (0 for no limit) | 50 | 0 | 10000 |
| `sv_van_conn_per_second` | Antispoof specific ratelimit (0 for no limit) | 10 | 0 | 10000 |
| `sv_sixup` | Enable sixup connections | 1 | 0 | 1 |
| `sv_skill_level` | Difficulty level for Teeworlds 0.7 (0: Casual, 1: Normal, 2: Competitive) | 1 | 0 | 2 |
| `debug` | Debug mode | 0 | 0 | 1 |
| `dbg_curl` | Debug curl | 0 | 0 | 1 |
| `dbg_stress` | Stress systems (Debug build only) | 0 | 0 | 0 |
| `http_allow_insecure` | Allow insecure HTTP protocol in addition to the secure HTTPS one. Mostly useful for testing. | 0 | 0 | 1 |
| `sv_welcome` | Message that will be displayed to players who join the server | "" |  |  |
| `sv_reserved_slots` | The number of slots that are reserved for special players | 0 | 0 | 64 |
| `sv_reserved_slots_pass` | The password that is required to use a reserved slot | "" |  |  |
| `sv_reserved_slots_auth_level` | Minimum rcon auth level needed to use a reserved slot. 4 = rcon auth disabled | 1 | 1 | 4 |
| `sv_hit` | Whether players can hammer/grenade/laser each other or not | 1 | 0 | 1 |
| `sv_endless_drag` | Turns endless hooking on/off | 0 | 0 | 1 |
| `sv_test_cmds` | Turns testing commands aka cheats on/off (setting only works in initial config) | 0 | 0 | 1 |
| `sv_freeze_delay` | How many seconds the players will remain frozen (applies to all except delayed freeze in switch layer & deepfreeze) | 3 | 1 | 30 |
| `sv_endless_super_hook` | Endless hook for super players on/off | 0 | 0 | 1 |
| `sv_hide_score` | Whether players scores will be announced or not | 0 | 0 | 1 |
| `sv_save_worse_scores` | Whether to save worse scores when you already have a better one | 1 | 0 | 1 |
| `sv_pauseable` | Whether players can pause their char or not | 0 | 0 | 1 |
| `sv_pause_messages` | Whether to show messages when a player pauses and resumes | 0 | 0 | 1 |
| `sv_pause_frequency` | The minimum allowed delay between /spec | 1 | 0 | 9999 |
| `sv_invite` | Whether players can invite other players to teams | 1 | 0 | 1 |
| `sv_invite_frequency` | The minimum allowed delay between invites | 1 | 0 | 9999 |
| `sv_tele_others_auth_level` | The auth level you need to tele others | 1 | 1 | 3 |
| `sv_emotional_tees` | Whether eye change of tees is enabled with emoticons = 1, not = 0, -1 not at all | 1 | -1 | 1 |
| `sv_emoticon_ms_delay` | The time in ms a player has to wait before allowing the next over-head emoticons | 3000 | 20 | 999999999 |
| `sv_global_emoticon_ms_delay` | The time in ms a player has to wait before allowing the next over-head emoticons that is send to all clients (this config must be higher or equal to sv\_emoticon\_ms\_delay to have an effect) | 3000 | 20 | 999999999 |
| `sv_eye_emote_change_delay` | The time in seconds between eye emoticons change | 1 | 0 | 9999 |
| `sv_chat_delay` | The time in seconds between chat messages | 1 | 0 | 9999 |
| `sv_team_change_delay` | The time in seconds between team changes (spectator/in game) | 3 | 0 | 9999 |
| `sv_info_change_delay` | The time in seconds between info changes (name/skin/color), to avoid ranbow mod set this to a very high time | 5 | 0 | 9999 |
| `sv_vote_time` | The time in seconds a vote lasts | 25 | 1 | 9999 |
| `sv_vote_map_delay` | The minimum time in seconds between map votes | 0 | 0 | 9999 |
| `sv_vote_delay` | The time in seconds between any vote | 3 | 0 | 9999 |
| `sv_vote_kick_delay` | The minimum time in seconds between kick votes | 0 | 0 | 9999 |
| `sv_vote_yes_percentage` | The percent of people that need to agree or deny for the vote to succeed/fail | 50 | 1 | 100 |
| `sv_vote_majority` | Whether No. of Yes is compared to No. of No votes or to number of total Players ( Default is 0 Y compare N) | 0 | 0 | 1 |
| `sv_vote_max_total` | How many people can participate in a vote at max (0 = no limit by default) | 0 | 0 | 64 |
| `sv_vote_veto_time` | Minutes of time on a server until a player can veto map change votes (0 = disabled) | 20 | 0 | 1000 |
| `sv_kill_delay` | The minimum time in seconds between kills | 1 | 0 | 9999 |
| `sv_map_window` | Map downloading send-ahead window | 15 | 0 | 100 |
| `sv_fast_download` | Enables fast download of maps | 1 | 0 | 1 |
| `sv_shotgun_bullet_sound` | Crazy shotgun bullet sound on/off | 0 | 0 | 1 |
| `sv_region_name` | Server region. Used for regional bans | "UNK" |  |  |
| `sv_sql_servername` | SQL Server name that is inserted into record table | "UNK" |  |  |
| `sv_savegames` | Enables savegames (/save and /load) | 1 | 0 | 1 |
| `sv_saveswapgames_delay` | Delay in seconds for loading a savegame or before swapping | 30 | 0 | 10000 |
| `sv_saveswapgames_penalty` | Penalty in seconds for saving or swapping position | 60 | 0 | 10000 |
| `sv_swap_timeout` | Timeout in seconds before option to swap expires | 180 | 0 | 10000 |
| `sv_swap` | Enable /swap | 1 | 0 | 1 |
| `sv_use_sql` | Enables MySQL backend instead of SQLite backend (sv\_sqlite\_file is still used as fallback write server when no MySQL server is reachable) | 0 | 0 | 1 |
| `sv_sql_queries_delay` | Delay in seconds between SQL queries of a single player | 1 | 0 | 20 |
| `sv_sqlite_file` | File to store ranks in case sv\_use\_sql is turned off or used as backup sql server | "ddnet-server.sqlite" |  |  |
| `sv_use_upnp` | Enables UPnP support. (Requires -DCONF\_UPNP=ON when compiling) | 0 | 0 | 1 |
| `sv_ddrace_rules` | Whether the default mod rules are displayed or not | 1 | 0 | 1 |
| `sv_rules_line1` | Rules line 1 | "" |  |  |
| `sv_rules_line2` | Rules line 2 | "" |  |  |
| `sv_rules_line3` | Rules line 3 | "" |  |  |
| `sv_rules_line4` | Rules line 4 | "" |  |  |
| `sv_rules_line5` | Rules line 5 | "" |  |  |
| `sv_rules_line6` | Rules line 6 | "" |  |  |
| `sv_rules_line7` | Rules line 7 | "" |  |  |
| `sv_rules_line8` | Rules line 8 | "" |  |  |
| `sv_rules_line9` | Rules line 9 | "" |  |  |
| `sv_rules_line10` | Rules line 10 | "" |  |  |
| `sv_team` | Teams configuration (0 = off, 1 = on but optional, 2 = must play only with teams, 3 = forced random team only for you) | 1 | 0 | 3 |
| `sv_min_team_size` | Minimum team size (finishing in a team smaller than this size gives you no teamrank) | 2 | 1 | 64 |
| `sv_max_team_size` | Maximum team size | 64 | 1 | 64 |
| `sv_map_vote` | Whether to allow /map | 1 | 0 | 1 |
| `sv_announcement_filename` | file which will have the announcement, each one at a line | "announcement.txt" |  |  |
| `sv_announcement_interval` | time(minutes) in which the announcement will be displayed from the announcement file | 300 | 1 | 9999 |
| `sv_announcement_random` | Whether announcements are sequential or random | 1 | 0 | 1 |
| `sv_old_laser` | Whether lasers can hit you if you shot them and that they pull you towards the bounce origin (0 for all new maps) or lasers can't hit you if you shot them, and they pull others towards the shooter | 0 | 0 | 1 |
| `sv_slash_me` | Whether /me is active on the server or not | 0 | 0 | 1 |
| `sv_rejoin_team_0` | Make a team automatically rejoin team 0 after finish (only if not locked) | 1 | 0 | 1 |
| `sv_no_weak_hook` | Whether to use an alternative calculation for world ticks, that makes the hook behave like all players have strong. | 0 | 0 | 1 |
| `conn_timeout` | Network timeout | 100 | 5 | 1000 |
| `conn_timeout_protection` | Network timeout protection | 1000 | 5 | 10000 |
| `sv_reset_pickups` | Whether the weapons are reset on passing the start tile or not | 0 | 0 | 1 |
| `sv_show_others` | Whether players can use the command showothers or not | 1 | 0 | 1 |
| `sv_show_others_default` | Whether players see others by default (2 for own team) | 0 | 0 | 2 |
| `sv_show_all_default` | Whether players see all tees by default | 0 | 0 | 1 |
| `sv_max_afk_time` | The time in seconds a player to be afk (0 = disabled) | 300 | 0 | 9999 |
| `sv_plasma_range` | How far will the plasma gun track tees | 700 | 1 | 99999 |
| `sv_plasma_per_sec` | How many shots does the plasma gun fire per seconds | 3 | 0 | 50 |
| `sv_dragger_range` | How far will the dragger track tees | 700 | 1 | 99999 |
| `sv_vote_pause` | Allow voting to pause players (instead of moving to spectators) | 1 | 0 | 1 |
| `sv_vote_pause_time` | The time (in seconds) players have to wait in pause when paused by vote | 10 | 0 | 360 |
| `sv_tune_reset` | Whether tuning is reset after each map change or not | 1 | 0 | 1 |
| `sv_reset_file` | File to execute on map change or reload to set the default server settings | "reset.cfg" |  |  |
| `sv_input_fifo` | Fifo file (non-Windows) or Named Pipe (Windows) to use as input for server console | "" |  |  |
| `sv_ddrace_tune_reset` | Whether DDRace tuning (sv\_hit, sv\_endless\_drag and sv\_old\_laser) is reset after each map change or not | 1 | 0 | 1 |
| `sv_nameless_score` | Whether nameless tee has a score or not | 1 | 0 | 1 |
| `sv_time_in_broadcast_interval` | How often to update the broadcast time | 1 | 0 | 60 |
| `sv_default_timer_type` | Default way of displaying time either game/round timer or broadcast. 0 = game/round timer, 1 = broadcast, 2 = 0+1, 3 = none | 0 | 0 | 3 |
| `sv_chat_initial_delay` | The time in seconds before the first message can be sent | 0 | 0 | 360 |
| `sv_chat_penalty` | chat score will be increased by this on every message, and decremented by 1 on every tick. | 250 | 50 | 1000 |
| `sv_chat_threshold` | if chat score exceeds this, the player will be muted for sv\_spam\_mute\_duration seconds | 1000 | 50 | 10000 |
| `sv_spam_mute_duration` | how many seconds to mute, if player triggers mute on spam. 0 = off | 60 | 0 | 3600 |
| `sv_shutdown_when_empty` | Shutdown server as soon as no one is on it anymore | 0 | 0 | 1 |
| `sv_reload_when_empty` | Reload map when server is empty (1 = reload once, 2 = reload every time server gets empty) | 0 | 0 | 2 |
| `sv_kill_protection` | 0 - Disable, 1-9999 minutes | 20 | 0 | 9999 |
| `sv_solo_server` | Set server to solo mode (no player interactions, has to be set before loading the map) | 0 | 0 | 1 |
| `sv_client_suggestion` | Broadcast to display to players without DDNet client | "Get DDNet client from DDNet.org to use all features on DDNet!" |  |  |
| `sv_client_suggestion_old` | Broadcast to display to players with an old version of DDNet client | "Your DDNet client is old, update it on DDNet.org!" |  |  |
| `sv_client_suggestion_bot` | Broadcast to display to players with a known botting client | "Your client has bots and can be remotely controlled!\\nPlease use another client like DDNet client from DDNet.org" |  |  |
| `sv_banned_versions` | Comma separated list of banned clients to be kicked on join | "" |  |  |
| `sv_netlimit` | Netlimit: Maximum amount of traffic a client is allowed to use (in kb/s) | 0 | 0 | 10000 |
| `sv_netlimit_alpha` | Netlimit: Alpha of Exponention moving average | 50 | 1 | 100 |
| `sv_connlimit` | Connlimit: Number of connections an IP is allowed to do in a timespan | 5 | 0 | 100 |
| `sv_connlimit_time` | Connlimit: Time in which IP's connections are counted | 20 | 0 | 1000 |
| `sv_conn_logging_server` | Unix socket server for IP address logging (Unix only) | "" |  |  |
| `sv_warmup` | Number of seconds to do warmup before round starts | 0 | 0 | 0 |
| `sv_motd` | Message of the day to display for the clients | "" |  |  |
| `sv_tournament_mode` | Tournament mode. When enabled, players joins the server as spectator | 0 | 0 | 1 |
| `sv_spamprotection` | Spam protection | 1 | 0 | 1 |
| `sv_spectator_slots` | Number of slots to reserve for spectators | 0 | 0 | 64 |
| `sv_inactivekick_time` | How many minutes to wait before taking care of inactive players | 0 | 0 | 1000 |
| `sv_inactivekick` | How to deal with inactive players (0=move to spectator, 1=move to free spectator slot/kick, 2=kick) | 0 | 0 | 2 |
| `sv_strict_spectate_mode` | Restricts information in spectator mode | 0 | 0 | 1 |
| `sv_vote_spectate` | Allow voting to move players to spectators | 1 | 0 | 1 |
| `sv_vote_spectate_rejoindelay` | How many minutes to wait before a player can rejoin after being moved to spectators by vote | 3 | 0 | 1000 |
| `sv_vote_kick` | Allow voting to kick players | 1 | 0 | 1 |
| `sv_vote_kick_min` | Minimum number of players required to start a kick vote | 0 | 0 | 64 |
| `sv_vote_kick_bantime` | The time in seconds to ban a player if kicked by vote. 0 makes it just use kick | 5 | 0 | 1440 |
| `sv_join_vote_delay` | Add a delay before recently joined players can call any vote or participate in a kick/spec vote (in seconds) | 300 | 0 | 1000 |
| `sv_old_teleport_weapons` | Teleporting of all weapons (deprecated, use special entities instead) | 0 | 0 | 1 |
| `sv_old_teleport_hook` | Hook through teleporter (deprecated, use special entities instead) | 0 | 0 | 1 |
| `sv_teleport_hold_hook` | Hold hook when teleported | 0 | 0 | 1 |
| `sv_teleport_lose_weapons` | Lose weapons when teleported (useful for some race maps) | 0 | 0 | 1 |
| `sv_deepfly` | Allow fire non auto weapons when deep | 1 | 0 | 1 |
| `sv_destroy_bullets_on_death` | Destroy bullets when their owner dies | 1 | 0 | 1 |
| `sv_destroy_lasers_on_death` | Destroy lasers when their owner dies | 0 | 0 | 1 |
| `sv_mapupdaterate` | 64 player id <-> vanilla id players map update rate | 5 | 1 | 100 |
| `sv_server_type` | Type of the server (novice, moderate, ...) | "none" |  |  |
| `sv_send_votes_per_tick` | Number of vote options being send per tick | 5 | 1 | 15 |
| `sv_rescue` | Allow /rescue command so players can teleport themselves out of freeze (setting only works in initial config) | 0 | 0 | 1 |
| `sv_rescue_delay` | Number of seconds between two rescues | 1 | 0 | 1000 |
| `sv_practice` | Enable practice mode for teams. Means you can use /rescue, but in turn your rank doesn't count. | 1 | 0 | 1 |
| `dbg_dummies` | (Debug build only) | 0 | 0 | 64 |

## Econ Settings

| Setting | Description | Default | Min | Max |
| --- | --- | --- | --- | --- |
| `ec_bindaddr` | Address to bind the external console to. Anything but 'localhost' is dangerous | "localhost" |  |  |
| `ec_port` | Port to use for the external console | 0 | 0 | 0 |
| `ec_password` | External console password | "" |  |  |
| `ec_bantime` | The time a client gets banned if econ authentication fails. 0 just closes the connection | 0 | 0 | 1440 |
| `ec_auth_timeout` | Time in seconds before the the econ authentication times out | 30 | 1 | 120 |
| `ec_output_level` | Adjusts the amount of information in the external console | 1 | 0 | 2 |

## Server Commands

| Command | Arguments | Description |
| --- | --- | --- |
| `echo` | r\[text\] | Echo the text |
| `exec` | r\[file\] | Execute the specified file |
| `reset` | s\[config-name\] | Reset a config to its default value |
| `toggle` | s\[config-option\] i\[value 1\] i\[value 2\] | Toggle config value |
| `access_level` | s\[command\] ?i\[accesslevel\] | Specify command accessibility (admin = 0, moderator = 1, helper = 2, all = 3) |
| `access_status` | i\[accesslevel\] | List all commands which are accessible for admin = 0, moderator = 1, helper = 2, all = 3 |
| `cmdlist` |  | List all commands which are accessible for users |
| `ban` | s\[ip\|id\] ?i\[minutes\] r\[reason\] | Ban ip for x minutes for any reason |
| `ban_range` | s\[first ip\] s\[last ip\] ?i\[minutes\] r\[reason\] | Ban ip range for x minutes for any reason |
| `unban` | s\[ip\|entry\] | Unban ip/banlist entry |
| `unban_range` | s\[first ip\] s\[last ip\] | Unban ip range |
| `unban_all` |  | Unban all entries |
| `bans` | ?i\[page\] | Show banlist (page 0 by default, 20 entries per page) |
| `bans_save` | s\[file\] | Save banlist in a file |
| `ban` | s\[ip\|id\] ?i\[minutes\] r\[reason\] | Ban player with ip/client id for x minutes for any reason |
| `ban_region` | s\[region\] s\[ip\|id\] ?i\[minutes\] r\[reason\] | Ban player in a region |
| `ban_region_range` | s\[region\] s\[first ip\] s\[last ip\] ?i\[minutes\] r\[reason\] | Ban range in a region |
| `kick` | i\[id\] ?r\[reason\] | Kick player with specified id for any reason |
| `status` | ?r\[name\] | List players containing name or all players |
| `shutdown` | ?r\[reason\] | Shut down |
| `logout` |  | Logout of rcon |
| `show_ips` | ?i\[show\] | Show IP addresses in rcon commands (1 = on, 0 = off) |
| `record` | ?s\[file\] | Record to a file |
| `stoprecord` |  | Stop recording |
| `reload` |  | Reload the map |
| `add_sqlserver` | s\['r'\|'w'\] s\[Database\] s\[Prefix\] s\[User\] s\[Password\] s\[IP\] i\[Port\] ?i\[SetUpDatabase ?\] | add a sqlserver |
| `dump_sqlservers` | s\['r'\|'w'\] | dumps all sqlservers readservers = r, writeservers = w |
| `auth_add` | s\[ident\] s\[level\] r\[pw\] | Add a rcon key |
| `auth_add_p` | s\[ident\] s\[level\] s\[hash\] s\[salt\] | Add a prehashed rcon key |
| `auth_change` | s\[ident\] s\[level\] r\[pw\] | Update a rcon key |
| `auth_change_p` | s\[ident\] s\[level\] s\[hash\] s\[salt\] | Update a rcon key with prehashed data |
| `auth_remove` | s\[ident\] | Remove a rcon key |
| `auth_list` |  | List all rcon keys |
| `name_ban` | s\[name\] ?i\[distance\] ?i\[is\_substring\] ?r\[reason\] | Ban a certain nickname |
| `name_unban` | s\[name\] | Unban a certain nickname |
| `name_bans` |  | List all name bans |
| `tune` | s\[tuning\] ?i\[value\] | Tune variable to value or show current value |
| `toggle_tune` | s\[tuning\] i\[value 1\] i\[value 2\] | Toggle tune variable |
| `tune_reset` | ?s\[tuning\] | Reset all or one tuning variable to default |
| `tunes` |  | List all tuning variables and their values |
| `tune_zone` | i\[zone\] s\[tuning\] i\[value\] | Tune in zone a variable to value |
| `tune_zone_dump` | i\[zone\] | Dump zone tuning in zone x |
| `tune_zone_reset` | ?i\[zone\] | reset zone tuning in zone x or in all zones |
| `tune_zone_enter` | i\[zone\] r\[message\] | which message to display on zone enter; use 0 for normal area |
| `tune_zone_leave` | i\[zone\] r\[message\] | which message to display on zone leave; use 0 for normal area |
| `mapbug` | s\[mapbug\] | Enable map compatibility mode using the specified bug (example: grenade-doubleexplosion@ddnet.tw) |
| `switch_open` | i\[switch\] | Whether a switch is deactivated by default (otherwise activated) |
| `pause_game` |  | Pause/unpause game |
| `change_map` | ?r\[map\] | Change map |
| `random_map` | ?i\[stars\] | Random map |
| `random_unfinished_map` | ?i\[stars\] | Random unfinished map |
| `restart` | ?i\[seconds\] | Restart in x seconds (0 = abort) |
| `broadcast` | r\[message\] | Broadcast message |
| `say` | r\[message\] | Say in chat |
| `set_team` | i\[id\] i\[team-id\] ?i\[delay in minutes\] | Set team of player to team |
| `set_team_all` | i\[team-id\] | Set team of all players to team |
| `add_vote` | s\[name\] r\[command\] | Add a voting option |
| `remove_vote` | r\[name\] | remove a voting option |
| `force_vote` | s\[name\] s\[command\] ?r\[reason\] | Force a voting option |
| `clear_votes` |  | Clears the voting options |
| `add_map_votes` |  | Automatically adds voting options for all maps |
| `vote` | r\['yes'\|'no'\] | Force a vote to yes/no |
| `dump_antibot` |  | Dumps the antibot status |
| `dbg_lognetwork` |  | Log the network |
| `kill_pl` | v\[id\] | Kills player v and announces the kill |
| `totele` | i\[number\] | Teleports you to teleporter v |
| `totelecp` | i\[number\] | Teleports you to checkpoint teleporter v |
| `tele` | ?i\[id\] ?i\[id\] | Teleports player i (or you) to player i (or you to where you look at) |
| `addweapon` | i\[weapon-id\] | Gives weapon with id i to you (all = -1, hammer = 0, gun = 1, shotgun = 2, grenade = 3, laser = 4, ninja = 5) |
| `removeweapon` | i\[weapon-id\] | removes weapon with id i from you (all = -1, hammer = 0, gun = 1, shotgun = 2, grenade = 3, laser = 4, ninja = 5) |
| `shotgun` |  | Gives a shotgun to you |
| `grenade` |  | Gives a grenade launcher to you |
| `laser` |  | Gives a laser to you |
| `rifle` |  | Gives a laser to you |
| `jetpack` |  | Gives jetpack to you |
| `weapons` |  | Gives all weapons to you |
| `unshotgun` |  | Removes the shotgun from you |
| `ungrenade` |  | Removes the grenade launcher from you |
| `unlaser` |  | Removes the laser from you |
| `unrifle` |  | Removes the laser from you |
| `unjetpack` |  | Removes the jetpack from you |
| `unweapons` |  | Removes all weapons from you |
| `ninja` |  | Makes you a ninja |
| `unninja` |  | Removes ninja from you |
| `super` |  | Makes you super |
| `unsuper` |  | Removes super from you |
| `endless_hook` |  | Gives you endless hook |
| `unendless_hook` |  | Removes endless hook from you |
| `solo` |  | Puts you into solo part |
| `unsolo` |  | Puts you out of solo part |
| `deep` |  | Puts you into deep freeze |
| `undeep` |  | Puts you out of deep freeze |
| `livefreeze` |  | Makes you live frozen |
| `unlivefreeze` |  | Puts you out of live freeze |
| `left` | ?i\[tiles\] | Makes you move 1 tile left |
| `right` | ?i\[tiles\] | Makes you move 1 tile right |
| `up` | ?i\[tiles\] | Makes you move 1 tile up |
| `down` | ?i\[tiles\] | Makes you move 1 tile down |
| `move` | i\[x\] i\[y\] | Moves to the tile with x/y-number ii |
| `move_raw` | i\[x\] i\[y\] | Moves to the point with x/y-coordinates ii |
| `force_pause` | v\[id\] i\[seconds\] | Force i to pause for i seconds |
| `force_unpause` | v\[id\] | Set force-pause timer of i to 0. |
| `set_team_ddr` | v\[id\] i\[team\] | Set ddrace team of a player |
| `uninvite` | v\[id\] i\[team\] | Uninvite player from team |
| `vote_mute` | v\[id\] i\[seconds\] ?r\[reason\] | Remove v's right to vote for i seconds |
| `vote_unmute` | v\[id\] | Give back v's right to vote. |
| `vote_mutes` |  | List the current active vote mutes. |
| `mute` |  |  |
| `muteid` | v\[id\] i\[seconds\] ?r\[reason\] | Mute player with id |
| `muteip` | s\[ip\] i\[seconds\] ?r\[reason\] | Mute player with IP address |
| `unmute` | i\[muteid\] |  |
| `unmuteid` | v\[id\] | Unmute player with id |
| `mutes` |  |  |
| `moderate` |  | Enables/disables active moderator mode for the player |
| `vote_no` |  |  |
| `save_dry` |  | Dump the current savestring |
| `dump_log` | ?i\[seconds\] | Show logs of the last i seconds |
| `freezehammer` | v\[id\] | Gives a player Freeze Hammer |
| `unfreezehammer` | v\[id\] | Removes Freeze Hammer from a player |

## Chat Commands

| Command | Arguments | Description |
| --- | --- | --- |
| `credits` |  | Shows the credits of the DDNet mod |
| `rules` |  | Shows the server rules |
| `emote` | ?s\[emote name\] i\[duration in seconds\] | Sets your tee's eye emote |
| `eyeemote` | ?s\['on'\|'off'\|'toggle'\] | Toggles use of standard eye-emotes on/off, eyeemote s, where s = on for on, off for off, toggle for toggle and nothing to show current status |
| `settings` | ?s\[configname\] | Shows gameplay information for this server |
| `help` | ?r\[command\] | Shows help to command r, general help if left blank |
| `info` |  | Shows info about this server |
| `list` | ?s\[filter\] | List connected players with optional case-insensitive substring matching filter |
| `me` | r\[message\] | Like the famous irc command '/me says hi' will display '<yourname> says hi' |
| `w` | s\[player name\] r\[message\] | Whisper something to someone (private message) |
| `whisper` | s\[player name\] r\[message\] | Whisper something to someone (private message) |
| `c` | r\[message\] | Converse with the last person you whispered to (private message) |
| `converse` | r\[message\] | Converse with the last person you whispered to (private message) |
| `pause` | ?r\[player name\] | Toggles pause |
| `spec` | ?r\[player name\] | Toggles spec (if not available behaves as /pause) |
| `pausevoted` |  | Toggles pause on the currently voted player |
| `specvoted` |  | Toggles spec on the currently voted player |
| `dnd` |  | Toggle Do Not Disturb (no chat and server messages) |
| `mapinfo` | ?r\[map\] | Show info about the map with name r gives (current map by default) |
| `timeout` | ?s\[code\] | Set timeout protection code s |
| `practice` | ?i\['0'\|'1'\] | Enable cheats (currently only /rescue) for your current team's run, but you can't earn a rank |
| `swap` | ?r\[player name\] | Request to swap your tee with another team member |
| `save` | ?r\[code\] | Save team with code r. |
| `load` | ?r\[code\] | Load with code r. /load to check your existing saves |
| `map` | ?r\[map\] | Vote a map by name |
| `rankteam` | ?r\[player name\] | Shows the team rank of player with name r (your team rank by default) |
| `teamrank` | ?r\[player name\] | Shows the team rank of player with name r (your team rank by default) |
| `rank` | ?r\[player name\] | Shows the rank of player with name r (your rank by default) |
| `top5team` | ?s\[player name\] ?i\[rank to start with\] | Shows five team ranks of the ladder or of a player beginning with rank i (1 by default, -1 for worst) |
| `teamtop5` | ?s\[player name\] ?i\[rank to start with\] | Shows five team ranks of the ladder or of a player beginning with rank i (1 by default, -1 for worst) |
| `top` | ?i\[rank to start with\] | Shows the top ranks of the global and regional ladder beginning with rank i (1 by default, -1 for worst) |
| `top5` | ?i\[rank to start with\] | Shows the top ranks of the global and regional ladder beginning with rank i (1 by default, -1 for worst) |
| `times` | ?s\[player name\] ?i\[number of times to skip\] | /times ?s?i shows last 5 times of the server or of a player beginning with name s starting with time i (i = 1 by default, -1 for first) |
| `points` | ?r\[player name\] | Shows the global points of a player beginning with name r (your rank by default) |
| `top5points` | ?i\[number\] | Shows five points of the global point ladder beginning with rank i (1 by default) |
| `timecp` | ?r\[player name\] | Set your checkpoints based on another player |
| `team` | ?i\[id\] | Lets you join team i (shows your team if left blank) |
| `lock` | ?i\['0'\|'1'\] | Toggle team lock so no one else can join and so the team restarts when a player dies. /lock 0 to unlock, /lock 1 to lock. |
| `unlock` |  | Unlock a team |
| `invite` | r\[player name\] | Invite a person to a locked team |
| `showothers` | ?i\['0'\|'1'\|'2'\] | Whether to show players from other teams or not (off by default), optional i = 0 for off, i = 1 for on, i = 2 for own team only |
| `showall` | ?i\['0'\|'1'\] | Whether to show players at any distance (off by default), optional i = 0 for off else for on |
| `specteam` | ?i\['0'\|'1'\] | Whether to show players from other teams when spectating (on by default), optional i = 0 for off else for on |
| `ninjajetpack` | ?i\['0'\|'1'\] | Whether to use ninja jetpack or not. Makes jetpack look more awesome |
| `saytime` | ?r\[player name\] | Privately messages someone's current time in this current running race (your time by default) |
| `saytimeall` |  | Publicly messages everyone your current time in this current running race |
| `time` |  | Privately shows you your current time in this current running race in the broadcast message |
| `timer` | ?s\['gametimer'\|'broadcast'\|'both'\|'none'\|'cycle'\] | Personal Setting of showing time in either broadcast or game/round timer, timer s, where s = broadcast for broadcast, gametimer for game/round timer, cycle for cycle, both for both, none for no timer and nothing to show current status |
| `r` |  | Teleport yourself out of freeze (use sv\_rescue 1 to enable this feature) |
| `rescue` |  | Teleport yourself out of freeze (use sv\_rescue 1 to enable this feature) |
| `tp` | ?r\[player name\] | Teleport yourself to player or to where you are spectating if no player name is given |
| `lasttp` |  | Teleport yourself to the last location you teleported to. |
| `teleport` | ?r\[player name\] | Teleport yourself to player or to where you are spectating if no player name is given |
| `unsolo` |  | Puts you out of solo part |
| `undeep` |  | Puts you out of deep freeze |
| `kill` |  | Kill yourself when kill-protected during a long game (use f1, kill for regular kill) |

## Client Settings

| Setting | Description | Default | Min | Max |
| --- | --- | --- | --- | --- |
| `cl_predict` | Predict client movements | 1 | 0 | 1 |
| `cl_predict_dummy` | Predict dummy movements | 1 | 0 | 1 |
| `cl_antiping_limit` | Antiping limit (0 to disable) | 0 | 0 | 200 |
| `cl_antiping` | Enable antiping, i. e. more aggressive prediction. | 0 | 0 | 1 |
| `cl_antiping_players` | Predict other player's movement more aggressively (only enabled if cl\_antiping is set to 1) | 1 | 0 | 1 |
| `cl_antiping_grenade` | Predict grenades (only enabled if cl\_antiping is set to 1) | 1 | 0 | 1 |
| `cl_antiping_weapons` | Predict weapon projectiles (only enabled if cl\_antiping is set to 1) | 1 | 0 | 1 |
| `cl_antiping_smooth` | Make the prediction of other player's movement smoother | 0 | 0 | 1 |
| `cl_antiping_gunfire` | Predict gunfire and show predicted weapon physics (with cl\_antiping\_grenade 1 and cl\_antiping\_weapons 1) | 1 | 0 | 1 |
| `cl_prediction_margin` | Prediction margin in ms (adds latency, can reduce lag from ping jumps) | 10 | 1 | 2000 |
| `cl_nameplates` | Show name plates | 1 | 0 | 1 |
| `cl_afk_emote` | Show zzz emote next to afk players | 1 | 0 | 1 |
| `cl_nameplates_always` | Always show name plates disregarding of distance | 1 | 0 | 1 |
| `cl_nameplates_teamcolors` | Use team colors for name plates | 1 | 0 | 1 |
| `cl_nameplates_size` | Size of the name plates from 0 to 100% | 50 | 0 | 100 |
| `cl_nameplates_clan` | Show clan in name plates | 0 | 0 | 1 |
| `cl_nameplates_clan_size` | Size of the clan plates from 0 to 100% | 30 | 0 | 100 |
| `cl_nameplates_ids` | Show IDs in name plates | 0 | 0 | 1 |
| `cl_nameplates_own` | Show own name plate (useful for demo recording) | 0 | 0 | 1 |
| `cl_nameplates_friendmark` | Show friend mark (♥) in name plates | 0 | 0 | 1 |
| `cl_nameplates_strong` | Show strong/weak in name plates (0 - off, 1 - icons, 2 - icons + numbers) | 0 | 0 | 2 |
| `cl_text_entities` | Render textual entity data | 1 | 0 | 1 |
| `cl_text_entities_size` | Size of textual entity data from 1 to 100% | 100 | 1 | 100 |
| `cl_authed_player_color` | Color of name of authenticated player in scoreboard | 5898211 |  |  |
| `cl_same_clan_color` | Clan color of players with the same clan as you in scoreboard. | 5898211 |  |  |
| `cl_enable_ping_color` | Whether ping is colored in scoreboard. | 1 | 0 | 1 |
| `cl_autoswitch_weapons` | Auto switch weapon on pickup | 1 | 0 | 1 |
| `cl_autoswitch_weapons_out_of_ammo` | Auto switch weapon when out of ammo | 0 | 0 | 1 |
| `cl_showhud` | Show ingame HUD | 1 | 0 | 1 |
| `cl_showhud_healthammo` | Show ingame HUD (Health + Ammo) | 1 | 0 | 1 |
| `cl_showhud_score` | Show ingame HUD (Score) | 1 | 0 | 1 |
| `cl_showhud_timer` | Show ingame HUD (Timer) | 1 | 0 | 1 |
| `cl_showhud_dummy_actions` | Show ingame HUD (Dummy Actions) | 1 | 0 | 1 |
| `cl_showhud_player_position` | Show ingame HUD (Player Position) | 0 | 0 | 1 |
| `cl_showhud_player_speed` | Show ingame HUD (Player Speed) | 0 | 0 | 1 |
| `cl_showhud_player_angle` | Show ingame HUD (Player Aim Angle) | 0 | 0 | 1 |
| `cl_showhud_ddrace` | Show ingame HUD (DDRace HUD) | 1 | 0 | 1 |
| `cl_showhud_jumps_indicator` | Show ingame HUD (Jumps you have and have used) | 1 | 0 | 1 |
| `cl_show_freeze_bars` | Whether to show a freeze bar under frozen players to indicate the thaw time | 1 | 0 | 1 |
| `cl_freezebars_alpha_inside_freeze` | Opacity of freeze bars inside freeze (0 invisible, 100 fully visible) | 0 | 0 | 100 |
| `cl_showrecord` | Show old style DDRace client records | 0 | 0 | 1 |
| `cl_shownotifications` | Make the client notify when someone highlights you | 1 | 0 | 1 |
| `cl_showemotes` | Show tee emotes | 1 | 0 | 1 |
| `cl_showchat` | Show chat (2 to always show large chat area) | 1 | 0 | 2 |
| `cl_show_chat_friends` | Show only chat messages from friends | 0 | 0 | 1 |
| `cl_show_chat_system` | Show chat messages from the server | 1 | 0 | 1 |
| `cl_showkillmessages` | Show kill messages | 1 | 0 | 1 |
| `cl_show_votes_after_voting` | Show votes window after voting | 0 | 0 | 1 |
| `cl_show_local_time_always` | Always show local time | 0 | 0 | 1 |
| `cl_showfps` | Show ingame FPS counter | 0 | 0 | 1 |
| `cl_showpred` | Show ingame prediction time in milliseconds | 0 | 0 | 1 |
| `cl_eye_wheel` | Show eye wheel along together with emotes | 1 | 0 | 1 |
| `cl_eye_duration` | How long the eyes emotes last | 999999 | 1 | 999999 |
| `cl_airjumpindicator` | Show the air jump indicator | 1 | 0 | 1 |
| `cl_threadsoundloading` | Load sound files threaded | 1 | 0 | 1 |
| `cl_warning_teambalance` | Warn about team balance | 1 | 0 | 1 |
| `cl_mouse_deadzone` | Deadzone for the camera to follow the cursor | 0 | 0 | 3000 |
| `cl_mouse_followfactor` | Factor for the camera to follow the cursor | 0 | 0 | 200 |
| `cl_mouse_max_distance` | Maximum cursor distance | 400 | 0 | 5000 |
| `cl_mouse_min_distance` | Minimum cursor distance | 0 | 0 | 5000 |
| `cl_dyncam` | Enable dyncam | 0 | 0 | 1 |
| `cl_dyncam_max_distance` | Maximum dynamic camera cursor distance | 1000 | 0 | 2000 |
| `cl_dyncam_min_distance` | Minimum dynamic camera cursor distance | 0 | 0 | 2000 |
| `cl_dyncam_mousesens` | Mouse sens used when dyncam is toggled on | 0 | 0 | 100000 |
| `cl_dyncam_deadzone` | Deadzone for the dynamic camera to follow the cursor | 300 | 1 | 1300 |
| `cl_dyncam_follow_factor` | Factor for the dynamic camera to follow the cursor | 60 | 0 | 200 |
| `cl_dyncam_smoothness` | Transition amount of the camera movement, 0=instant, 100=slow and smooth | 0 | 0 | 100 |
| `cl_dyncam_stabilizing` | Amount of camera slowdown during fast cursor movement. High value can cause delay in camera movement | 0 | 0 | 100 |
| `cl_multiview_sensitivity` | Set how fast the camera will move to the desired location (higher = faster) | 100 | 0 | 200 |
| `cl_multiview_zoom_smoothness` | Set the smoothness of the multi-view zoom (in ms, higher = slower) | 1300 | 50 | 5000 |
| `ed_autosave_interval` | Interval in minutes at which a copy of the current editor map is automatically saved to the 'auto' folder (0 for off) | 10 | 0 | 240 |
| `ed_autosave_max` | Maximum number of autosaves that are kept per map name (0 = no limit) | 10 | 0 | 1000 |
| `ed_smooth_zoom_time` | Time of smooth zoom animation in the editor in ms (0 for off) | 250 | 0 | 5000 |
| `ed_limit_max_zoom_level` | Specifies, if zooming in the editor should be limited or not (0 = no limit) | 1 | 0 | 1 |
| `ed_zoom_target` | Zoom to the current mouse target | 0 | 0 | 1 |
| `ed_showkeys` | Show pressed keys | 0 | 0 | 1 |
| `cl_show_welcome` | Show welcome message indicating the first launch of the client | 1 | 0 | 1 |
| `cl_motd_time` | How long to show the server message of the day | 10 | 0 | 100 |
| `cl_map_download_url` | URL used to download maps (can start with http:// or https://) | "[https://maps.ddnet.org](https://maps.ddnet.org)" |  |  |
| `cl_map_download_connect_timeout_ms` | HTTP map downloads: timeout for the connect phase in milliseconds (0 to disable) | 2000 | 0 | 100000 |
| `cl_map_download_low_speed_limit` | HTTP map downloads: Set low speed limit in bytes per second (0 to disable) | 4000 | 0 | 100000 |
| `cl_map_download_low_speed_time` | HTTP map downloads: Set low speed limit time period (0 to disable) | 3 | 0 | 100000 |
| `cl_languagefile` | What language file to use | "" |  |  |
| `cl_skin_download_url` | URL used to download skins | "[https://skins.ddnet.org/skin/](https://skins.ddnet.org/skin/)" |  |  |
| `cl_skin_community_download_url` | URL used to download community skins | "[https://skins.ddnet.org/skin/community/](https://skins.ddnet.org/skin/community/)" |  |  |
| `cl_vanilla_skins_only` | Only show skins available in Vanilla Teeworlds | 0 | 0 | 1 |
| `cl_download_skins` | Download skins from cl\_skin\_download\_url on-the-fly | 1 | 0 | 1 |
| `cl_download_community_skins` | Allow to download skins created by the community. Uses cl\_skin\_community\_download\_url instead of cl\_skin\_download\_url for the download | 0 | 0 | 1 |
| `cl_auto_statboard_screenshot` | Automatically take game over statboard screenshot | 0 | 0 | 1 |
| `cl_auto_statboard_screenshot_max` | Maximum number of automatically created statboard screenshots (0 = no limit) | 10 | 0 | 1000 |
| `cl_default_zoom` | Default zoom level | 10 | 0 | 20 |
| `cl_smooth_zoom_time` | Time of smooth zoom animation ingame in ms (0 for off) | 250 | 0 | 5000 |
| `cl_limit_max_zoom_level` | Specifies, if zooming ingame should be limited or not (0 = no limit) | 1 | 0 | 1 |
| `player_use_custom_color` | Toggles usage of custom colors | 0 | 0 | 1 |
| `player_color_body` | Player body color | 65408 |  |  |
| `player_color_feet` | Player feet color | 65408 |  |  |
| `player_skin` | Player skin | "default" |  |  |
| `player_default_eyes` | Player eyes when joining server. 0 = normal, 1 = pain, 2 = happy, 3 = surprise, 4 = angry, 5 = blink | 0 | 0 | 5 |
| `cl_skin_prefix` | Replace the skins by skins with this prefix (e.g. kitty, santa) | "" |  |  |
| `cl_fat_skins` | Enable fat skins | 0 | 0 | 1 |
| `ui_page` | Interface page | 9 | 6 | 10 |
| `ui_settings_page` | Interface settings page | 0 | 0 | 9 |
| `ui_toolbox_page` | Toolbox page | 0 | 0 | 2 |
| `ui_server_address` | Interface server address | "localhost:8303" |  |  |
| `ui_mousesens` | Mouse sensitivity for menus/editor | 200 | 1 | 100000 |
| `ui_controller_sens` | Controller sensitivity for menus/editor | 100 | 1 | 100000 |
| `ui_smooth_scroll_time` | Time of smooth scrolling animation in menus/editor in ms (0 for off) | 500 | 0 | 5000 |
| `ui_color` | Interface color | 0xE4A046AF |  |  |
| `ui_colorize_ping` | Highlight ping | 1 | 0 | 1 |
| `ui_colorize_gametype` | Highlight gametype | 1 | 0 | 1 |
| `ui_close_window_after_changing_setting` | Close window after changing setting | 1 | 0 | 1 |
| `ui_unread_news` | Whether there is unread news | 0 | 0 | 1 |
| `gfx_noclip` | Disable clipping | 0 | 0 | 1 |
| `dummy_name` | Name of the dummy | "" |  |  |
| `dummy_clan` | Clan of the dummy | "" |  |  |
| `dummy_country` | Country of the Dummy | -1 | -1 | 1000 |
| `dummy_use_custom_color` | Toggles usage of custom colors | 0 | 0 | 1 |
| `dummy_color_body` | Dummy body color | 65408 |  |  |
| `dummy_color_feet` | Dummy feet color | 65408 |  |  |
| `dummy_skin` | Dummy skin | "default" |  |  |
| `dummy_default_eyes` | Dummy eyes when joining server. 0 = normal, 1 = pain, 2 = happy, 3 = surprise, 4 = angry, 5 = blink | 0 | 0 | 5 |
| `cl_dummy` | 0 - player / 1 - dummy | 0 | 0 | 1 |
| `cl_dummy_hammer` | Whether dummy is hammering for a hammerfly | 0 | 0 | 1 |
| `cl_dummy_resetonswitch` | Whether dummy or player should stop pressing keys when you switch. 0 = off, 1 = dummy, 2 = player | 0 | 0 | 2 |
| `cl_dummy_restore_weapon` | Whether dummy should switch to last weapon after hammerfly | 1 | 0 | 1 |
| `cl_dummy_copy_moves` | Whether dummy should copy your moves | 0 | 0 | 1 |
| `cl_dummy_control` | Whether can you control dummy at the same time (cl\_dummy\_jump, cl\_dummy\_fire, cl\_dummy\_hook) | 0 | 0 | 1 |
| `cl_dummy_jump` | Whether dummy is jumping (requires cl\_dummy\_control 1) | 0 | 0 | 1 |
| `cl_dummy_fire` | Whether dummy is firing (requires cl\_dummy\_control 1) | 0 | 0 | 1 |
| `cl_dummy_hook` | Whether dummy is hooking (requires cl\_dummy\_control 1) | 0 | 0 | 1 |
| `cl_show_start_menu_images` | Show start menu images | 1 | 0 | 1 |
| `cl_skip_start_menu` | Skip the start menu | 0 | 0 | 1 |
| `cl_video_pausewithdemo` | Pause video rendering when demo playing pause | 1 | 0 | 1 |
| `cl_video_showhud` | Show ingame HUD when rendering video | 0 | 0 | 1 |
| `cl_video_showchat` | Show chat when rendering video | 1 | 0 | 1 |
| `cl_video_sound_enable` | Use sound when rendering video | 1 | 0 | 1 |
| `cl_video_show_hook_coll_other` | Show other players' hook collision lines when rendering video | 0 | 0 | 1 |
| `cl_video_show_direction` | Show players' key presses when rendering video (1 = other players', 2 = also your own) | 0 | 0 | 2 |
| `cl_video_crf` | Set crf when encode video with libx264 (0 for highest quality, 51 for lowest) | 18 | 0 | 51 |
| `cl_video_preset` | Set preset when encode video with libx264, default is 5 (medium), 0 is ultrafast, 9 is placebo (the slowest, not recommend) | 5 | 0 | 9 |
| `dbg_tuning` | Display information about the tuning parameters that affect the own player (0 = off, 1 = show changed, 2 = show all) | 0 | 0 | 2 |
| `player_name` | Name of the player | "" |  |  |
| `player_clan` | Clan of the player | "" |  |  |
| `player_country` | Country of the player | -1 | -1 | 1000 |
| `password` | Password to the server | "" |  |  |
| `events` | Enable triggering of events, (eye emotes on some holidays in server, christmas skins in client). | 1 | 0 | 1 |
| `steam_name` | Last seen name of the Steam profile | "" |  |  |
| `logfile` | Filename to log all output to | "" |  |  |
| `logappend` | Append to logfile instead of overwriting it every time | 1 | 0 | 1 |
| `loglevel` | Adjusts the amount of information in the logfile (-3 = none, -2 = error only, -1 = warn, 0 = info, 1 = debug, 2 = trace) | 0 | -3 | 2 |
| `stdout_output_level` | Adjusts the amount of information in the system console (-3 = none, -2 = error only, -1 = warn, 0 = info, 1 = debug, 2 = trace) | 0 | -3 | 2 |
| `console_output_level` | Adjusts the amount of information in the local/remote console (-3 = none, -2 = error only, -1 = warn, 0 = info, 1 = debug, 2 = trace) | 0 | -3 | 2 |
| `console_enable_colors` | Enable colors in console output | 1 | 0 | 1 |
| `cl_save_settings` | Write the settings file on exit | 1 | 0 | 1 |
| `cl_refresh_rate` | Refresh rate for updating the game (in Hz) | 0 | 0 | 10000 |
| `cl_refresh_rate_inactive` | Refresh rate for updating the game when the window is inactive (in Hz) | 120 | 0 | 10000 |
| `cl_editor` | Open the map editor | 0 | 0 | 1 |
| `cl_editor_dilate` | Automatically dilates embedded images | 1 | 0 | 1 |
| `cl_skin_filter_string` | Skin filtering string | "" |  |  |
| `cl_auto_demo_record` | Automatically record demos | 1 | 0 | 1 |
| `cl_auto_demo_on_connect` | Only start a new demo when connect while automatically record demos | 0 | 0 | 1 |
| `cl_auto_demo_max` | Maximum number of automatically recorded demos (0 = no limit) | 10 | 0 | 1000 |
| `cl_auto_screenshot` | Automatically take game over screenshot | 0 | 0 | 1 |
| `cl_auto_screenshot_max` | Maximum number of automatically created screenshots (0 = no limit) | 10 | 0 | 1000 |
| `cl_auto_csv` | Automatically create game over csv | 0 | 0 | 1 |
| `cl_auto_csv_max` | Maximum number of automatically created csvs (0 = no limit) | 10 | 0 | 1000 |
| `cl_show_broadcasts` | Show broadcasts ingame | 1 | 0 | 1 |
| `cl_print_broadcasts` | Print broadcasts to console | 1 | 0 | 1 |
| `cl_print_motd` | Print motd to console | 1 | 0 | 1 |
| `cl_friends_ignore_clan` | Ignore clan tag when searching for friends | 1 | 0 | 1 |
| `cl_assets_entities` | The asset/assets for entities | "default" |  |  |
| `cl_asset_game` | The asset for game | "default" |  |  |
| `cl_asset_emoticons` | The asset for emoticons | "default" |  |  |
| `cl_asset_particles` | The asset for particles | "default" |  |  |
| `cl_asset_hud` | The asset for HUD | "default" |  |  |
| `cl_asset_extras` | The asset for the game graphics that do not come from Teeworlds | "default" |  |  |
| `br_filter_string` | Server browser filtering string | "Novice" |  |  |
| `br_exclude_string` | Server browser exclusion string | "" |  |  |
| `br_filter_full` | Filter out full server in browser | 0 | 0 | 1 |
| `br_filter_empty` | Filter out empty server in browser | 0 | 0 | 1 |
| `br_filter_spectators` | Filter out spectators from player numbers | 0 | 0 | 1 |
| `br_filter_friends` | Filter out servers with no friends | 0 | 0 | 1 |
| `br_filter_country` | Filter out servers with non-matching player country | 0 | 0 | 1 |
| `br_filter_country_index` | Player country to filter by in the server browser | -1 | -1 | 999 |
| `br_filter_pw` | Filter out password protected servers in browser | 0 | 0 | 1 |
| `br_filter_gametype` | Game types to filter | "" |  |  |
| `br_filter_gametype_strict` | Strict gametype filter | 0 | 0 | 1 |
| `br_filter_connecting_players` | Filter connecting players | 1 | 0 | 1 |
| `br_filter_serveraddress` | Server address to filter | "" |  |  |
| `br_filter_unfinished_map` | Show only servers with unfinished maps | 0 | 0 | 1 |
| `br_filter_exclude_countries` | Filter out DDNet servers by country | "" |  |  |
| `br_filter_exclude_types` | Filter out DDNet servers by type (mod) | "" |  |  |
| `br_indicate_finished` | Show whether you have finished a DDNet map (transmits your player name to info.ddnet.org/info) | 1 | 0 | 1 |
| `br_location` | Override location for ping estimation, available: auto, af, as, as:cn, eu, na, oc, sa (Automatic, Africa, Asia, China, Europe, North America, Oceania/Australia, South America | "auto" |  |  |
| `br_cached_best_serverinfo_url` | Do not set this variable, instead create a ddnet-serverlist-urls.cfg next to settings\_ddnet.cfg to specify all possible serverlist URLs | "" |  |  |
| `br_filter_exclude_countries_kog` | Filter out kog servers by country | "" |  |  |
| `br_filter_exclude_types_kog` | Filter out kog servers by type (mod) | "" |  |  |
| `br_sort` | Sorting column in server browser | 4 | 0 | 256 |
| `br_sort_order` | Sorting order in server browser | 2 | 0 | 2 |
| `br_max_requests` | Number of concurrent requests to use when refreshing server browser | 100 | 0 | 1000 |
| `br_demo_sort` | Sorting column in demo browser | 0 | 0 | 3 |
| `br_demo_sort_order` | Sorting order in demo browser | 0 | 0 | 1 |
| `br_demo_fetch_info` | Whether to auto fetch demo infos on refresh | 0 | 0 | 1 |
| `snd_buffer_size` | Sound buffer size | 512 | 128 | 32768 |
| `snd_rate` | Sound mixing rate | 48000 | 5512 | 384000 |
| `snd_enable` | Sound enable | 1 | 0 | 1 |
| `snd_enable_music` | Play background music | 0 | 0 | 1 |
| `snd_volume` | Sound volume | 30 | 0 | 100 |
| `snd_chat_volume` | Chat sound volume | 30 | 0 | 100 |
| `snd_game_volume` | Game sound volume | 30 | 0 | 100 |
| `snd_ambient_volume` | Map Sound sound volume | 30 | 0 | 100 |
| `snd_background_music_volume` | Background music sound volume | 30 | 0 | 100 |
| `snd_nonactive_mute` | Mute sounds when window is not active | 0 | 0 | 1 |
| `snd_game` | Enable game sounds | 1 | 0 | 1 |
| `snd_gun` | Enable gun sound | 1 | 0 | 1 |
| `snd_long_pain` | Enable long pain sound (used when shooting in freeze) | 1 | 0 | 1 |
| `snd_chat` | Enable regular chat sound | 1 | 0 | 1 |
| `snd_team_chat` | Enable team chat sound | 1 | 0 | 1 |
| `snd_servermessage` | Enable server message sound | 1 | 0 | 1 |
| `snd_highlight` | Enable highlighted chat sound | 1 | 0 | 1 |
| `gfx_screen` | Screen index | 0 | 0 | 15 |
| `gfx_screen_width` | Screen resolution width | 0 | 0 | 0 |
| `gfx_screen_height` | Screen resolution height | 0 | 0 | 0 |
| `gfx_screen_refresh_rate` | Screen refresh rate | 0 | 0 | 0 |
| `gfx_desktop_width` | Desktop resolution width for detecting display changes (not recommended to change manually) | 0 | 0 | 0 |
| `gfx_desktop_height` | Desktop resolution height for detecting display changes (not recommended to change manually) | 0 | 0 | 0 |
| `gfx_borderless` | Borderless window (not to be used with fullscreen) | 1 | 0 | 1 |
| `gfx_fullscreen` | Set fullscreen mode: 0=no fullscreen, 1=pure fullscreen, 2=desktop fullscreen, 3=windowed fullscreen | 0 | 0 | 3 |
| `gfx_highdpi` | Enable high-dpi | 1 | 0 | 1 |
| `gfx_color_depth` | Colors bits for framebuffer (fullscreen only) | 24 | 16 | 24 |
| `gfx_vsync` | Vertical sync (may cause delay) | 0 | 0 | 1 |
| `gfx_display_all_video_modes` | Show all video modes | 0 | 0 | 1 |
| `gfx_high_detail` | High detail | 1 | 0 | 1 |
| `gfx_fsaa_samples` | FSAA samples (may cause delay) | 0 | 0 | 64 |
| `gfx_refresh_rate` | Screen refresh rate | 0 | 0 | 10000 |
| `gfx_backgroundrender` | Render graphics when window is in background | 1 | 0 | 1 |
| `gfx_text_overlay` | Stop rendering textoverlay in editor or with entities: high value = less details = more speed | 10 | 1 | 100 |
| `gfx_asyncrender_old` | During an update cycle, skip the render cycle, if the render cycle would need to wait for the previous render cycle to finish | 1 | 0 | 1 |
| `gfx_quad_as_triangle` | Render quads as triangles (fixes quad coloring on some GPUs) | 0 | 0 | 1 |
| `inp_mousesens` | Mouse sensitivity | 200 | 1 | 100000 |
| `inp_translated_keys` | Translate keys before interpreting them, respects keyboard layouts | 0 | 0 | 1 |
| `inp_ignored_modifiers` | Ignored keyboard modifier mask | 0 | 0 | 65536 |
| `inp_ime_native_ui` | Use native UI for IME (may cause IME to not work in fullscreen mode) (changing requires restart) | 0 | 0 | 1 |
| `inp_controller_enable` | Enable controller | 0 | 0 | 1 |
| `inp_controller_guid` | Controller GUID which uniquely identifies the active controller | "" |  |  |
| `inp_controller_absolute` | Enable absolute controller aiming ingame | 0 | 0 | 1 |
| `inp_controller_sens` | Ingame controller sensitivity | 100 | 1 | 100000 |
| `inp_controller_x` | Controller axis that controls X axis of cursor | 0 | 0 | 12 |
| `inp_controller_y` | Controller axis that controls Y axis of cursor | 1 | 0 | 12 |
| `inp_controller_tolerance` | Controller axis tolerance to account for jitter | 5 | 0 | 50 |
| `cl_port` | Port to use for client connections to server (0 to choose a random port, 1024 or higher to set a manual port, requires a restart) | 0 | 0 | 65535 |
| `cl_dummy_port` | Port to use for dummy connections to server (0 to choose a random port, 1024 or higher to set a manual port, requires a restart) | 0 | 0 | 65535 |
| `cl_contact_port` | Port to use for serverinfo connections to server (0 to choose a random port, 1024 or higher to set a manual port, requires a restart) | 0 | 0 | 65535 |
| `bindaddr` | Address to bind the client/server to | "" |  |  |
| `debug` | Debug mode | 0 | 0 | 1 |
| `dbg_curl` | Debug curl | 0 | 0 | 1 |
| `dbg_graphs` | Performance graphs | 0 | 0 | 1 |
| `dbg_gfx` | Show graphic library warnings and errors, if the GPU supports it (0: none, 1: minimal, 2: affects performance, 3: verbose, 4: all) | 0 | 0 | 4 |
| `dbg_stress` | Stress systems (Debug build only) | 0 | 0 | 0 |
| `dbg_stress_server` | Server to stress (Debug build only) | "localhost" |  |  |
| `http_allow_insecure` | Allow insecure HTTP protocol in addition to the secure HTTPS one. Mostly useful for testing. | 0 | 0 | 1 |
| `cl_race_binds_set` | What level the DDRace binds are set to (this is automated, you don't need to use this) | 0 | 0 | 1 |
| `cl_reconnect_timeout` | How many seconds to wait before reconnecting (after timeout, 0 for off) | 120 | 0 | 600 |
| `cl_reconnect_full` | How many seconds to wait before reconnecting (when server is full, 0 for off) | 5 | 0 | 600 |
| `cl_message_system_color` | System message color | 2817983 |  |  |
| `cl_message_client_color` | Client message color | 9633471 |  |  |
| `cl_message_highlight_color` | Highlighted message color | 65471 |  |  |
| `cl_message_team_color` | Team message color | 5636050 |  |  |
| `cl_message_color` | Message color | 255 |  |  |
| `cl_laser_rifle_inner_color` | Laser inner color for Rifle | 11206591 |  |  |
| `cl_laser_rifle_outline_color` | Laser outline color for Rifle | 11176233 |  |  |
| `cl_laser_sg_inner_color` | Laser inner color for Shotgun | 1467241 |  |  |
| `cl_laser_sg_outline_color` | Laser outline color for Shotgun | 1866773 |  |  |
| `cl_laser_door_inner_color` | Laser inner color for doors | 7701379 |  |  |
| `cl_laser_door_outline_color` | Laser outline color for doors | 7667473 |  |  |
| `cl_laser_freeze_inner_color` | Laser inner color for freezes | 12001153 |  |  |
| `cl_laser_freeze_outline_color` | Laser outline color for freezes | 11613223 |  |  |
| `cl_kill_message_normal_color` | Kill message normal color | 255 |  |  |
| `cl_kill_message_highlight_color` | Kill message highlight color | 255 |  |  |
| `cl_message_friend` | Enable coloring and the heart for friends | 1 | 0 | 1 |
| `cl_message_friend_color` | Friend message color | 65425 |  |  |
| `conn_timeout` | Network timeout | 100 | 5 | 1000 |
| `cl_show_ids` | Whether to show client ids in scoreboard | 0 | 0 | 1 |
| `cl_scoreboard_on_death` | Whether to show scoreboard after death or not | 1 | 0 | 1 |
| `cl_auto_race_record` | Save the best demo of each race | 1 | 0 | 1 |
| `cl_replays` | Enable/disable replays | 0 | 0 | 1 |
| `cl_replay_length` | Set the default length of the replays | 30 | 10 | 0 |
| `cl_race_record_server_control` | Let the server start the race recorder | 1 | 0 | 1 |
| `cl_demo_name` | Save the player name within the demo | 1 | 0 | 1 |
| `cl_demo_assume_race` | Assume that demos are race demos | 1 | 0 | 1 |
| `cl_race_ghost` | Enable ghost | 1 | 0 | 1 |
| `cl_race_ghost_server_control` | Let the server start the ghost | 1 | 0 | 1 |
| `cl_race_show_ghost` | Show ghost | 1 | 0 | 1 |
| `cl_race_save_ghost` | Save ghost | 1 | 0 | 1 |
| `cl_ddrace_scoreboard` | Enable DDRace Scoreboard | 1 | 0 | 1 |
| `cl_show_others` | Show players in other teams (2 to show own team only) | 0 | 0 | 2 |
| `cl_show_others_alpha` | Show players in other teams (alpha value, 0 invisible, 100 fully visible) | 40 | 0 | 100 |
| `cl_overlay_entities` | Overlay game tiles with a percentage of opacity | 0 | 0 | 100 |
| `cl_showquads` | Show quads (only interesting for mappers, or if your system has extremely bad performance) | 1 | 0 | 1 |
| `cl_background_color` | Background color | 128 |  |  |
| `cl_background_entities_color` | Background (entities) color | 128 |  |  |
| `cl_background_entities` | Background (entities) | "" |  |  |
| `cl_run_on_join` | Command to run when joining a server | "" |  |  |
| `cl_menu_map` | Background map in the menu | "auto" |  |  |
| `cl_rotation_radius` | Menu camera rotation radius | 30 | 1 | 500 |
| `cl_rotation_speed` | Menu camera rotations in seconds | 40 | 1 | 120 |
| `cl_camera_speed` | Menu camera speed | 5 | 1 | 40 |
| `cl_background_show_tiles_layers` | Whether draw tiles layers when using custom background (entities) | 0 | 0 | 1 |
| `cl_unpredicted_shadow` | Show unpredicted shadow tee (0 = off, 1 = on, -1 = don't even show in debug mode) | 0 | -1 | 1 |
| `cl_predict_freeze` | Predict freeze tiles (0 = off, 1 = on, 2 = partial (allow a small amount of movement in freeze)) | 1 | 0 | 2 |
| `cl_show_ninja` | Show ninja skin | 1 | 0 | 1 |
| `cl_show_hook_coll_other` | Show other players' hook collision line (2 to always show) | 1 | 0 | 2 |
| `cl_show_hook_coll_own` | Show own players' hook collision line (2 to always show) | 1 | 0 | 2 |
| `cl_hook_coll_size` | Size of hook collision line | 0 | 0 | 20 |
| `cl_hook_coll_alpha` | Alpha of hook collision line (0 invisible, 100 fully visible) | 100 | 0 | 100 |
| `cl_hook_coll_color_no_coll` | Specifies the color of a hookline that hits nothing. | 65407 |  |  |
| `cl_hook_coll_color_hookable_coll` | Specifies the color of a hookline that hits hookable tiles. | 6401973 |  |  |
| `cl_hook_coll_color_tee_coll` | Specifies the color of a hookline that hits tees. | 2817919 |  |  |
| `cl_chat_teamcolors` | Show names in chat in team colors | 0 | 0 | 1 |
| `cl_chat_reset` | Reset chat when pressing escape | 1 | 0 | 1 |
| `cl_chat_old` | Old chat style: No tee, no background | 0 | 0 | 1 |
| `cl_show_direction` | Show key presses (1 = other players', 2 = also your own) | 1 | 0 | 2 |
| `cl_old_gun_position` | Tees hold gun a bit higher like in TW 0.6.1 and older | 0 | 0 | 1 |
| `cl_confirm_disconnect_time` | Confirmation popup before disconnecting after game time (in minutes, -1 to turn off, 0 to always turn on) | 20 | -1 | 1440 |
| `cl_confirm_quit_time` | Confirmation popup before quitting after game time (in minutes, -1 to turn off, 0 to always turn on) | 20 | -1 | 1440 |
| `cl_timeout_code` | Timeout code to use | "" |  |  |
| `cl_dummy_timeout_code` | Dummy Timeout code to use | "" |  |  |
| `cl_timeout_seed` | Timeout seed | "" |  |  |
| `cl_input_fifo` | Fifo file (non-Windows) or Named Pipe (Windows) to use as input for client console | "" |  |  |
| `cl_config_version` | The config version. Helps newer clients fix bugs with older configs. | 0 | 0 | 0 |
| `cl_demo_slice_begin` | Begin marker for demo slice | -1 | 0 | 0 |
| `cl_demo_slice_end` | End marker for demo slice | -1 | 0 | 0 |
| `cl_demo_show_speed` | Show speed meter on change | 0 | 0 | 1 |
| `cl_demo_show_pause` | Show pause/play indicator on change | 1 | 0 | 1 |
| `cl_demo_keyboard_shortcuts` | Enable keyboard shortcuts in demo player | 1 | 0 | 1 |
| `gfx_gl_major` | Graphic library major version | 1 | 1 | 10 |
| `gfx_gl_minor` | Graphic library minor version | 3 | 0 | 10 |
| `gfx_gl_patch` | Graphic library patch version | 0 | 0 | 10 |
| `gfx_gl_texture_lod_bias` | The lod bias for graphic library texture sampling multiplied by 1000 | -500 | -15000 | 15000 |
| `gfx_3d_texture_analysis_ran` | Ran an analyzer to check if sampling 3D/2D array textures works correctly | 0 | 0 | 1 |
| `gfx_3d_texture_analysis_renderer` | The renderer on which the analysis was performed | "" |  |  |
| `gfx_3d_texture_analysis_version` | The version on which the analysis was performed | "" |  |  |
| `gfx_gpu_name` | The GPU's name, which will be selected by the backend. (if supported by the backend) | "auto" |  |  |
| `gfx_backend` | The backend to use (e.g. OpenGL or Vulkan) | "OpenGL" |  |  |
| `gfx_render_thread_count` | Number of threads the backend can use for rendering. (note: the value can be ignored by the backend) | 3 | 0 | 0 |
| `gfx_driver_is_blocked` | If 1, the current driver is in a blocked error state. | 0 | 0 | 1 |
| `cl_video_recorder_fps` | At which FPS the videorecorder should record demos. | 60 | 1 | 1000 |

## Client Commands

| Command | Arguments | Description |
| --- | --- | --- |
| `exec` | r\[file\] | Execute the specified file |
| `reset` | s\[config-name\] | Reset a config to its default value |
| `toggle` | s\[config-option\] i\[value 1\] i\[value 2\] | Toggle config value |
| `+toggle` | s\[config-option\] i\[value 1\] i\[value 2\] | Toggle config value via keypress |
| `toggle_local_console` |  | Toggle local console |
| `toggle_remote_console` |  | Toggle remote console |
| `clear_local_console` |  | Clear local console |
| `clear_remote_console` |  | Clear remote console |
| `dump_local_console` |  | Write local console contents to a text file |
| `dump_remote_console` |  | Write remote console contents to a text file |
| `console_page_up` |  | Previous page in console |
| `console_page_down` |  | Next page in console |
| `callvote` | s\['kick'\|'spectate'\|'option'\] s\[id\|option text\] ?r\[reason\] | Call vote |
| `vote` | r\['yes'\|'no'\] | Vote yes/no |
| `+emote` |  | Open emote selector |
| `emote` | i\[emote-id\] | Use emote |
| `+spectate` |  | Open spectator mode selector |
| `spectate` | i\[spectator-id\] | Switch spectator mode |
| `spectate_next` |  | Spectate the next player |
| `spectate_previous` |  | Spectate the previous player |
| `spectate_closest` |  | Spectate the closest player |
| `spectate_multiview` | i\[id\] | Add/remove Client-IDs to spectate them exclusively (-1 to reset) |
| `zoom+` |  | Zoom increase |
| `zoom-` |  | Zoom decrease |
| `zoom` | ?i | Change zoom |
| `set_view` | i\[x\]i\[y\] | Set camera position to x and y in the map |
| `goto_switch` | i\[number\]?i\[offset\] | View switch found (at offset) with given number |
| `goto_tele` | i\[number\]?i\[offset\] | View tele found (at offset) with given number |
| `say` | r\[message\] | Say in chat |
| `say_team` | r\[message\] | Say in team chat |
| `chat` | s\['team'\|'all'\] ?r\[message\] | Enable chat with all/team mode |
| `+show_chat` |  | Show chat |
| `echo` | r\[message\] | Echo the text in chat window |
| `+left` |  | Move left |
| `+right` |  | Move right |
| `+jump` |  | Jump |
| `+hook` |  | Hook |
| `+fire` |  | Fire |
| `+showhookcoll` |  | Show Hook Collision |
| `+weapon1` |  | Switch to hammer |
| `+weapon2` |  | Switch to gun |
| `+weapon3` |  | Switch to shotgun |
| `+weapon4` |  | Switch to grenade |
| `+weapon5` |  | Switch to laser |
| `+nextweapon` |  | Switch to next weapon |
| `+prevweapon` |  | Switch to previous weapon |
| `add_foe` | s\[name\] ?s\[clan\] | Add a foe |
| `remove_foe` | s\[name\] ?s\[clan\] | Remove a foe |
| `foes` |  | List foes |
| `add_friend` | s\[name\] ?s\[clan\] | Add a friend |
| `remove_friend` | s\[name\] ?s\[clan\] | Remove a friend |
| `friends` |  | List friends |
| `team` | i\[team-id\] | Switch team |
| `kill` |  | Kill yourself to restart |
| `tune_zone` | i\[zone\] s\[tuning\] i\[value\] | Tune in zone a variable to value |
| `dummy_connect` |  | Connect dummy |
| `dummy_disconnect` |  | Disconnect dummy |
| `dummy_reset` |  | Reset dummy |
| `quit` |  | Quit the client |
| `exit` |  | Quit the client |
| `restart` |  | Restart the client |
| `minimize` |  | Minimize the client |
| `connect` | r\[host\|ip\] | Connect to the specified host/ip |
| `disconnect` |  | Disconnect from the server |
| `ping` |  | Ping the current server |
| `screenshot` |  | Take a screenshot |
| `start_video` |  | Start recording a video |
| `stop_video` |  | Stop recording a video |
| `rcon` | r\[rcon-command\] | Send specified command to rcon |
| `rcon_auth` | r\[password\] | Authenticate to rcon |
| `rcon_login` | s\[username\] r\[password\] | Authenticate to rcon with a username |
| `play` | r\[file\] | Play the file specified |
| `record` | ?r\[file\] | Record to the file |
| `stoprecord` |  | Stop recording |
| `add_demomarker` |  | Add demo timeline marker |
| `begin_favorite_group` |  | Use this before \`add\_favorite\` to group favorites. End with \`end\_favorite\_group\` |
| `end_favorite_group` |  | Use this after \`add\_favorite\` to group favorites. Start with \`begin\_favorite\_group\` |
| `add_favorite` | s\[host\|ip\] ?s\['allow\_ping'\] | Add a server as a favorite |
| `remove_favorite` | r\[host\|ip\] | Remove a server from favorites |
| `demo_slice_start` |  |  |
| `demo_slice_end` |  |  |
| `demo_play` |  | Play demo |
| `demo_speed` | i\[speed\] | Set demo speed |
| `save_replay` | ?i\[length\] s\[filename\] | Save a replay of the last defined amount of seconds |
| `benchmark_quit` | i\[seconds\] r\[file\] | Benchmark frame times for number of seconds to file, then quit |

## Tunings

| Tuning | Description | Default |
| --- | --- | --- |
| `ground_control_speed` | Max speed the tee can get on ground | 10.0 |
| `ground_control_accel` | Acceleration speed on the ground | 2.0 |
| `ground_friction` | Friction on the ground | 0.5 |
| `ground_jump_impulse` | Impulse when jumping on ground | 13.2 |
| `air_jump_impulse` | Impulse when jumping in air | 12.0 |
| `air_control_speed` | Max speed the tee can get in the air | 5.0 |
| `air_control_accel` | Acceleration speed in air | 1.5 |
| `air_friction` | Friction in the air | 0.95 |
| `hook_length` | Length of the hook | 380.0 |
| `hook_fire_speed` | How fast the hook is fired | 80.0 |
| `hook_drag_accel` | Acceleration when hook is stuck | 3.0 |
| `hook_drag_speed` | Drag speed of the hook | 15.0 |
| `gravity` | Gravity of the teeworld | 0.5 |
| `velramp_start` | Velocity ramp start | 550 |
| `velramp_range` | Velocity ramp range | 2000 |
| `velramp_curvature` | Velocity ramp curvature | 1.4 |
| `gun_curvature` | Gun curvature | 1.25\[Vanilla\] 0.0\[DDRace\] |
| `gun_speed` | Gun speed | 2200.0 |
| `gun_lifetime` | Gun lifetime | 2.0 |
| `shotgun_curvature` | Shotgun curvature | 1.25 |
| `shotgun_speed` | Shotgun speed | 2750.0 |
| `shotgun_speeddiff` | (UNUSED) Speed difference between shotgun bullets | 0.8 |
| `shotgun_lifetime` | (UNUSED) Shotgun lifetime | 0.20 |
| `grenade_curvature` | Grenade curvature | 7.0 |
| `grenade_speed` | Grenade speed | 1000.0 |
| `grenade_lifetime` | Grenade lifetime | 2.0 |
| `laser_reach` | How long the laser can reach | 800.0 |
| `laser_bounce_delay` | When bouncing, stop the laser this long | 150 |
| `laser_bounce_num` | How many times the laser can bounce | 1000 |
| `laser_bounce_cost` | Remove this much from reach when laser is bouncing | 0 |
| `laser_damage` | (UNUSED) Laser damage | 5 |
| `player_collision` | Enable player collisions | 1 |
| `player_hooking` | Enable player vs player hooking | 1 |
| `jetpack_strength` | Jetpack pistol strength | 400.0 |
| `shotgun_strength` | Shotgun pull strength | 10.0 |
| `explosion_strength` | Explosion strength (grenade for example) | 6.0 |
| `hammer_strength` | Hammer strength | 1.0 |
| `hook_duration` | Hook duration | 1.25 |
| `hammer_fire_delay` | Delay of hammering (when hitting nothing) | 125 |
| `gun_fire_delay` | Delay of firing gun | 125 |
| `shotgun_fire_delay` | Delay of firing shotgun | 500 |
| `grenade_fire_delay` | Delay of firing grenade | 500 |
| `laser_fire_delay` | Delay of firing laser laser | 800 |
| `ninja_fire_delay` | Delay of firing ninja | 800 |
| `hammer_hit_fire_delay` | Delay of hammering (when hitting another tee) | 320 |
| `ground_elasticity_x` | Wall elasticity | 0 |
| `ground_elasticity_y` | Ground/ceiling elasticity | 0 |
