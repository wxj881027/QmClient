# Q1menG Client UI Enhancement Features / UI增强功能

## Overview / 概述

This document describes the new UI enhancement features added to Q1menG Client for creating stunning visual effects.
本文档描述了为Q1menG客户端添加的新UI增强功能，用于创建令人惊艳的视觉效果。

## Menu Background Particles / 菜单背景粒子系统

### Features / 功能特性

The menu background now supports dynamic particle effects to make the UI more visually appealing.
菜单背景现在支持动态粒子效果，使UI更具视觉吸引力。

#### Particle Types / 粒子类型

1. **Star (星星)** - Rotating star-shaped particles with elongated arms
2. **Circle (圆形)** - Simple circular particles
3. **Sparkle (闪光)** - Bright sparkle points with glow effect
4. **Glow (辉光)** - Soft glowing particles with pulsing animation

#### Effect Modes / 效果模式

1. **None (无)** - No special effects, particles use default white color
2. **Rainbow (彩虹)** - Smooth HSV rainbow color transition
3. **Pulse (脉冲)** - Pulsing between blue and cyan colors
4. **Wave (波浪)** - Wave effect with cyan/magenta gradient
5. **Spiral (螺旋)** - Particles spiral outward from center with gradient colors
6. **Meteor (流星)** - Particles fall diagonally like meteors with fiery colors

### Configuration / 配置选项

```
cl_menu_particles [0/1]           - Enable/disable menu particles
                                    启用/禁用菜单粒子效果
                                    Default: 1

cl_menu_particle_effect [0-5]     - Select effect mode
                                    选择效果模式
                                    0 = None, 1 = Rainbow, 2 = Pulse
                                    3 = Wave, 4 = Spiral, 5 = Meteor
                                    Default: 1 (Rainbow)

cl_menu_particle_alpha [0-100]    - Particle opacity
                                    粒子透明度
                                    Default: 60
```

## Menu Transitions / 菜单过渡效果

### Features / 功能特性

Smooth animated transitions when navigating between menu pages.
在菜单页面之间导航时的平滑动画过渡。

### Configuration / 配置选项

```
cl_menu_transitions [0/1]         - Enable/disable menu transitions
                                    启用/禁用菜单过渡动画
                                    Default: 1

cl_menu_transition_speed [50-200] - Transition animation speed (percentage)
                                    过渡动画速度（百分比）
                                    Default: 100
```

## HUD Animations / HUD动画效果

### Features / 功能特性

Smooth animations for HUD elements including:
HUD元素的平滑动画，包括：

- Health and armor value changes / 生命值和护甲值变化
- Weapon switching / 武器切换
- Score updates / 分数更新
- Status effect indicators / 状态效果指示器

### Transition Types / 过渡类型

The UI Effects system supports multiple transition types:
UI效果系统支持多种过渡类型：

1. **Linear (线性)** - Constant speed transition
2. **Ease In (渐入)** - Starts slow, ends fast
3. **Ease Out (渐出)** - Starts fast, ends slow
4. **Ease In Out (渐入渐出)** - Starts and ends slow, fast in middle
5. **Bounce (弹跳)** - Bouncing effect at the end

### Configuration / 配置选项

```
cl_hud_animations [0/1]           - Enable/disable HUD animations
                                    启用/禁用HUD动画
                                    Default: 1

cl_hud_animation_speed [50-200]   - HUD animation speed (percentage)
                                    HUD动画速度（百分比）
                                    Default: 100
```

## Technical Details / 技术细节

### Component Architecture / 组件架构

The UI enhancement system consists of three main components:
UI增强系统由三个主要组件组成：

1. **CMenuParticles** - Handles menu background particle rendering
2. **CUiEffects** - Provides smooth value transitions and animation helpers
3. **Integration** - Integrated into existing components (HUD, menus, etc.)

### Performance / 性能

- Particle system limited to 150 particles maximum for performance
  粒子系统限制为最多150个粒子以保证性能
- Automatic particle cleanup to prevent memory leaks
  自动粒子清理以防止内存泄漏
- Configurable update rates for smooth 60+ FPS
  可配置的更新率以保持60+帧率

## Usage Examples / 使用示例

### Enable Rainbow Particles / 启用彩虹粒子

```
cl_menu_particles 1
cl_menu_particle_effect 1
cl_menu_particle_alpha 70
```

### Enable Meteor Effect / 启用流星效果

```
cl_menu_particles 1
cl_menu_particle_effect 5
cl_menu_particle_alpha 80
```

### Disable All Effects / 禁用所有效果

```
cl_menu_particles 0
cl_menu_transitions 0
cl_hud_animations 0
```

## Future Enhancements / 未来增强

Potential future additions:
潜在的未来添加：

- Custom particle textures / 自定义粒子纹理
- User-configurable color schemes / 用户可配置的配色方案
- More complex particle behaviors / 更复杂的粒子行为
- Sound effects for transitions / 过渡音效
- Customizable animation curves / 可自定义的动画曲线

## Credits / 致谢

These enhancements build upon the excellent foundation of:
这些增强功能建立在以下优秀基础之上：

- DDNet - Core game engine
- TaterClient - Client modifications framework
- Q1menG_Client - Custom client implementation

---

For more information, visit the project repository.
更多信息，请访问项目仓库。
