# 头衔 Style 规格（Calamity Mod 复刻基准）

本文是头衔动态渐变系统的**唯一真源**。所有色值、周期、插值公式均以 Calamity Mod 源码为准，
不得凭印象修改。新增 style 必须先改本文，再改代码。

## 1. 来源与版本（可追溯）

| 项 | 值 |
|---|---|
| 仓库 | `CalamityTeam/CalamityModPublic` |
| 分支 | `1.4.4`（tModLoader 版本号，非 mod 版本） |
| commit | `1a8cebd27ec5615316b78f71973446b5528d2b78`（2026-08-08，`Merge Update 2.2.2 into release branch`） |
| mod 版本 | `2.2.2`（`build.txt`） |
| 原始文件 URL 模板 | `https://raw.githubusercontent.com/CalamityTeam/CalamityModPublic/<commit>/<路径>` |

> 官方主仓库 `CalamityTeam/CalamityMod` 已不可公开访问（404），使用上述官方镜像。
> 旧分支 `1.3-release` / `1.4-release` 的稀有度实现与本基准**不兼容**（`1.4-release` 全是静态色，
> 动画是 `1.4.4` 才引入的），本规格**不采用**。

### 1.1 关键结构性事实

1. 动画**不在** `ModRarity.RarityColor` 里（那里只有静态的 `TextClr * 2f`）。
   动画在 `GlobalItem.PreDrawTooltipLine` 拦截后、由自定义 `TextSnippet.UniqueDraw` **整行重画**。
2. `1.4.4` 中**不存在** `RarityHelper` / `CalamityRarity` / `CustomRarity` / `RarityLoader`，也没有 tooltip 相关的 IL 注入。
3. 时间源**只有** `Main.GlobalTimeWrappedHourly`（秒，浮点）。
4. `Rarities/` 目录内 `HSV|HSL|Hue` **零命中** —— 全部是 RGB 插值，没有色相环绕运算。
5. 每个稀有度的参数**完全硬编码**在各 class 内，没有共享配置层。
   唯一的中央表是 `Rarities/HotPink.cs` 的 `CustomColors` / `CustomRarities` 两个字典。

## 2. 效果模型

### 2.1 四种插值原语（C# 原文）

```csharp
// Utilities/DrawingUtils.cs:621-632 —— 正弦往返，周期 = seconds
public static Color ColorSwap(Color firstColor, Color secondColor, float seconds)
{
    double timeMult = (double)(MathHelper.TwoPi / seconds);
    float colorMePurple = (float)((Math.Sin(timeMult * Main.GlobalTimeWrappedHourly) + 1) * 0.5f);
    return Color.Lerp(firstColor, secondColor, colorMePurple);
}

// Utilities/DrawingUtils.cs:633-646 —— 多色循环
public static Color MulticolorLerp(float increment, params Color[] colors)
{
    increment %= 0.999f;
    int currentColorIndex = (int)(increment * colors.Length);
    Color currentColor = colors[currentColorIndex];
    Color nextColor = colors[(currentColorIndex + 1) % colors.Length];
    return Color.Lerp(currentColor, nextColor, increment * colors.Length % 1f);
}

// Rarities/ColorTool.cs:49-53 —— 按"秒"索引的多色循环
public static Color colorLerps(Color[] colors, float time)
{
    int index = (int)time;
    return Color.Lerp(colors[index % colors.Length], colors[(index + 1) % colors.Length], time % 1f);
}

// Items/Weapons/Magic/Eternity.cs:64-79 —— "每色停留 2 秒"模板（Earth 同构）
public static Color RarityColor()
{
    List<Color> colorSet = [ /* 7 色，见 §4.2 */ ];
    int colorIndex = (int)(Main.GlobalTimeWrappedHourly / 2 % colorSet.Count);
    Color currentColor = colorSet[colorIndex];
    Color nextColor = colorSet[(colorIndex + 1) % colorSet.Count];
    return Color.Lerp(currentColor, nextColor, Main.GlobalTimeWrappedHourly % 2f > 1f ? 1f : Main.GlobalTimeWrappedHourly % 1f);
}
```

### 2.2 逐字符空间相位（本系统的视觉核心）

相位量是**累计像素宽度** `pos.X`，**不是字符索引**：

```csharp
// Rarities/ExoticRainbow.cs:74-85
pos = position;
pos.X += FontAssets.MouseText.Value.MeasureString(txt).X;   // 该字符左边缘相对行首的像素偏移
float rate = Main.GlobalTimeWrappedHourly * (IsExpert ? 2 : 1) + pos.X * (IsExpert ? 0.01f : 0.005f);
int colorIndex = (int)(rate / 2 % eColors.Count);
Color currentColor = eColors[colorIndex];
Color nextColor = eColors[(colorIndex + 1) % eColors.Count];
Color usedColor = Color.Lerp(currentColor, nextColor, rate % 2f > 1f ? 1f : MathF.Round(rate % 1f));
```

