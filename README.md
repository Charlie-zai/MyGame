# 🎮 SIDEQUEST — 小型 WinUI 3 平台射击游戏
## 📖 概述
独立的关卡选择页面提供可滚动的十个关卡列表。教程为可选内容，可以随时重玩。选中任意关卡时，关卡会完全重置、从头开始。

注意：这个项目使用全程使用GBK编码（此文档除外），如果遇到乱码可以尝试改变编码。

## 🎮 操作说明
| 按键/输入 | 功能 |
|---|---|
| A / 左方向键 | 向左移动 |
| D / 右方向键 | 向右移动 |
| W / 上方向键 / 空格 | 跳跃 |
| 贴靠墙壁 + W / 空格 | 攀爬垂直墙面 |
| 鼠标 | 瞄准 |
| 鼠标左键 | 发射当前选中武器 |
| 1 | 选择手枪（每发5点伤害；弹匣20发） |
| 2 | 选择榴弹发射器（单发，3秒装填冷却） |
| 3 | 选择轻机枪（6点伤害；50发弹匣，10秒装填冷却） |
| R | 对手枪或轻机枪手动换弹 |
| 鼠标滚轮 | 在已解锁武器之间循环切换 |
| Esc | 暂停 / 恢复游戏 |

## ⚔️ 游戏机制
玩家拥有40点生命值。普通敌人拥有20点生命值，接触造成5点伤害；装甲单位**仅受到爆炸伤害**。轰炸机与枪手会使用不同的远程攻击。
击败每名敌人会掉落一枚手榴弹。手榴弹受重力呈抛物线飞行，爆炸半径256单位，伤害会随距离衰减。

第5关与第10关为Boss关卡，拥有独特弹幕模式与血条。击败关卡Boss即可开启出口。Boss攻击、敌方子弹、炸弹、换弹动画、血条均有动画表现。

前四关拥有连贯的全新生物群系流程：
1. 第1关：路线较长、地形起伏，出口处驻守五名敌人。所有垂直平台墙体均可贴墙+W/空格攀爬。
2. 第2关：一面高耸可攀爬墙体，持续遭受炮火轰击；抵达顶端可拾取轻机枪。
3. 第3关：场景从普通地表过渡到雪地，会周期性投放伞兵敌人到关卡路径上。
4. 第4关：陡峭坚实雪山，可见岩石与矿脉，五处被敌人占据的平台落脚点。按住D+W/空格可以攀爬雪山任意覆雪坡面；从山上坠落会传送回上一个到达过的落脚点。雪山最后区域为暴风雪环境，每3秒受到1点伤害。

除教程外，所有关卡开局自带1枚手榴弹。

敌人会在连通平台间追击玩家，会规避无法完成的跳跃；部分敌人受伤、或预判即将中弹时会向后撤退。
雪地行者每3秒投掷一枚3伤害雪球；山地火炮投掷7伤害雪弹。
Boss地面重击前会给出预警提示；需要跳跃躲开冲击波。差点击中Boss会使其进入狂暴，触发更快的重击。

接触出口门即可开门；敌人和Boss不会锁住出口。门会做透视开门动画，玩家走过后，关卡选择页面滑入界面。
选择关卡时，选择页向左滑入，展示游戏内容。
角色战败后，会淡入重试/关卡选择界面；重试会将失败卡片向下滑走，之后角色重生。
游戏内按钮可以暂停、重置当前关卡、返回关卡选择页。关卡选择页面与游戏画面独立滚动，页面内的退出按钮可以关闭游戏。

## 🔨 构建运行
在 Visual Studio 打开 `MyGame.slnx`，编译 x64 Debug 配置。
项目基于 C++/WinRT、Windows App SDK、Win2D。WinUI 负责菜单与HUD界面；Win2D 的 CanvasControl 渲染游戏动画场景。

---

# 🎮 SIDEQUEST - a small WinUI 3 platform shooter
## 📖 Overview
The standalone chapter‑selection page provides a scrollable list of ten stages. The tutorial is optional and can be replayed at any time. Every stage starts fresh when selected.

Note: **This project uses GBK encoding throughout (except for this document). Try changing the encoding if you encounter garbled text.**

## 🎮 Controls
| Key / Input | Function |
|---|---|
| A / Left arrow | Move left |
| D / Right arrow | Move right |
| W / Up / Space | Jump |
| Hold toward a wall + W / Space | Climb vertical walls |
| Mouse | Aim |
| Left mouse | Fire the selected weapon |
| 1 | Select pistol (5 damage per shot; 20 rounds per magazine) |
| 2 | Select grenade launcher (one shot, 3‑second reload) |
| 3 | Select light machine gun (6 damage; 50 rounds, 10‑second reload) |
| R | Manually reload the selected pistol or light machine gun |
| Mouse wheel | Cycle through unlocked weapons |
| Escape | Pause / resume |

## ⚔️ Game Mechanics
The player has 40 health. Regular enemies have 20 health and deal 5 contact damage; armored units only take explosive damage. Bombers and gunners use different ranged attacks.
Each defeated enemy drops one grenade. Grenades arc under gravity, have a 256‑unit blast radius, and deal damage that falls off with distance.

Stages 5 and 10 feature bosses with distinct projectile patterns and health bars. Defeat a stage boss to unlock its exit. Boss attacks, enemy shots, bombs, reloads, and health bars are animated.

The first four stages have a new connected biome sequence:
1. Stage 1 is a longer, uneven route guarded by five enemies at the exit. All vertical platform walls can be climbed by holding toward the wall and pressing W/Space.
2. Stage 2 has a tall climbable wall under bombardment; reach its top to collect the light machine gun.
3. Stage 3 transitions from the usual ground into snow and periodically drops parachuting enemies onto the route.
4. Stage 4 is a steep, solid snowy mountain with visible rock and mineral seams and five enemy‑held rest ledges. Climb any point on its snow‑covered face with D plus W/Space; falling on the mountain returns you to the last reached ledge. The final mountain stretch is a blizzard that deals 1 damage every three seconds.

Non‑tutorial stages start with one grenade.

Enemies pursue across connected platforms, avoid impossible jumps, and some may retreat when hurt or when a shot is about to hit them.
Snow walkers throw 3‑damage snowballs every three seconds; mountain artillery throws 7‑damage snow bombs.
Bosses telegraph ground slams with a warning; jump over the resulting ground shockwaves. Near misses can enrage a boss and provoke a faster slam.

Every exit opens on contact; enemies and bosses do not lock the door. The door opens in perspective and the player walks through before the chapter‑selection page slides in.
Selecting a stage slides the page left to reveal the game.
Defeat is followed by an animated fade‑in of the retry/selection choices; retrying slides the defeat card down before respawning.
The in‑game buttons pause, reset the current level, or return to selection. The selection page scrolls independently from the game and its Exit button closes the game.

## 🔨 Build
Open `MyGame.slnx` in Visual Studio and build the x64 Debug configuration.
This project uses C++/WinRT, the Windows App SDK, and Win2D. WinUI owns the menus and HUD; Win2D's CanvasControl draws the animated game scene.
