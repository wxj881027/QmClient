By DDNet Wiki contributors. [Source revision 1430](https://wiki.ddnet.org/wiki/Special:PermanentLink/1430) · [Contributors and edit history](https://wiki.ddnet.org/w/index.php?title=Binds&action=history) · [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/). QmClient reformatted the article and omitted site navigation and embedded media.

DDNet client has a multitude of [commands and settings](https://ddnet.org/settingscommands/#client-settings) that can be *bound* to nearly any key or mouse button. You can enter commands into the console, which by default is accessed by pressing **F1.**

## Syntax

### Single binds

```
bind [key] [command/setting] [value]
```

This is the basic syntax for simple binds.

- **\[key\]** is the key on your keyboard/mouse you want to bind a command to.
- **\[command/setting\]** A full list of all commands and settings can be found [here](https://ddnet.org/settingscommands/#client-settings).
- **\[value\]** is the variable you want to set your setting to or an argument for a command. Not all commands require arguments

You can find keycodes [here](https://github.com/ddnet/ddnet/blob/master/src/engine/client/keynames.h), or just type the key temporarily into one of the controls in the Settings > Controls page in DDNet client and it will show the key name. You can also use modifier keys with binds.

```
bind ctrl+[key] [command/setting] [value]
```

you can use **ctrl**, **alt**, **shift**, and **gui**. **Gui** is used to refer to the windows key on windows, the command key on macOS/osx, and super/meta on linux.

### Multiple binds

You can also chain commands/settings together by separating them using a semicolon and wrapping them in quotes. You can add as many commands to this chain as you want.

```
bind [key] "[command/setting] [value]; [command/setting] [value]"
```

It's important to know that when chaining things together like this, all commands/settings get executed by the game at the exact same time, meaning they will not execute sequentially in the order you write them. There is no sleep or delay type command to space things out the way you want them. The best way to make things happen in an ordered sequence is to either bind a different key for each step of the sequence, or to use [config files](https://wiki.ddnet.org/wiki/Binds#Using_Config_Files) to press one key multiple times to read multiple files sequentially.

### Nested binds

You can nest multiple quoted strings in a single line by escaping each new set of quotes with backslashes.

```
bind [key] "bind [key] \"[command/setting] [value]; bind [key] \\\"[command/setting] [value] \\\"\""
```

As the quoted strings go deeper, you also need to escape the backslash. Since each new layer will again un-escape them, you need to double the amount of backslashes per layer. The maximum amount of layers you can use is **5** before the console gives you an error. This is because the console has a maximum character limit of **255**. The amount of backslashes for 6 layers is **240** (1\*2 + 3\*2 + 7\*2 + 15\*2 + 31\*2 + 63\*2), leaving you with only **15** characters.

```
1 = 1   \
2 = 3   \\\
3 = 7   \\\\\\\
4 = 15  \\\\\\\\\\\\\\\
5 = 31  \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\
```

If this is confusing to look at, try making a new line and indent for each escaped quote to get a more clear visual idea of what's happening.

```
 bind [key]
 "bind [key]
  \"[command/setting] [value]; bind [key]
   \\\"[command/setting] [value]; bind [key]
    \\\\\\\"[command/setting] [value]
    \\\\\\\"
   \\\"
  \"
 "
```

**Note:** You cannot run a config file formatted as the example above is, it will not work. This example is just to get a clear visual idea of how a nested bind is constructed.

### Useful Commands & Settings

#### Toggles

Toggles are a way to use a single key to alternate between 2 values for a setting. There are two types of toggles.

- **toggle** is used so that each time you press a key, it will change between one of the two values for the setting
- **+toggle** is used so that your first value is set only while the key is being pressed, and the second value is set when the key is released.

```
bind [key] toggle [setting] [value1] [value2]
```

```
bind [key] +toggle [setting] [value1] [value2]
```

Toggles only work for client settings. They cannot be used for client or chat commands.

#### Say

Say is an important client command for binding. It allows you to incorporate chat commands into a bind as well as just send text to the chat.

```
bind [key] say /[chat command]
```

```
bind [key] say [chat message]
```

#### Colors

Some client commands allow you to set colors.

- **player\_color\_body**
- **player\_color\_feet**
- **cl\_message\_client\_color**

are a few examples.

To get the correct color values, go to **Settings > Tee**, and use the sliders to find the color you want. Go into the console and type the command for the color you're trying to set, by itself with no value. The number that comes after "**Value:**" is the relevant number.

```
player_color_body 5635840
```

or, if you prefer to use the hexadecimal value:

```
player_color_body $00FF00
```

It will also accept a few color values by name. **red**, **yellow**, **green**, **cyan**, **blue**, **magenta**, **white, gray, black** all work.

#### Emotes

The **emote** command can be used to display emotes without using the emote wheel. Each emote is represented by a different number.



| Command | Emote |
| --- | --- |
| `emote 0` | oop |
| `emote 1` | exclamation |
| `emote 2` | hearts |
| `emote 3` | drop |
| `emote 4` | dotdot |
| `emote 5` | music |
| `emote 6` | sorry |
| `emote 7` | ghost |
| `emote 8` | sushi |
| `emote 9` | splattee |
| `emote 10` | deviltee |
| `emote 11` | zomg |
| `emote 12` | zzz |
| `emote 13` | wtf |
| `emote 14` | eyes |
| `emote 15` | question |

For example:

```
bind h emote 14
```

#### Other

```
binds [key]
```

You can use this command to print what's bound to a single key. Use it without a key at the end to get a full list of all your current binds.

```
unbind [key]
```

Will unbind whatever key you specify.

## Using Config Files

### Syntax

For more complicated binds, you can make text files containing settings/commands that get executed via a key press.

**settings.cfg**

```
[command/setting] [value]
[command/setting] [value]
[command/setting] [value]; [command/setting] [value]
[command/setting] [value]; [command/setting] [value]

bind [key] [command/setting] [value]
bind [key] exec [file.cfg]
```

Settings/commands can be chained together via a semicolon or by just using a new line. You can put as many commands/settings/binds as you want in these files.

**Note:** You don't need to use **.cfg** as the extension. You can actually use any file extension you want. .**cfg** just seems to be the standard.

### Path/Exec

In order to execute these files, you need to put them in correct directory and execute them with the **exec** command. The correct path depends on your operating system. To find the right path for your operating system, see [Where is the DDNet config, config directory or save directory?](https://wiki.ddnet.org/wiki/Special:MyLanguage/FAQ#Where_is_the_DDNet_config%2C_config_directory_or_save_directory%3F)

Once the files are in the correct path, you can execute them in the game console with the **exec** command.

```
exec [file.cfg]
```

or

```
bind [key] exec [file.cfg]
```

You can use sub-directories within the 'DDNet' or 'Teeworlds' directory to organize files. Just include the path when you execute the file.

```
bind [key] exec [path/to/file.cfg]
```

### Improved toggle with config files

The most common reason to use config files is to create a single-key toggle to alternate between two files.

**file1.cfg**

```
[command/setting] [value]
[command/setting] [value]
[command/setting] [value]

bind [key] exec file2.cfg
```

**file2.cfg**

```
[command/setting] [value]
[command/setting] [value]
[command/setting] [value]

bind [key] exec file1.cfg
```

In this case, **\[key\]** would be the same key in both files. This way each time you press the key, it will switch to the other file. Once you have both of these files in the correct path, you can **exec** either one just once and they will be bound forever until you unbind them.

```
bind [key] exec file1.cfg
```

You can use this method to cycle between any amount of files.

### Customization

#### Echo

As a more clear way to show which file you're activating when you press a bind, you can use the **echo** command to display which one is active.

```
[command/setting] [value]
[command/setting] [value]

echo setting ON

bind [key] exec file1.cfg
```

To take this a step further, there is a command to change the color of the echo messages.

```
[command/setting] [value]
[command/setting] [value]

cl_message_client_color red
echo setting OFF

bind [key] exec file1.cfg
```

Having different colors for "on" and "off" binds makes it a bit easier to notice which setting you've activated. If you want to get more creative with colors, see [colors](https://wiki.ddnet.org/wiki/Binds#Colors).

#### Comments

You can put comments in your bind files too.

```
# These lines change some settings
[command/setting] [value]
[command/setting] [value]

# This next line rebinds the key
bind [key] exec file1.cfg
```

## Examples

### Hook line always on

The command **cl\_show\_hook\_coll\_own**, sets whether you will see your own hook collision line or not when you hold your **+showhookcoll** bind (defaulted to **S**). When set to **2**, your hook collision line will always show only to you. Others players won't see it.

**hook\_line\_on.cfg**

```
cl_show_hook_coll_own 2

cl_message_client_color green
echo Hook Line ON

bind x exec hook_line_off.cfg
```

**hook\_line\_off.cfg**

```
cl_show_hook_coll_own 1

cl_message_client_color red
echo Hook Line OFF

bind x exec hook_line_on.cfg
```

Put both of these in the correct path, and in console:

```
bind x exec hook_line_on.cfg
```

### Everyone's hooklines

The command **cl\_show\_hook\_coll\_other** allows you to see other tee's hooklines, which can be useful when they are out of your view. You can add this to the basic hookline bind to "upgrade" it:

**cl\_show\_hook\_coll\_other**

```
bind x "+showhookcoll; +toggle cl_show_hook_coll_other 2 1"
```

### Deep Fly

The deep fly bind is a way to make your dummy hammer/fire toward you whenever you fire with your main tee. If your dummy is deep frozen, they can hammer back at you when you hammer them with this bind on. You can use this to fly with your dummy, but it takes practice. If your dummy is not deep frozen, you can just use this as a alternative version of the regular Dummy Hammerfly bind included in the games settings, where you can fire your pistol each time you want to be hammered. It offers better control since you can control the timing of your dummy's hammers.

**Note:** If the mapper has set `sv_deepfly 0` on their map, you will not be able to deep fly with a deep frozen tee, since it can sometimes enable players to do parts in ways that the mapper did not intend. The deep fly bind will still work normally with frozen or unfrozen tees. As a player, this is not a setting you can adjust.

The command **cl\_dummy\_hammer 1** is what is used for the regular Dummy Hammerfly bind in the games control options. By using a [toggle](https://wiki.ddnet.org/wiki/Binds#Toggles), you can make your dummy hammer you only when you fire, and can make an on/off switch with config files.

#### Using 2 Config Files

**deepfly\_on.cfg**

```
bind mouse1 "+fire; +toggle cl_dummy_hammer 1 0"

cl_message_client_color green
echo Deep Fly ON

bind x exec deepfly_off.cfg
```

**deepfly\_off.cfg**

```
bind mouse1 +fire
cl_dummy_hammer 0

cl_message_client_color red
echo Deep Fly OFF

bind x exec deepfly_on.cfg
```

Put both of these in the correct path, and in console:

```
bind x exec deepfly_on.cfg
```

#### Using 1 Config File

You can accomplish the same thing as above using only 1 config file.

```
bind x "bind mouse1 \"+fire; +toggle cl_dummy_hammer 1 0\";cl_message_client_color green; echo Deep Fly ON; bind x \"bind mouse1 +fire; cl_dummy_hammer 0; cl_message_client_color red; echo Deep Fly OFF; exec deepfly.cfg\""
```

Put this file in the correct path, and in console:

```
bind x exec deepfly.cfg
```

**Note 1:** This assumes mouse1 is your **+fire** bind.

**Note 2:** If you turn deep fly off while you're holding your **+fire** bind, your dummy will get stuck in a hammering rhythm since **cl\_dummy\_hammer** is set to **1** still. The **cl\_dummy\_hammer 0** line in **deepfly\_off.cfg** fixes this.

#### Weapon Swapping

If your dummy isn't holding hammer when you turn the bind on, they will switch back and forth between pistol and hammer. If you want to fix this:

```
cl_dummy_restore_weapon 0
```

Now if they're holding pistol when you start using the bind, they will switch to hammer and stay there after 1 pistol shot. There are however some benefits of having this setting set to **1**.

```
cl_dummy_restore_weapon 1
```

This will allow you to laser yourself with your dummy with perfect aim. It can also be useful with shotgun.

To manage dealing with both of these settings, you can either create a [toggle](https://wiki.ddnet.org/wiki/Binds#Toggles), use [config files](https://wiki.ddnet.org/wiki/Binds#Using_Config_Files), or have this setting attached to your weapon binds.

```
bind [key] "+weapon1; cl_dummy_restore_weapon 0"
```

```
bind [key] "+weapon5; cl_dummy_restore_weapon 1"
```

This of course only works if you have dedicated weapon binds as opposed to **+nextweapon** and **+prevweapon** which is by default bound to **mousewheelup** and **mousewheeldown**.

### 45° Aiming

This bind allows you to only aim in 45° angles when holding down the key. This is useful for shooting rockets straight up for double rockets and a few other scenarios.

```
bind x "+toggle cl_mouse_max_distance 2 400; +toggle inp_mousesens 1 200; +showhookcoll"
```

Keep in mind that if you have changed **cl\_mouse\_max\_distance** or **inp\_mousesens** from the default, using the example above will reset those settings to default. Before you just enter the binds, check your settings for both first by typing **cl\_mouse\_max\_distance** and **inp\_mousesens** into the console.

```
bind x "+toggle cl_mouse_max_distance 2 [Your Value]; +toggle inp_mousesens 1 [Your Value]; +showhookcoll"
```

**Note:** the **+showhookcoll** line is optional. It's just helpful to see which way you're aiming since your tee's eyes don't follow the 45° constraints, and by nature of how this bind works, your crosshair won't show direction either.

### Rainbow Tee

You will sometimes see players rapidly changing color as they play. To do this you just have to bind a **player\_color\_body** change to each of your regular movement inputs.

```
bind a "+left; player_color_body red"
```

You can do this for several common inputs and get a variety of to cycle through.

**rainbow\_tee.cfg**

```
bind a "+left; player_color_body red"
bind d "+right; player_color_body yellow"
bind space "+jump; player_color_body green"
bind mouse1 "+fire; player_color_body blue"
bind mouse2 "+hook; player_color_body cyan"
```

Here's an example of what a rainbow tee file looks like. You could save that in the correct path and execute it once.

```
exec rainbow_tee.cfg
```

Or if you want a key to toggle it on and off:

**rainbow\_tee\_on.cfg**

```
bind a "+left; player_color_body red"
bind d "+right; player_color_body yellow"
bind space "+jump; player_color_body green"
bind mouse1 "+fire; player_color_body blue"
bind mouse2 "+hook; player_color_body cyan"

bind x exec rainbow_tee_off.cfg
```

**rainbow\_tee\_off.cfg**

```
bind a +left
bind d +right
bind space +jump
bind mouse1 +fire
bind mouse2 +hook

bind x exec rainbow_tee_on.cfg
```

Put both of these in the correct path, and in console:

```
bind x exec rainbow_tee_on.cfg
```

You can of course get more creative with colors and use **player\_color\_feet** too. To learn more about how colors work, see [colors](https://wiki.ddnet.org/wiki/Binds#Colors)

**Note:** This assumes you have default binds for left, right, jump, fire, and hook. There is also a delay between color changes, so changes don't happen the instant you press a key.

## See also

- [Just bind it](https://forum.ddnet.org/viewtopic.php?t=2537) - Useful binds available for copying
- [Take Binding to the next level, with Scripting!](https://forum.ddnet.org/viewtopic.php?t=5819) - Another forum post about more advanced binds
- [Teeworlds New Binding Tutorial by Klex](https://www.youtube.com/watch?v=sELsw2I2jj8&) - A video tutorial about binds by KlexMikrowelle