```csharp
// Rarities/BurnishedAuric.cs:76-80 —— 逐字符高光扫过
pos = position;
pos.X += FontAssets.MouseText.Value.MeasureString(txt).X;
float sin = (MathF.Sin(pos.X * 0.02f + Main.GlobalTimeWrappedHourly * -1.5f) + 1) * 0.5f;
var c = shineColor * MathF.Pow(sin, 120);
```

**统一形式**：`color(x, t) = palette(t · kt + x · kx)`

| 实现 | 时间系数 `kt` | 位置系数 `kx` |
|---|---|---|
| ExoticRainbow（普通） | `1` | `0.005` |
| ExoticRainbow（Expert） | `2` | `0.01` |
| BurnishedAuric 高光 | `-1.5` | `0.02` |

**只有这 3 处**有空间相位。CosmicPurple / CalamityRed / HotPink / DarkOrange / PureGreen / Turquoise
以及绝大多数逐物品色都是**整行同一个颜色**。

### 2.3 硬切换（易被误认为"平滑"）

`ExoticRainbow` 的插值因子被 `MathF.Round` 量化到 **0 或 1**：

```
rate % 2 ∈ [0, 0.5)  → t = 0（= currentColor）
rate % 2 ∈ [0.5, 2)  → t = 1（= nextColor）
```

即**逐字符硬切换、无过渡**。这与 `ColorSwap` 系列（平滑正弦）是两种不同观感。
本系统默认使用平滑插值，硬切换作为 style 可选参数保留（见 §6）。

> 注意：`ChatTags/CustomColorEffectHandler.cs` 里 XyksBlessing 系列写的是
> `rate % 2f >= 1f ? 1f : rate % 1f`（**平滑**），与 `ExoticRainbow` **不同**，勿混用。

### 2.4 逐字符位置变化（波浪浮动）

**稀有度系统（`Rarities/`）没有逐字符位移**，但 Calamity 其它模块里有，且全仓**只有 3 个逐字符绘制点**：

| 效果 | 位置 | 幅度 | 波长 | 轴向 |
|---|---|---|---|---|
| **Wavy**（对话文字） | `UI/DialogueDisplay/TextEffects/Wavy.cs:29-31` | 6 px | 320 px | 仅 Y |
| **DoGTextSnippet**（聊天 `[ceffect/dog]`） | `ChatTags/CustomColorEffectHandler.cs:136` | ±2 px | 314.16 px | 仅 Y |
| **TiredTail** | `Items/Accessories/Wings/TiredTail.cs:456` | Y ±2 / X ±2 px / 0.1 rad | 314.16 px | Y + X + 自转 |

```csharp
// UI/DialogueDisplay/TextEffects/Wavy.cs:9-31
private const float StandardAmp = 6f;
private const float StandardFreq = 1.5f;
private const float StandardOffsetFactor = MathHelper.TwoPi / 320f; //since we use position, this means a full cycle will occur every 320 coordiantes
...
var sineWave = (float)Math.Sin(Main.GlobalTimeWrappedHourly * freq + data.TextPosition.X * indexFactor) * amp;
return pos + Vector2.UnitY * sineWave;
```

```csharp
// ChatTags/CustomColorEffectHandler.cs:136（DoG）
float sin = MathHelper.SmoothStep(0, 1, (MathF.Sin(pos.X * 0.02f + Main.GlobalTimeWrappedHourly * -1.5f) + 1) * 0.5f);
ChatManager.DrawColorCodedString(spriteBatch, FontAssets.MouseText.Value, item.ToString(), pos + new Vector2(0, -2 + sin * 4), Color.Lerp(Color.Cyan, Color.Fuchsia, sin), 0, Vector2.Zero, new Vector2(scale));
```

**统一形式**：`dy = sin(t · freq + x · (2π / wavelength)) · amplitude`，相位来自字符自身的 X 坐标（不是字符索引）。

DoG 的**颜色波与位置波同相位**（共用同一个 `sin`），这是它"能量流动"观感的来源。

**不存在逐字符缩放动画**：`TextEffect` 基类全仓只有 `Wavy` / `Shaking` 两个子类，都只重写 `ModifyPos`，无一重写 `ModifyScale` / `ModifyRot`。

## 3. 表 A：8 个 `ModRarity` 基色

`grep 'class \w+ : ModRarity'` 全仓库命中恰好 8 个，全部在 `Rarities/` 下。

