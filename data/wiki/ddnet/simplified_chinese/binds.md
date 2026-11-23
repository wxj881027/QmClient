来源为 DDNet Wiki 贡献者。[原文修订 21032](https://wiki.ddnet.org/wiki/Special:PermanentLink/21032) · [贡献者与编辑历史](https://wiki.ddnet.org/w/index.php?title=Binds/zh&action=history) · [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)。QmClient 转换了排版并省略站点导航及媒体，补译处已标注。

在《DDNet》里你能通过F1控制台将[设置与指令](https://wiki.ddnet.org/wiki/Special:MyLanguage/Settings_and_Commands)**绑定**到您的键盘和鼠标上。

## 语法

### 单指令绑定

```
bind [按键] [设置／指令] [参数]
```

此为绑定的基本语法。

- **\[按键\]**：键盘和鼠标上您所选的任意按键。
- **\[设置／指令\]**：您可以在官方网站（[客户端设置](https://ddnet.org/settingscommands/#client-settings)／[客户端指令](https://ddnet.org/settingscommands/#client-commands)）或站内页面（[客户端设置](https://wiki.ddnet.org/wiki/Settings_and_Commands/zh#%E5%AE%A2%E6%88%B7%E7%AB%AF%E8%AE%BE%E7%BD%AE)／[客户端指令](https://wiki.ddnet.org/wiki/Settings_and_Commands/zh#%E5%AE%A2%E6%88%B7%E7%AB%AF%E6%8C%87%E4%BB%A4)）查找相关设置和指令。
- **\[参数\]**：您想要调整的设置或指令的变量或参数。（仅有某些设置需要使用参数，指令和某些设置无需参数）

倘若您不知道某个按键的名称，可以在[这个页面](https://github.com/ddnet/ddnet/blob/master/src/engine/client/keynames.h)中查找，亦或者在游戏设置 > 控制中随意找一格按下你想要的按键，即会显示按键名称。您也可以与修饰键`ctrl`、`⇧ Shift`、`alt`和GUI键（即Windows的`⊞ Win`、Mac的`⌘ Command`、Linux的`Super`）一起使用，如下：

```
bind ctrl+[按键] [设置／指令] [参数]
```

### 多指令绑定

您可以使用分号将多条设置与指令绑定到单一按键上：

```
bind [按键] "[设置／指令] [参数];[设置／指令] [参数]"
```

当您按下此按键后，所有绑定到这个按键的设置与指令会在同一时间被激活，换言之，这些设置与指令不会按照您所写的顺序依次激活。如果您想它们依次激活，您可以将它们分别绑定到不同的按键上，依次按下以激活，或者[使用配置文件](https://wiki.ddnet.org/wiki/Special:MyLanguage/Binds#Using_Config_Files_config_files)让您多次按下同一按键时顺序激活不同的设置与指令。

一条绑定中可以继续加指令。用分号分隔并用引号包起来即可。控制台没有能在同一条绑定内插入等待的命令。（QmClient 补译）

### 嵌套绑定

您可以使用反斜杠将一条绑定嵌套在另一绑定中：

```
bind [按键] "bind [按键] \"[设置／指令] [参数]; bind [按键] \\\"[设置／指令] [参数] \\\"\""
```

客户端的字符长度限制为255，而每一层嵌套的反斜杠数量为上一层的反斜杠数量乘二加一。倘若您使用六层嵌套，反斜杠数量将会是240个（1\*2 + 3\*2 + 7\*2 + 15\*2 + 31\*2 + 63\*2），这样您只剩15个字符可键入，因此只能接受最多五层嵌套。每一层的反斜杠数量如下：

```
 1 = 1   \
2 = 3   \\\
3 = 7   \\\\\\\
4 = 15  \\\\\\\\\\\\\\\
5 = 31  \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\
```

如果您仍感到疑惑，可看下方的拆解：

```
bind [按键]
 "bind [按键]
  \"[设置／指令] [参数]; bind [按键]
   \\\"[设置／指令] [参数]; bind [按键]
    \\\\\\\"[设置／指令] [参数]
    \\\\\\\"
   \\\"
  \"
 "
```

**提示：**你无法通过以上代码使一个cfg文件运行。这个例子只是为了清晰的展示出嵌套的用法。

### 实用设置与指令

#### Toggle

toggle（切换）指令可以让您在单个按键切换设置的两个参数。客户端中有两种切换方式。

- **toggle**：令您每按一次按键时切换为另一个参数。
- **+toggle**：令您在按下按键时使用第一个参数，松开按键时则使用第二个参数。

```
bind [按键] "toggle [设置] [参数1] [参数2]"
```

```
bind [按键] "+toggle [设置] [参数1] [参数2]"
```

`toggle` 和 `+toggle` 只能切换客户端设置的值，不能用来切换客户端或聊天指令。（QmClient 补译）

#### Say

Say（说）指令可以帮您在聊天框里快速输入文本。

```
bind [按键] say /[聊天框指令]
```

```
bind [按键] say [聊天信息]
```

#### 颜色

某些客户端指令可以让您设置颜色。

- **player\_color\_body**
- **player\_color\_feet**
- **cl\_message\_client\_color**

以上为部分例子。

想知道您所要的颜色参数，您可先在设定内调好，再在F1控制台内查询。例如在 设置 > Tee 里将本体身体颜色调成绿色后，可以在控制台内使用与其所对应的客户端指令`player_color_body`，所返回在**Value:**后的一串数字则是您所调的绿色的参数。

然后您可使用以下绑定快速设置颜色：

```
bind [按键] player_color_body 5635840
```

或者使用十六进制代码：

```
bind [按键] player_color_body $00FF00
```

客户端也支持直接使用某些颜色名：**red**，**yellow**，**green**，**cyan**，**blue**，**magenta**，**white**，**gray**，**black**。

#### Emote

emote（表情）指令可以让您在不使用表情选择器下快速发出表情。

| 指令与参数 | 表情解释 | 英语原文 |
| --- | --- | --- |
| `emote 0` | 哎呀 | oop |
| `emote 1` | 惊叹 | exclamation |
| `emote 2` | 爱心 | hearts |
| `emote 3` | 流汗 | drop |
| `emote 4` | 无语 | dotdot |
| `emote 5` | 哼歌 | music |
| `emote 6` | 抱歉 | sorry |
| `emote 7` | 鬼魂 | ghost |
| `emote 8` | 发怒 | sushi |
| `emote 9` | 谩骂Tee | splattee |
| `emote 10` | 魔鬼Tee | deviltee |
| `emote 11` | 粗鄙之语 | zomg |
| `emote 12` | 打哈欠 | zzz |
| `emote 13` | 什么鬼 | wtf |
| `emote 14` | 笑脸 | eyes |
| `emote 15` | 疑问 | question |

假如您想发出鬼魂表情时，您要输入：

```
bind [按键] emote 14
```

#### 其它

如果您想知道某个按键的绑定，输入：

```
binds [按键]
```

当您不输入按键时，则会显示所有按键的绑定。

如果您想取消某个按键的绑定，输入：

```
unbind [按键]
```

## 使用配置文件

### 语法

对于更复杂的绑定，您可以创建并使用配置文件。

**settings.cfg**

```
[设置／指令] [参数]
[设置／指令] [参数]
[设置／指令] [参数]; [设置／指令] [参数]
[设置／指令] [参数]; [设置／指令] [参数]

bind [按键] [设置／指令] [参数]
bind [按键] exec [文件名.cfg]
```

如上，在配置文件里，多条设置／指令可以以分号或分行分隔开。您可将您所要的设置／指令／绑定都写在单个配置文件里，然后将其绑定在按键上：

把执行配置文件的 `exec` 指令绑定到按键上。（QmClient 补译）

```
bind [按键] exec [settings.cfg]
```

**备注**：后缀名**.cfg**可以更改为任何您想要的后缀名。

### 路径与执行

您需要将配置文件放在配置目录中，有关于配置目录的路径，详见：[《DDNet》的配置文件，配置目录在哪？](https://wiki.ddnet.org/wiki/Special:MyLanguage/FAQ#Where_is_the_DDNet_config%2C_config_directory_or_save_directory%3F)

当您将配置文件放在正确的路径，您可以在F1控制台使用`exec`指令执行它：

不绑定按键时，可以直接在 F1 控制台执行文件。（QmClient 补译）

```
exec [配置文件.cfg]
```

也可以把执行文件的操作绑定到一个按键。（QmClient 补译）

```
bind [按键] exec [配置文件.cfg]
```

当您将配置文件放在配置目录下的文件夹中，您需把文件夹的名也给打上：

```
bind [按键] exec [文件夹/文件夹/配置文件.cfg]
```

### toggle进阶：cfg文件

配置文件可以让您更容易的切换，您只需要创建两个配置文件：

**配置文件1.cfg**

```
[设置／指令] [参数]
[设置／指令] [参数]
[设置／指令] [参数]

bind [按键] exec 配置文件2.cfg
```

**配置文件2.cfg**

```
[设置／指令] [参数]
[设置／指令] [参数]
[设置／指令] [参数]

bind [按键] exec 配置文件1.cfg
```

在两个配置文件里的\[按键\]需为同一按键。将其中一个配置文件绑定到\[按键\]中：

```
bind [按键] exec 配置文件1.cfg
```

这样您每按一次按键，都会执行相对配置文件的设置／指令并将按键绑定为另一个配置文件，以此达到循环切换。您还可以在此循环内加入更多的配置文件。

### 自定义

#### Echo

echo（回应）指令可以让您更清楚的了解您执行了什么设置／指令。

```
[设置／指令] [参数]
[设置／指令] [参数]

echo 设置开

bind [按键] exec 配置文件1.cfg
```

这样当您按下\[按键\]时，聊天框内会提示“设置开”。

您更可以加上颜色：

```
[设置／指令] [参数]
[设置／指令] [参数]

cl_message_client_color red
echo 设置关

bind [按键] exec 配置文件1.cfg
```

这样当您按下\[按键\]时，聊天框内会提示“设置关”。将开关定为不同的颜色，可方便您了解您执行了什么设置／指令。

#### 注释

在配置文件里，您可使用注释：

```
 # 以下为设置／指令
[设置／指令] [参数]
[设置／指令] [参数]

# 以下为将按键绑定为另一配置文件
bind [按键] exec 配置文件1.cfg
```

## 绑定例子

### 辅助线常开

`cl_show_hook_coll_own`指令能让您在不用`+showhookcoll`绑定（默认为`S`键）下显示辅助线。当您将其参数设为`2`时，您的辅助线会对您常显示（其他玩家不会看见）。

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

将这两个配置文件放在配置目录里，并在F1控制台输入：

```
bind x exec hook_line_on.cfg
```

### 显示所有玩家辅助线

`cl_show_hook_coll_other`指令能让您看到所有玩家的辅助线。您可以直接将其添加到默认绑定中：

```
bind s "+showhookcoll; +toggle cl_show_hook_coll_other 2 1"
```

### 深度冻结飞行

深度冻结飞行（英语：Deep Fly）绑定可以让您的分身朝您锤击／开火。如果您的分身处于深度冻结状态，您使用此绑定锤击它时，它们也会锤击您。以此，在练习后您可与在深度冻结状态下的分身飞行（某些地图会设置成深度冻结状态下不能锤击／开火）。而如果没有被深度冻结，您也可以以此绑定取代默认的Hammerfly绑定，这能让您更好的控制分身锤击时间。

**提示：**如果地图作者将地图设置为 `sv_deepfly 0` ，你将无法与这个深度冻结的tee进行飞行，否则该技巧能可能让玩家不以作者设想的方式来通过地图。深度冻结飞行的绑定对冻结或解冻状态下的tee仍然有效。作为地图里的玩家，这项设置是无法改变的。

`cl_dummy_hammer 1`是一般在设置里的分身Hammerfly绑定。使用+toggle指令就能达到在按下按键时，分身才锤击的效果，如下：

#### 使用两个配置文件

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

将这两个配置文件放在配置目录里，并在F1控制台输入：

```
bind x exec deepfly_on.cfg
```

#### 使用单个配置文件

您也可以将以上两个配置文件整合：

```
 bind x "bind mouse1 \"+fire; +toggle cl_dummy_hammer 1 0\";cl_message_client_color green; echo Deep Fly ON; bind x \"bind mouse1 +fire; cl_dummy_hammer 0; cl_message_client_color red; echo Deep Fly OFF; exec deepfly.cfg\""
```

将这个配置文件放在配置目录里，并在F1控制台输入：

```
bind x exec deepfly.cfg
```

**备注一**：配置文件中假定了您的`+fire`（开火）绑定在`mouse1` （鼠标左键）上。

**备注二**：如果您使用单个配置文件，在您按住开火键时关闭绑定的话，会出现所谓卡键的情况，因为您的`cl_dummy_hammer`指令参数固定为`1`。您只能通过使用两个配置文件解决。

#### 武器切换

cl\_dummy\_restore\_weapon 1 您的设置默认为以上，因此当您的分身并非拿着锤子而您去使用深度冻结飞行绑定时会出现如视频中的情况，这有一定的好处。您可以将参数更改为`0`，让分身武器自动切换为锤子：

```
cl_dummy_restore_weapon 0
```

当分身手持手枪时，上面的设置会让它开一枪后切到锤子并保持持锤。将武器恢复设为 `1` 时，分身可以用激光枪精确解救本体，用霰弹枪时也可以利用这种切换行为。（QmClient 补译）

```
cl_dummy_restore_weapon 1
```

您也可以自行运用toggle或者配置文件将下面指令绑定至切换武器的按键上。

```
bind [按键] "+weapon1; cl_dummy_restore_weapon 0"
```

```
 bind [按键] "+weapon5; cl_dummy_restore_weapon 1"
```

**备注**：请谨记不要使用`+nextweapon`和`+prevweapon`（默认绑定为鼠标滚轮向上和鼠标滚轮向下中）切换武器，否则将出现武器乱切换的情况。

### 45度角

这个绑定能让您锁死角度，只剩下八个方向（每45度）：

```
bind x "+toggle cl_mouse_max_distance 2 400; +toggle inp_mousesens 1 200; +showhookcoll"
```

您需要在F1控制台输入`cl_mouse_max_distance`查询参数，并将`400`替换成查询参数；以及在F1控制台输入`inp_mousesens`查询参数，并将`200`替换成查询参数：

```
bind x "+toggle cl_mouse_max_distance 2 [您的参数]; +toggle inp_mousesens 1 [您的参数]; +showhookcoll"
```

锁定 45° 方向可以让火箭笔直向上发射，例如双火箭技巧中。`+showhookcoll` 可以省略，它的作用是显示方向。Tee 的眼睛和准星不会反映绑定限定的方向。（QmClient 补译）

### 变色Tee

要达到变色的效果，你需要将`player_color_body`指令绑定到移动或操作键上，例如：

```
bind a "+left; player_color_body red"
```

您可使用配置文件，以便于调整。以下为例子： **rainbow\_tee.cfg**

```
bind a "+left; player_color_body red"
bind d "+right; player_color_body yellow"
bind space "+jump; player_color_body green"
bind mouse1 "+fire; player_color_body blue"
bind mouse2 "+hook; player_color_body cyan"
```

将这配置文件放在配置目录里，并在F1控制台输入：

```
exec rainbow_tee.cfg
```

您可使用多个配置文件，方便开关。以下为一组例子： **rainbow\_tee\_on.cfg**

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

将这两个配置文件放在配置目录里，并在F1控制台输入：

```
bind x exec rainbow_tee_on.cfg
```

您可以加入`player_color_feet`指令达到更有趣的变色效果。要了解关于颜色的指令。

**备注**：配置文件中假定了您的的绑定皆为默认绑定。

颜色切换有间隔，按键后可能过一会儿才显示下一种颜色。（QmClient 补译）

## 另见

- [Just bind it](https://forum.ddnet.org/viewtopic.php?t=2537) - 实用绑定
- [Take Binding to the next level, with Scripting!](https://forum.ddnet.org/viewtopic.php?t=5819) - 进阶绑定
- [Teeworlds New Binding Tutorial by Klex](https://www.youtube.com/watch?v=sELsw2I2jj8&) - 玩家KlexMikrowelle关于绑定的教程视频
