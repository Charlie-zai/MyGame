SIDEQUEST - a small WinUI 3 platform shooter
============================================

The standalone chapter-selection page provides a scrollable list of ten
stages. The tutorial is optional and can be replayed at any time. Every
stage starts fresh when selected.

Controls
--------
A / Left arrow:  Move left
D / Right arrow: Move right
W / Up / Space:  Jump
Hold toward a wall + W / Space: Climb vertical walls
Mouse:           Aim
Left mouse:      Fire the selected weapon
1:               Select pistol (5 damage per shot; 20 rounds per magazine)
2:               Select grenade launcher (one shot, 3-second reload)
3:               Select light machine gun (6 damage; 50 rounds, 10-second reload)
R:               Manually reload the selected pistol or light machine gun
Mouse wheel:     Cycle through unlocked weapons
Escape:          Pause / resume

The player has 40 health. Regular enemies have 20 health and deal 5 contact
damage; armored units only take explosive damage. Bombers and gunners use
different ranged attacks. Each defeated enemy drops one grenade. Grenades arc
under gravity, have a 256-unit blast radius, and deal damage that falls off
with distance.

Stages 5 and 10 feature bosses with distinct projectile patterns and health
bars. Defeat a stage boss to unlock its exit. Boss attacks, enemy shots, bombs,
reloads, and health bars are animated.

The first four stages have a new connected biome sequence. Stage 1 is a longer,
uneven route guarded by five enemies at the exit. All vertical platform walls
can be climbed by holding toward the wall and pressing W/Space. Stage 2 has a
tall climbable wall under bombardment; reach its top to collect the light
machine gun. Stage 3 transitions from the usual ground into snow and
periodically drops parachuting enemies onto the route. Stage 4 is a steep,
solid snowy mountain with visible rock and mineral seams and five enemy-held
rest ledges. Climb any point on its snow-covered face with D plus W/Space;
falling on the mountain returns you to the last reached ledge. The final
mountain stretch is a blizzard that deals 1 damage every three seconds.
Non-tutorial stages start with one grenade.

Enemies pursue across connected platforms, avoid impossible jumps, and some
may retreat when hurt or when a shot is about to hit them. Snow walkers throw
3-damage snowballs every three seconds; mountain artillery throws 7-damage
snow bombs. Bosses telegraph ground slams with a warning; jump over the
resulting ground shockwaves. Near misses can enrage a boss and provoke a faster
slam.

Every exit opens on contact; enemies and bosses do not lock the door. The door
opens in perspective and the player walks through before the chapter-selection
page slides in. Selecting a stage slides the page left to reveal the game.
Defeat is followed by an animated fade-in of the retry/selection choices;
retrying slides the defeat card down before respawning.
The in-game buttons pause, reset the current level, or return to selection.
The selection page scrolls independently from the game and its Exit button
closes the game.

Build
-----
Open MyGame.slnx in Visual Studio and build the x64 Debug configuration.
This project uses C++/WinRT, the Windows App SDK, and Win2D. WinUI owns the
menus and HUD; Win2D's CanvasControl draws the animated game scene.