| # | style id | 名称 | 文字实绘色 `TextClr` | 显示色 `RarityColor` | 定义处 |
|---|---|---|---|---|---|
| 1 | `turquoise` | Turquoise 绿松石 | — | `#00FFC8` | `Rarities/Turquoise.cs:10` |
| 2 | `pure_green` | PureGreen 纯绿 | — | `#00FF00` | `Rarities/PureGreen.cs:10` |
| 3 | `cosmic_purple` | CosmicPurple 宇宙紫 | `#67428A` | `#CE84FF`（= `#67428A` ×2 钳位） | `Rarities/CosmicPurple.cs:19,23` |
| 4 | `burnished_auric` | BurnishedAuric 抛光金 | `#9D6E0B` | `#FFDC16`（= `#9D6E0B` ×2 钳位） | `Rarities/BurnishedAuric.cs:19,23` |
| 5 | `hot_pink` | HotPink 亮粉 | `#FF00FF` | `#FF00FF`（**无 ×2**） | `Rarities/HotPink.cs:27,28` |
| 6 | `calamity_red` | CalamityRed 灾厄红 | `#F21B1B` | `#FF3636`（= `#F21B1B` ×2 钳位） | `Rarities/CalamityRed.cs:17,21` |
| 7 | `exotic_rainbow` | ExoticRainbow 异域彩虹 | `#F21B1B` | 基色 `#FF3636`，内部循环见 §3.1 | `Rarities/ExoticRainbow.cs:20,43-49` |
| 8 | `dark_orange` | DarkOrange（Wiki: Draedon's Arsenal） | — | `#CC4723` | `Rarities/DarkOrange.cs:11` |

源码原文：

```csharp
public override Color RarityColor => new Color(0, 255, 200);          // Turquoise
public override Color RarityColor => new Color(0, 255, 0);            // PureGreen
public override Color RarityColor => TextClr * 2f;                    // CosmicPurple / BurnishedAuric / CalamityRed
public override Color RarityColor => TextColor;                       // HotPink
public override Color RarityColor => new Color(204, 71, 35);          // DarkOrange
public static Color TextClr = new Color(103, 66, 138, 255);           // CosmicPurple
public static Color TextClr = new Color(157, 110, 11, 255);           // BurnishedAuric
public static Color TextClr = new Color(242, 27, 27, 255);            // CalamityRed / ExoticRainbow
```

### 3.1 `ExoticRainbow` 的两套调色板

**普通（非 Expert）—— 三色循环，源码顺序即 Ares → Thanatos → Apollo：**

| 序 | 注释名 | RGB | hex |
|---|---|---|---|
| 1 | Ares | `(255,107,107)` | `#FF6B6B` |
| 2 | Thanatos | `(125,196,225)` | `#7DC4E1` |
| 3 | Apollo | `(211,235,108)` | `#D3EB6C` |
| — | Artemis | `(255,160,71)` | `#FFA047`（源码中**被注释掉**，不启用） |

**Expert（`Item.expert == true`）—— 六色循环，且速度 ×2、位置系数 ×2：**

| 序 | RGB | hex |
|---|---|---|
| 1 | `(255,70,70)` | `#FF4646` |
| 2 | `(255,70,255)` | `#FF46FF` |
| 3 | `(70,70,255)` | `#4646FF` |
| 4 | `(70,255,255)` | `#46FFFF` |
| 5 | `(70,255,90)` | `#46FF5A` |
| 6 | `(255,255,70)` | `#FFFF46` |

### 3.2 `× 2f` 的语义（实现必须照抄）

`MonoGame Color * float` = **逐通道相乘并饱和钳位到 255**，不是取模回绕。

| 运算 | 逐通道 ×2 | 钳位结果 | 与 Wiki 对照 |
|---|---|---|---|
| `(103,66,138)` | `(206,132,276)` | `#CE84FF` | ✅ 吻合 |
| `(157,110,11)` | `(314,220,22)` | `#FFDC16` | ✅ 吻合 |
| `(242,27,27)` | `(484,54,54)` | `#FF3636` | ✅ 吻合 |

> 若误实现为取模 256，`242×2=484 → 228`，色值会明显变暗。**这是必须守住的语义。**
> 注：该结论由 Wiki 数值反证得出（3 组独立吻合），FNA 源码未直接核对。

## 4. 表 B：逐物品自定义色（`HotPink.CustomColors`）

集中定义文件 = `Rarities/HotPink.cs:34-61`，是**唯一**的「物品 → 自定义稀有度色」总表。
共 22 条目，其中 `Ozzathoth` 复用 `Shattered Community` 的色定义，故 22 个可用条目。

