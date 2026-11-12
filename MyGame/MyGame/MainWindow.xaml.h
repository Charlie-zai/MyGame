#pragma once

#include "MainWindow.g.h"
#include <chrono>
#include <random>
#include <string>
#include <vector>

namespace winrt::MyGame::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

    private:
        enum class EnemyKind
        {
            Walker,
            Armored,
            Bomber,
            Gunner,
            Parachuter,
            SnowWalker,
            SnowArtillery,
            MiniBoss,
            FinalBoss
        };

        enum class ProjectileKind
        {
            Pistol,
            Grenade,
            MachineGun,
            EnemyBullet,
            EnemyBomb,
            Snowball,
            SnowBomb,
            BossOrb,
            Shockwave
        };

        enum class WeaponKind
        {
            Pistol,
            Grenade,
            MachineGun
        };

        enum class EndSequence
        {
            None,
            Death,
            DeathRestart,
            Victory,
            Selection,
            SelectionExit
        };

        struct Platform
        {
            float x;
            float y;
            float width;
            float height;
        };

        struct ClimbRoute
        {
            float startX;
            float startY;
            float endX;
            float endY;
            float solidX;
        };

        struct MountainPoint
        {
            float x;
            float y;
        };

        struct Enemy
        {
            float x;
            float y;
            float speed;
            float minX;
            float maxX;
            float health;
            float maxHealth;
            float displayedHealth;
            float hitFlash;
            float phase;
            float attackCooldown;
            float verticalVelocity;
            float anger;
            float stompWindup;
            float damageCooldown;
            size_t platformIndex;
            EnemyKind kind;
            bool grounded;
            bool fleeing;
            bool grenadeTutorialTarget;
            bool parachuting;
        };

        struct Projectile
        {
            float x;
            float y;
            float vx;
            float vy;
            float life;
            float damage;
            float blastRadius;
            ProjectileKind kind;
        };

        struct Particle
        {
            float x;
            float y;
            float vx;
            float vy;
            float life;
            float maxLife;
            float size;
            float rotation;
            Windows::UI::Color color;
        };

        struct DamageNumber
        {
            float x;
            float y;
            float life;
            std::wstring text;
        };

        struct Explosion
        {
            float x;
            float y;
            float life;
            bool hostile;
            float radius;
        };

        void StartGame(bool tutorial, int32_t level = 1);
        void BuildLevel();
        void UpdateGame();
        void Render();
        void DrawGame(Microsoft::Graphics::Canvas::UI::Xaml::CanvasControl const& sender,
            Microsoft::Graphics::Canvas::UI::Xaml::CanvasDrawEventArgs const& args);
        void UpdateHud();
        void EndGame(bool victory);
        void ShowSelection();
        void TogglePause();
        void ResetCurrentLevel();
        void SelectWeapon(WeaponKind weapon);
        void StartReload();
        void ClearMovementInput(bool stopImmediately);
        void SpawnEnemy(float x, size_t platformIndex, float health = 20.0f,
            bool grenadeTarget = false, EnemyKind kind = EnemyKind::Walker);
        void SpawnBurst(float x, float y, Windows::UI::Color color, int count);
        size_t FindPlatformAt(float x, float feetY, float tolerance) const;
        float MountainLength() const;
        float MountainProgressAt(float x, float y) const;
        MountainPoint MountainPointAt(float distance) const;
        void Explode(float x, float y, bool hostile = false, float damage = 8.0f,
            float radius = 0.0f);
        void DamagePlayer(float damage, float knockbackX, bool ignoreInvulnerability = false);
        void DamageEnemy(size_t index, float damage, bool grenadeDamage);
        void DamageEnemy(size_t index, float damage);
        void CheckTutorialProgress();

        winrt::Microsoft::UI::Xaml::DispatcherTimer m_timer{ nullptr };
        std::vector<Platform> m_platforms;
        std::vector<ClimbRoute> m_climbRoutes;
        std::vector<MountainPoint> m_mountainSurface;
        std::vector<Enemy> m_enemies;
        std::vector<Projectile> m_projectiles;
        std::vector<Particle> m_particles;
        std::vector<DamageNumber> m_damageNumbers;
        std::vector<Explosion> m_explosions;
        std::mt19937 m_random{ std::random_device{}() };
        std::chrono::steady_clock::time_point m_lastFrame{};
        float m_playerX{ 0.0f };
        float m_playerY{ 0.0f };
        float m_velocityX{ 0.0f };
        float m_velocityY{ 0.0f };
        float m_aimX{ 1.0f };
        float m_aimY{ 0.0f };
        float m_shotCooldown{ 0.0f };
        float m_elapsed{ 0.0f };
        float m_pointerX{ 0.0f };
        float m_pointerY{ 0.0f };
        float m_muzzleFlash{ 0.0f };
        float m_recoil{ 0.0f };
        float m_invulnerability{ 0.0f };
        float m_cameraShake{ 0.0f };
        float m_jumpBuffer{ 0.0f };
        float m_coyoteTime{ 0.0f };
        float m_cameraX{ 0.0f };
        float m_cameraY{ 0.0f };
        float m_worldWidth{ 0.0f };
        float m_viewHeight{ 0.0f };
        float m_screenShakeX{ 0.0f };
        float m_screenShakeY{ 0.0f };
        int32_t m_score{ 0 };
        int32_t m_playerHealth{ 40 };
        int32_t m_pistolAmmo{ 20 };
        int32_t m_pistolShotsSinceReload{ 0 };
        int32_t m_grenades{ 0 };
        int32_t m_tutorialStep{ 0 };
        int32_t m_facing{ 1 };
        int32_t m_currentLevel{ 1 };
        float m_reloadTimer{ 0.0f };
        float m_reloadDuration{ 0.0f };
        float m_reloadPhase{ 0.0f };
        float m_sequenceTime{ 0.0f };
        float m_victoryStartX{ 0.0f };
        float m_victoryStartY{ 0.0f };
        float m_pistolReloadTimer{ 0.0f };
        float m_grenadeReloadTimer{ 0.0f };
        float m_machineGunReloadTimer{ 0.0f };
        float m_stationaryTime{ 0.0f };
        float m_snowDamageTimer{ 0.0f };
        float m_parachuterSpawnTimer{ 0.0f };
        float m_machineGunPickupTime{ 0.0f };
        float m_exitY{ 0.0f };
        float m_machineGunPickupX{ 0.0f };
        float m_machineGunPickupY{ 0.0f };
        float m_climbProgress{ 0.0f };
        float m_mountainProgress{ 0.0f };
        float m_checkpointX{ 0.0f };
        float m_checkpointY{ 0.0f };
        bool m_gameRunning{ false };
        bool m_paused{ false };
        bool m_isTutorial{ false };
        bool m_tutorialComplete{ false };
        bool m_left{ false };
        bool m_right{ false };
        bool m_jumpRequested{ false };
        bool m_jumpKeyDown{ false };
        bool m_grounded{ false };
        bool m_mouseFiring{ false };
        bool m_pointerActive{ false };
        WeaponKind m_selectedWeapon{ WeaponKind::Pistol };
        WeaponKind m_reloadWeapon{ WeaponKind::Pistol };
        int32_t m_machineGunAmmo{ 50 };
        int32_t m_activeClimbRoute{ -1 };
        int32_t m_climbWallPlatform{ -1 };
        int32_t m_climbWallSide{ 0 };
        size_t m_checkpointPlatform{ 0 };
        bool m_climbing{ false };
        bool m_machineGunUnlocked{ false };
        bool m_machineGunPickupCollected{ false };
        bool m_reloading{ false };
        bool m_playerDead{ false };
        EndSequence m_endSequence{ EndSequence::None };
        bool m_windowActive{ true };
        std::vector<Microsoft::UI::Xaml::Controls::Button> m_levelButtons;
    };
}

namespace winrt::MyGame::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