| # | style id | 物品名 | 模式 | 色标（源码顺序） | hex | 出处 |
|---|---|---|---|---|---|---|
| 1 | `angelic_alliance` | Angelic Alliance | MultiLerp，周期 2s | 3 色 | `#FFC437` `#FFE76B` `#FFFEF3` | `Items/Accessories/AngelicAlliance.cs:69` |
| 2 | `contagion` | Contagion | 静态 | 1 色 | `#CF1175` | `Items/Weapons/Ranged/Contagion.cs:57` |
| 3 | `crystyl_crusher` | Crystyl Crusher | 静态 | 1 色 | `#811D95` | `Items/Tools/CrystylCrusher.cs:148` |
| 4 | `dance_of_light` | The Dance of Light | 动态（`Main.DiscoG`） | — | `#FF7FFF`…`#FFD8FF` | `Items/Weapons/Magic/TheDanceofLight.cs:27,28` |
| 5 | `demonshade` | Demonshade Helm/Breastplate/Greaves | SinSwap 4s | 2 色 | `#FF8416` ↔ `#DD5507` | `Items/Armor/Demonshade/DemonshadeHelm.cs:84` |
| 6 | `draconic_destruction` | Draconic Destruction | SinSwap 4s | 2 色 | `#FF4500` ↔ `#8B0000` | `Items/Weapons/Melee/DraconicDestruction.cs:66` |
| 7 | `earth` | Earth | Ticked（每色 2s） | 3 色 | `#FF4500` `#48D1CC` `#32CD32` | `Items/Weapons/Melee/Earth.cs:80-87` |
| 8 | `endogenesis` | Endogenesis | SinSwap 4s | 2 色 | `#83EFFF` ↔ `#2437E6` | `Items/Weapons/Summon/Endogenesis.cs:105` |
| 9 | `eternity` | Eternity | Ticked（每色 2s） | 7 色 | `#BCC0C1` `#9D64B7` `#F9A64D` `#FF69EA` `#43CCDB` `#F9F563` `#ECA8F7` | `Items/Weapons/Magic/Eternity.cs:64-79` |
| 10 | `flamsteed_ring` | Flamsteed Ring | Piecewise 0.6/0.2/0.2 | 2 色 | `#59E5FF` ↔ `#FFFFFF` | `Items/Weapons/Summon/FlamsteedRing.cs:208-216` |
| 11 | `illustrious_knives` | Illustrious Knives | SinSwap 4s | 2 色 | `#9AFF97` ↔ `#E497FF` | `Items/Weapons/Melee/IllustriousKnives.cs:66` |
| 12 | `nanoblack_reaper` | Nanoblack Reaper | 动态（`Main.DiscoG`） | — | `#565656`…`#56FFD6` | `Items/Weapons/Rogue/NanoblackReaper.cs:178` |
| 13 | `profaned_soul_crystal` | Profaned Soul Crystal | SinSwap **6s** | 2 色 | `#FFA600` ↔ `#19FA19` | `Items/Accessories/ProfanedSoulCrystal.cs:641` |
| 14 | `red_sun` | Red Sun | SinSwap 4s | 2 色 | `#CC5650` ↔ `#ED458D` | `Items/Weapons/Melee/RedSun.cs:121` |
| 15 | `scarlet_devil` | Scarlet Devil | SinSwap 4s | 2 色 | `#BF2D47` ↔ `#B9BBFD` | `Items/Weapons/Rogue/ScarletDevil.cs:57` |
| 16 | `shattered_community` | Shattered Community | SinSwap **3s** | 2 色 | `#803E80` ↔ `#F569F5` | `Items/Accessories/ShatteredCommunity.cs:28,29,70` |
| 17 | `ozzathoth` | Ozzathoth | 同 #16 | 2 色 | 同 `shattered_community` | `Rarities/HotPink.cs:50` |
| 18 | `soma_prime` | Soma Prime | SinSwap 4s | 2 色 | `#FFFFFF` ↔ `#D1CC6F` | `Items/Weapons/Ranged/SomaPrime.cs:80` |
| 19 | `staff_of_blushie` | Staff of Blushie | 静态 | 1 色 | `#0000FF` | `Items/Weapons/Magic/StaffofBlushie.cs:49` |
| 20 | `svantechnical` | Svantechnical | 静态 | 1 色 | `#DC143C` | `Items/Weapons/Ranged/Svantechnical.cs:86` |
| 21 | `sylvestaff` | Sylvestaff | 静态 | 1 色 | `#F9C5FF` | `Items/Weapons/Magic/Sylvestaff.cs:115` |
| 22 | `temporal_umbrella` | Temporal Umbrella | SinSwap 4s | 2 色 | `#D200FF` ↔ `#FFF818` | `Items/Weapons/Summon/TemporalUmbrella.cs:69` |
| 23 | `triactis_hammer` | Triactis' True Paladinian Mage-Hammer of Might | 静态 | 1 色 | `#E3E2B4` | `Items/Weapons/Melee/TriactisTruePaladinianMageHammerofMight.cs:49` |

> **未收录的条目**：`dance_of_light`(#4) 与 `nanoblack_reaper`(#12) 依赖原版 `Main.DiscoG`（周期与算法未确认），
> `DevItemColor`(`#FF00FF`) 与 `hot_pink` 取值重复，三者均**暂不提供 style id**。
> 反向差异：`exotic_rainbow_expert` 是本系统新增的 id——§3.1 只把它描述为 `exotic_rainbow` 在 `Item.expert` 时的调色板变体，
> 拆成独立 id 是为了让玩家可直接选择。

### 4.1 `Flamsteed Ring` 的分段时序（原文）

```csharp
// Items/Weapons/Summon/FlamsteedRing.cs:208-216
public static Color RarityColor()
{
    if (Main.GlobalTimeWrappedHourly % 1f < 0.6f)
        return new Color(89, 229, 255);
    else if (Main.GlobalTimeWrappedHourly % 1f < 0.8f)
        return Color.Lerp(new Color(89, 229, 255), Color.White, (Main.GlobalTimeWrappedHourly % 1f - 0.6f) / 0.2f);
    else
        return Color.Lerp(Color.White, new Color(89, 229, 255), (Main.GlobalTimeWrappedHourly % 1f - 0.8f) / 0.2f);
}
```

即 1 秒周期内：`0.0–0.6s` 纯青蓝 → `0.6–0.8s` 渐变到白 → `0.8–1.0s` 渐变回青蓝。**不是正弦往返**。

### 4.2 动态色（无静态 hex，依赖原版 `Main.DiscoG`）

| style id | 公式 |
|---|---|
| `dance_of_light` | `new Color(1f, 0.5f + 0.35f * DiscoG/255f, 1f)` → `#FF7FFF`…`#FFD8FF` |
| `nanoblack_reaper` | `new Color(0.34f, 0.34f + 0.66f * DiscoG/255f, 0.34f + 0.5f * DiscoG/255f)` → `#565656`…`#56FFD6` |

QmClient 落地：用本地时间以原版 Rainbow 的周期驱动等价变量，或直接降级为静态代表色（待定，见 §7）。

## 5. 表 C：开发者 / 捐赠者常量

| 名称 | 用途 | 色值 | 出处 |
|---|---|---|---|
| `DevItemColor` | 开发者物品 tooltip 标记行 | `#FF00FF` | `Utilities/ItemUtils.cs:43` |
| `DonatorItemColor` | 捐赠者物品标记行 + 附魔描述 | `#FF799C` | `Utilities/ItemUtils.cs:44` |

```csharp
internal static readonly Color DevItemColor = new Color(255, 0, 255);
internal static readonly Color DonatorItemColor = new Color(255, 121, 156);
```

## 6. 绘制层手法（非颜色部分）

> **实现阶段**：本节内容**尚未实现**，属于 P4（绘制层复刻）的范围。目前落地的只有颜色层（§2、§3、§4）与逐字符浮动（§9.2）。
> 这些是 Calamity 观感的重要组成。QmClient 侧默认档位会收敛（见 §6.4）。

### 6.1 描边：8 方向 × 2px 偏移

RT 方案（CosmicPurple / BurnishedAuric / ExoticRainbow）：

```csharp
// Rarities/ExoticRainbow.cs:102-108
for (float f = 0f; f < MathHelper.TwoPi; f += MathHelper.TwoPi * 0.125f)
{
    spriteBatch.Draw(lease.Target, Vector2.Zero + new Vector2(2, 0).RotatedBy(f), null, Color.Black, 0f, Vector2.Zero, 1f, SpriteEffects.None, 0f);
}
spriteBatch.Draw(lease.Target, Vector2.Zero, null, Color.White, 0f, Vector2.Zero, 1f, SpriteEffects.None, 0f);
```

- 方向数：8（步长 `TwoPi/8`），偏移半径固定 2px。
- ExoticRainbow：纯黑描边 + 白主体；BurnishedAuric：`borderColor = color * 2f` + 核心色 `new Color(77, 0, 33)`。

### 6.2 加法混合外发光（手工 bloom，非引擎 Bloom）

```csharp
// Rarities/ExoticRainbow.cs:90-100
Main.spriteBatch.Begin(SpriteSortMode.Immediate, BlendState.Additive, ...);
float sine = (float)Math.Sin(Main.GlobalTimeWrappedHourly * 2 / MathHelper.Pi);
sine = (float)Math.Pow(MathHelper.Lerp(sine, 0, 0.35f), 5);
int draws = 16;
for (int i = 0; i < draws; i++)
{
    Vector2 backPosition = (MathHelper.TwoPi * i / (float)draws + Main.GlobalTimeWrappedHourly * 1.7f).ToRotationVector2() * (4 + 16 * sine);
    spriteBatch.Draw(lease.Target, Vector2.Zero + backPosition, null, Color.White, 0f, Vector2.Zero, 1f, SpriteEffects.None, 0f);
}
```

- 16 份沿圆周均布，整圈以 `t * 1.7` 旋转，半径 `4 + 16 · sin⁵` 呼吸。

### 6.3 其他稀有度的绘制层

| 稀有度 | 手法（原文行号） |
|---|---|
| CosmicPurple | 8 份随 `t*2` 旋转的偏移副本（`pulsing = 2.5 + sin(t*5)`）+ 原版 8 方向阴影（`color * 2f`）+ 纯黑实心文字；另有 `CrystalTextGlow` 背光与 `CrystalTextSparkle` |
| CalamityRed | 8 份火焰层（`-(|sin(angle)| · 5)` 向上偏移 + `distortion` 水平抖动 + `scaleVariation = 0.95 + 0.05·sin(t*15 + f*2)`）+ 1.03 倍清晰主体 + 阴影（`Lerp(color, White, 0.67)`）+ 纯黑核心 |
| BurnishedAuric | 逐字符高光层 `shineColor * sin^120`（极窄亮带扫过）+ 随机闪蓝状态机（`flashChance = 0.005f`/帧，`flashDuration = 0.2f` s，用 `Main.GameUpdateCount` 驱动） |
| TiredTail | 逐字符绘制，`txt.Length % 4 == 3` 决定 HotPink，含旋转 |

`BloomClr` 常量（**A 通道全为 0**）：`CosmicPurple` `(65,38,87,0)`、`BurnishedAuric` `(48,33,4,0)`、
`CalamityRed`/`ExoticRainbow` `(180,20,75,0)`、`HotPink` `Color.White`。

**没有引擎 Bloom、没有 Dust 粒子系统** —— 全部是手写加法混合 + 贴图。

### 6.4 `CrystalTextGlow` / `CrystalTextSparkle` 贴图

两张 `.xnb` 二进制贴图**不在源码内**，尺寸与像素内容未确认。QmClient 不引用 Calamity 素材，
改为**程序化生成等效背光**（径向渐变拉长光晕），视觉接近但非像素级相同。

### 6.5 整行（非逐字符）效果全表

P4 绘制层复刻的依据；副本份数与位移公式逐字来自源码。

| 效果 | 位置 | 副本 | 位移公式 | 幅度 / 频率 |
|---|---|---|---|---|
| CosmicPurple 名 | `Rarities/CosmicPurple.cs:41-45` | 8 | `rot(f + (2t mod 2π)) · pulsing` | `pulsing = 2.5 + sin(5t)` ∈ [1.5, 3.5] |
| CalamityRed 名 | `Rarities/CalamityRed.cs:46-83` | 8 + 1 | `(cos(a)·pulsing·0.4 + dist, −abs(sin(a))·5)` | X ±[3.6,4.4]±2；**Y ∈ [−5,0] 仅向上**；`pulsing = 10 + sin(20t)` |
| ExoticRainbow 名 | `Rarities/ExoticRainbow.cs:91-98` | 16 | `rot(2πi/16 + 1.7t) · (4 + 16·sine)` | `sine = (0.65·sin(2t/π))⁵` → 半径 ∈ [2.1435, 5.8565] |
| BurnishedAuric 名 | `Rarities/BurnishedAuric.cs:45` | 1 | `pos += NextVector2Circular(8, 4.8)` | 椭圆 (8, 4.8)，每帧白噪声，闪烁期约 12~13 帧 |
| 聊天 darksun | `ChatTags/CustomColorEffectHandler.cs:57-60` | 20 | `rot(f + t) · 2` | 固定 2，环 1.0 rad/s |
| 聊天 drunk | `ChatTags/CustomColorEffectHandler.cs:84-98` | 3 | `(1.7cos(a), 1.0sin(a)) · radius` | `radius = (sin(t+1)/2)·(3+6i)` |
| 附魔物品名 | `Items/CalamityGlobalItemTooltip.cs:1556-1566` | 2 + 1 | `pos − (1, 0.1)·k·10` | 最大 (−10, −1)；`k = (0.81t mod 1)^1.5`，周期 1.2346 s；缩放 1→1.2 |
| XyksBlessing 名 | `...Tooltip.cs:1452-1458` / `1511-1517` | 20 | `rot(2πi/20) · 3.5` | 固定 3.5，**静态环** |
| OD Tooltip5 抖动 | `...Tooltip.cs:1606-1613` | 4 | `pos + NextVector2Circular(5, 5)` | 每帧白噪声 |
| OD Tooltip2/5 环 | `...Tooltip.cs:1615-1624` | 20 | `rot(2πi/20) · (1.5 + 0.2·sine)` | `sine = sin(5t/π)`，1.5915 rad/s |
| OD Tooltip4 双环 | `...Tooltip.cs:1628-1643` | 20 + 20 | `· (4.5 ± 0.2)` 与 `· (2.5 ± 0.2)` | 1.5915 rad/s |
| OD Tooltip7 环 | `...Tooltip.cs:1658-1662` | 20 | `rot(2πi/20) · 1.5` | 固定 1.5，**静态** |
| Boss 血条名（狂暴 / 防御） | `UI/BossHealthBarManager.cs:698-706` / `713-721` | 4 | 沿 0/90/180/270° 外扩 | `outwardness = 进度/120·1.5 + pulse·2` ∈ (2, 3.5]；`pulse = (sin(4.5t)+1)/2` |

**Boss Rush、状态提示（108 处走原版 `Main.NewText`）、其余全部 UI 文字均为静态**（逐项核验过）。

## 7. 已知陷阱与死代码

| 项 | 说明 |
|---|---|
| **硬切换** | `ExoticRainbow` 的 `MathF.Round` 量化是源码事实，但"平滑"是本系统默认档（§2.3） |
| **死代码** | `CalamityRed.cs:100` 的 `bloomColor = ColorTool.Rainbowing(time*4 - 0.9f)` 算出后**从未使用**；`CrystalTextSparkle` 请求后**从未绘制**。**不要照抄** |
| **Wiki 与源码冲突** | `Earth`：Wiki 写 `#FF6392/#FFE45E/#7FC8F8`，源码是 `#FF4500/#48D1CC/#32CD32`；`Nanoblack Reaper`：Wiki 写 `#57FFFF`，源码上限 `#56FFD6`。**一律以源码为准** |
| **不存在的东西** | 没有 `Tier 1`~`Tier 20` 命名稀有度，没有 `Tier 18` 这个标识符；"18" 只是 Wiki 编号（对应 `ExoticRainbow`） |
| 死代码（次要） | `CosmicPurple.cs:103` 的 `if (lifeTime > MathHelper.TwoPi) continue;` 恒为 false |
| **BurnishedAuric 位置不动** | `BurnishedAuric.cs:78` 的相位公式与 DoG / TiredTail **逐字相同**（`sin(x*0.02 - 1.5t)`），但它只喂 `MathF.Pow(sin, 120)` 做**高光扫过**，`pos` 全程等于基线。照抄成位移就错了 |
| **「8 向描边」实际是 9 份** | `f += TwoPi * 0.125f` 的循环因 float32 累加误差多执行一次：`0.7853981852531433 × 8 = 6.283185005187988` < `TwoPi(float32) = 6.2831854820251465`，第 9 次（`f ≈ 2π ≡ 0`）仍执行，两份视觉重合（加法混合下略亮）。受影响：`ExoticRainbow.cs:103`、`BurnishedAuric.cs:62`。对照：`f += 0.79f` → 8 份、`TwoPi*0.05f` → 20 份，均正确 |
| **CosmicPurple 运算符优先级** | `f + Main.GlobalTimeWrappedHourly * 2f % MathHelper.TwoPi` 解析为 `f + ((t*2) % 2π)`，即整圈以 **2 rad/s** 旋转并对 2π 取模 |
| **tooltip 行旋转对稀有度文字无效** | 所有 `Rarities/*.Draw` 都透传 `line.Rotation` / `line.Origin`，但这两个参数只用于背景光晕贴图；文字本体一律 `rotation = 0, origin = Vector2.Zero`。C++ 复刻可省掉这条路径 |

## 8. 未确认项（实现时不得当作已知事实）

1. `Color * 2f` 的钳位语义：由 Wiki 3 组数值反证，**未直接核对 FNA 源码**。
2. `Main.GlobalTimeWrappedHourly` 的精确更新与回绕规则（是否含暂停、回绕周期）未核对原版源码。
3. `Main.DiscoColor` / `Main.DiscoG` 的周期与生成算法未核对。
4. `BloomClr` 的 A=0 在不同 `BlendState` 下的实际观感未实测。
5. `CrystalTextGlow` / `CrystalTextSparkle` 的尺寸与像素内容未确认。
6. `Main.GlobalTimeWrappedHourly` 是否每 3600 秒回绕**未证实**（Terraria 本体不开源）。所有公式的结构、幅度与频率都是源码逐字确认，仅"t 是否跳变"这一点未证实；含显式取模的两处（`CosmicPurple` 的 `(2t) % 2π`、附魔的 `(0.81t) % 1`）不受影响。
7. `Main.rand.NextVector2Circular(a, b)` 的精确分布属外部 API 推断（按"椭圆内面积均匀"表述）。受影响：BurnishedAuric 抖动、OD Tooltip5 抖动、对话 `Shaking`。

## 9. QmClient 落地映射

| 概念 | 落地 |
|---|---|
| style id 来源 | 服务端下发的 `style` 字段（字符串），客户端内置 id → 参数表 |
| 时间基准 | 服务端 `server_time` 偏移 + `Client()->GlobalTime()`（保证所有人看到一致） |
| 空间相位 | 逐字符累计像素 X（`TextRender()->TextWidth()` 前缀测量），对齐 `pos.X` |
| 逐字符上色 | `Cursor.m_vColorSplits`（现成机制，逐字符纯色） |
| 字符内渐变 | `STextColorSplit` 扩展结束色 → 顶点左右异色（引擎 opt-in 改动） |
| 默认档位 | 平滑插值（硬切换为可选参数）；描边/bloom 默认收敛，能力全部实现 |

### 9.1 本规格之外的合法扩展

以下是为 QmClient 增加的参数，**Calamity 源码中不存在**，默认值必须保持"等于 Calamity 行为"：

| 参数 | 默认 | 说明 |
|---|---|---|
| `interpolation` | `smooth` | `hard` 时完全复刻 `ExoticRainbow` 的 `MathF.Round` 量化 |
| `phase_scale` | `1.0` | 缩放 `kx`；`0` = 关闭空间相位（整行同步） |
| `bloom_level` | 待定 | 0 = 关闭，1 = 收敛，2 = 完全复刻 16 份 |
| `pulse_level` | 待定 | 亮度呼吸幅度 |
| `bob_amplitude` | `2` | 逐字符垂直浮动幅度（像素）；`0` = 关闭。Calamity Wavy 原值为 `6`，头衔字号更小，默认取 `2` 更克制 |
| `bob_wavelength` | `320` | 浮动波长（像素），采用 Wavy 原值 |
| `bob_speed` | `1.5` | 相位角速度（**弧度/秒**，与 Wavy 的 `freq` 同单位），采用 Wavy 原值 |
| `bob_pixel_snap` | `0` | `1` = 吸附整数像素（轮廓更锐利，但小幅度下会长时间停在同一像素再跳变，观感像掉帧）；`0` = 亚像素 |

### 9.2 逐字符波浪浮动（与 Calamity Wavy 同构）

**Calamity 的稀有度配色没有逐字符浮动**（`Rarities/` 内的位置变化全是"整行画多份偏移副本"），
但 Calamity 的**对话文字效果 Wavy** 正是逐字符垂直波，本实现与其同构（见 §2.4）。

实现要点：

| 项 | 值 |
|---|---|
| 相位 | 按**像素 X** 递进（与颜色相位同源），而非字符索引，保证中英文混排波纹均匀 |
| 公式 | `dy = round(A · sin(t · freq + x · (2π / λ)))`，`freq` 即 `m_Speed`（弧度/秒） |
| Calamity 原值 | `A = 6`、`λ = 320`、`freq = 1.5`（可直接照抄） |
| 量化 | 默认**亚像素**（步长 0），位移随帧连续变化。`qm_title_bob_pixel_snap` 可切到整数像素吸附——注意整数像素在 2px 幅度、1.5 rad/s 下每帧仅变化约 0.05px，要约 20 帧才跳 1 像素，观感像掉帧，因此不作为默认 |
| 作用域 | 只改渲染顶点，**不参与布局、断行、选区与光标计算**，文字宽度与对齐保持稳定 |
| 布局预留 | 调用方必须按 `A` 预留垂直 padding，否则浮动到顶会被相邻元素裁掉 |

### 9.3 已知限制与实现约定

| 项 | 说明 |
|---|---|
| 时间基准必须先 wrap | `QmTitleStyleSample` 的 `TimeSec` 是 `float`。直接传 epoch / `server_time` 量级的大数会因 float ULP 抹平相位（1e9 时 ULP ≈ 64 秒），必须先减去基准并取模 |
| 非缓冲路径丢失顶点色 | `IsTextBufferingEnabled()` 在 GL < 3.3 / GLES / null 后端为 false，此时走逐 quad 立即模式，描边与填充都被刷成单一平色。**字符内横向渐变在这些后端静默退化为左端纯色**，属能力降级而非回归 |
| `Color.a != 0` 门控只看左端色 | 容器生成时以左端色 alpha 决定是否产出顶点。因此"左端全透明、右端不透明"的淡入会让整个字符不绘制；需要淡入时应让两端 alpha 同号 |
| 通道精度 | 顶点色为 8 位/通道，端点色可精确往返，中间值可能与 Calamity 的字节空间插值差 ±1 LSB。故"逐字节一致"应表述为"8 位量化后一致" |
| `PhaseCycle` 的周期 | 源码中该模板的周期与阈值硬编码为 2 秒 / 1 秒，实现按半个周期泛化；但**现有三条表项的 `m_PeriodSec` 必须保持 `2.0`**，改成其它值会悄悄改变语义 |

### 9.4 相位基准对齐（P6）

动画相位必须所有客户端一致，否则同一时刻不同人看到的颜色会错开。做法：

```
offset = EWMA(server_time − Client()->GlobalTime())      // 每次拉取 presences 时更新
phase  = fmod(Client()->GlobalTime() + offset, 3600)      // 取模后交给采样器
```

- **必须取模**：相位以 `float` 传给采样器，Unix 时间戳量级（1e9）在 float 下 ULP ≈ 64 秒，相位会完全失效。取模到 3600 秒后 ULP ≈ 2.4e-4 秒。
- **取模周期必须是所有风格周期的公倍数**：现有周期为 1 / 2 / 3 / 4 / 6 秒，3600 全部整除，因此边界处不会跳变（有单元测试守护这条不变量）。
- **偏移做指数平滑**（α = 0.2）：单次估算受网络延迟与调度抖动影响有几十毫秒起伏，直接采用会让相位一跳一跳。
- 服务端不可用时（无 `server_time`）退回本地时间：动画仍可用，只是不再跨客户端对齐。

## 10. 变更记录

| 日期 | 变更 |
|---|---|
| 2026-09-13 | 初版：固化 mod 2.2.2 基准的 8 个 ModRarity、ExoticRainbow 双调色板、22 条逐物品色、4 个插值原语、绘制层手法与陷阱清单 |
| 2026-09-13 | 补充逐字符位置变换穷举（Wavy / DoG / TiredTail 三个绘制点）、整行效果全表（§6.5）、4 条新陷阱与 2 条未确认项；浮动参数对齐 Wavy 原值（A=6 / λ=320 / freq=1.5） |
