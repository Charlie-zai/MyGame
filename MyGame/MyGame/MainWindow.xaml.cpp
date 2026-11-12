#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <winrt/Microsoft.Graphics.Canvas.Geometry.h>
#include <winrt/Windows.Foundation.Numerics.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <queue>
#include <sstream>
#include <utility>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Xaml::Input;
using namespace Windows::System;

namespace winrt::MyGame::implementation
{
    namespace
    {
        Windows::UI::Color Color(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255)
        {
            return Windows::UI::ColorHelper::FromArgb(alpha, red, green, blue);
        }

        constexpr float Pi = 3.14159265f;
        constexpr float PlayerHalfWidth = 13.0f;
        constexpr float PlayerHalfHeight = 23.0f;
        constexpr float BulletDamage = 5.0f;
        constexpr float GrenadeRadius = 256.0f;
    }

    MainWindow::MainWindow()
    {
        InitializeComponent();

        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());
        SystemBackdrop(Microsoft::UI::Xaml::Media::DesktopAcrylicBackdrop{});
        auto titleBar = AppWindow().TitleBar();
        auto transparent = Color(0, 0, 0, 0);
        titleBar.BackgroundColor(transparent);
        titleBar.InactiveBackgroundColor(transparent);
        titleBar.ButtonBackgroundColor(transparent);
        titleBar.ButtonInactiveBackgroundColor(transparent);

        GameCanvas().Draw([this](auto const& sender, auto const& args)
        {
            DrawGame(sender, args);
        });

        m_timer = DispatcherTimer{};
        m_timer.Interval(Windows::Foundation::TimeSpan{ 160000 });
        m_timer.Tick([this](auto const&, auto const&)
        {
            UpdateGame();
        });

        m_levelButtons = {
            LevelButton1(), LevelButton2(), LevelButton3(), LevelButton4(), LevelButton5(),
            LevelButton6(), LevelButton7(), LevelButton8(), LevelButton9(), LevelButton10()
        };
        for (size_t i = 0; i < m_levelButtons.size(); ++i)
        {
            m_levelButtons[i].Click([this, level = static_cast<int32_t>(i + 1)](auto const&, auto const&)
            {
                StartGame(false, level);
            });
        }

        TutorialButton().Click([this](auto const&, auto const&)
        {
            StartGame(true, m_currentLevel);
        });
        QuitButton().Click([this](auto const&, auto const&)
        {
            Close();
        });
        PauseButton().Click([this](auto const&, auto const&)
        {
            TogglePause();
        });
        ResetButton().Click([this](auto const&, auto const&)
        {
            ResetCurrentLevel();
        });
        ExitLevelButton().Click([this](auto const&, auto const&)
        {
            ShowSelection();
        });
        ResumeButton().Click([this](auto const&, auto const&)
        {
            if (!m_gameRunning)
            {
                if (m_playerDead)
                {
                    m_endSequence = EndSequence::DeathRestart;
                    m_sequenceTime = 0.0f;
                    OverlayCardTranslate().Y(0.0);
                    m_lastFrame = std::chrono::steady_clock::now();
                    m_timer.Start();
                    return;
                }
                StartGame(m_isTutorial, m_currentLevel);
            }
            else
            {
                m_paused = false;
                Overlay().Visibility(Visibility::Collapsed);
                PauseButton().Content(box_value(hstring{ L"\xE769" }));
                m_lastFrame = std::chrono::steady_clock::now();
                InputSink().Focus(FocusState::Programmatic);
            }
        });
        ReturnToSelectionButton().Click([this](auto const&, auto const&)
        {
            ShowSelection();
        });

        Activated([this](IInspectable const&, WindowActivatedEventArgs const& args)
        {
            m_windowActive = args.WindowActivationState() != WindowActivationState::Deactivated;
            if (!m_windowActive)
            {
                ClearMovementInput(true);
                m_mouseFiring = false;
            }
        });

        PlayfieldHost().KeyDown([this](IInspectable const&, KeyRoutedEventArgs const& args)
        {
            auto key = args.Key();
            if (key == VirtualKey::Escape)
            {
                TogglePause();
                args.Handled(true);
                return;
            }
            if (!m_gameRunning || m_paused)
            {
                return;
            }
            if (key == VirtualKey::R)
            {
                StartReload();
                args.Handled(true);
                return;
            }
            switch (key)
            {
            case VirtualKey::Left:
            case VirtualKey::A:
            case VirtualKey::Right:
            case VirtualKey::D:
            case VirtualKey::Up:
            case VirtualKey::W:
            case VirtualKey::Space: return;
            default:
                if (static_cast<int>(key) == 0x31)
                {
                    SelectWeapon(WeaponKind::Pistol);
                }
                else if (static_cast<int>(key) == 0x32)
                {
                    SelectWeapon(WeaponKind::Grenade);
                }
                else if (static_cast<int>(key) == 0x33)
                {
                    SelectWeapon(WeaponKind::MachineGun);
                }
                else
                {
                    return;
                }
            }
            args.Handled(true);
        });

        PlayfieldHost().LostFocus([this](IInspectable const&, RoutedEventArgs const&)
        {
            ClearMovementInput(true);
            m_mouseFiring = false;
        });

        Arena().PointerMoved([this](IInspectable const&, PointerRoutedEventArgs const& args)
        {
            auto position = args.GetCurrentPoint(Arena()).Position();
            m_pointerX = static_cast<float>(position.X);
            m_pointerY = static_cast<float>(position.Y);
            m_pointerActive = true;
        });
        Arena().PointerPressed([this](IInspectable const&, PointerRoutedEventArgs const& args)
        {
            auto point = args.GetCurrentPoint(Arena());
            if (!point.Properties().IsLeftButtonPressed() || !m_gameRunning || m_paused)
            {
                return;
            }
            m_mouseFiring = true;
            auto position = point.Position();
            m_pointerX = static_cast<float>(position.X);
            m_pointerY = static_cast<float>(position.Y);
            m_pointerActive = true;
            Arena().CapturePointer(args.Pointer());
            InputSink().Focus(FocusState::Programmatic);
        });
        Arena().PointerReleased([this](IInspectable const&, PointerRoutedEventArgs const& args)
        {
            if (!args.GetCurrentPoint(Arena()).Properties().IsLeftButtonPressed())
            {
                m_mouseFiring = false;
                Arena().ReleasePointerCapture(args.Pointer());
            }
        });
        Arena().PointerCaptureLost([this](IInspectable const&, PointerRoutedEventArgs const&)
        {
            m_mouseFiring = false;
        });
        Arena().PointerWheelChanged([this](IInspectable const&, PointerRoutedEventArgs const& args)
        {
            if (!m_gameRunning || m_paused)
            {
                return;
            }
            auto nextWeapon = static_cast<int>(m_selectedWeapon) + 1;
            auto weaponCount = m_machineGunUnlocked ? 3 : 2;
            SelectWeapon(static_cast<WeaponKind>(nextWeapon % weaponCount));
            args.Handled(true);
        });
        Arena().SizeChanged([this](IInspectable const&, SizeChangedEventArgs const&)
        {
            if (m_gameRunning)
            {
                Render();
            }
        });

        ShowSelection();
    }

    void MainWindow::BuildLevel()
    {
        auto width = static_cast<float>(Arena().ActualWidth());
        auto height = static_cast<float>(Arena().ActualHeight());
        m_viewHeight = height;
        float floorY = height - 38.0f;
        m_platforms.clear();
        m_climbRoutes.clear();
        m_mountainSurface.clear();
        m_machineGunPickupCollected = false;
        m_parachuterSpawnTimer = 0.0f;
        m_enemies.clear();

        auto floor = [this](float start, float y, float length)
        {
            m_platforms.push_back({ start, y, length, m_viewHeight - y });
        };
        if (m_isTutorial)
        {
            m_worldWidth = width * 3.15f;
            floor(0.0f, floorY, width * 0.70f);
            floor(width * 0.81f, floorY, width * 0.66f);
            floor(width * 1.57f, floorY, width * 0.57f);
            floor(width * 2.24f, floorY, width * 0.91f);
            m_platforms.push_back({ width * 0.38f, height * 0.74f, width * 0.25f, 18.0f });
            m_platforms.push_back({ width * 1.09f, height * 0.69f, width * 0.24f, 18.0f });
            m_platforms.push_back({ width * 1.72f, height * 0.72f, width * 0.22f, 18.0f });
            m_platforms.push_back({ width * 2.08f, height * 0.54f, width * 0.21f, 18.0f });
        }
        else if (m_currentLevel == 1)
        {
            m_worldWidth = width * 8.55f;
            constexpr std::array<float, 10> starts{
                0.0f, 0.82f, 1.65f, 2.48f, 3.32f, 4.17f, 5.01f, 5.86f, 6.70f, 7.55f
            };
            constexpr std::array<float, 10> lengths{
                0.73f, 0.75f, 0.74f, 0.76f, 0.75f, 0.74f, 0.75f, 0.74f, 0.75f, 0.99f
            };
            constexpr std::array<float, 10> heights{
                0.88f, 0.83f, 0.91f, 0.85f, 0.90f, 0.82f, 0.89f, 0.84f, 0.91f, 0.86f
            };
            for (size_t i = 0; i < starts.size(); ++i)
            {
                floor(width * starts[i], height * heights[i], width * lengths[i]);
            }
            m_platforms.push_back({ width * 0.50f, height * 0.68f, width * 0.25f, 18.0f });
            m_platforms.push_back({ width * 1.37f, height * 0.63f, width * 0.19f, 18.0f });
            m_platforms.push_back({ width * 2.18f, height * 0.70f, width * 0.23f, 18.0f });
            m_platforms.push_back({ width * 3.06f, height * 0.60f, width * 0.18f, 18.0f });
            m_platforms.push_back({ width * 4.00f, height * 0.66f, width * 0.22f, 18.0f });
            m_platforms.push_back({ width * 4.92f, height * 0.57f, width * 0.18f, 18.0f });
            m_platforms.push_back({ width * 5.78f, height * 0.67f, width * 0.22f, 18.0f });
            m_platforms.push_back({ width * 6.66f, height * 0.58f, width * 0.19f, 18.0f });
        }
        else if (m_currentLevel == 2)
        {
            m_worldWidth = width * 8.55f;
            float wallX = width * 2.75f;
            float wallTop = height * 0.18f;
            floor(0.0f, floorY, width * 1.02f);
            floor(width * 1.09f, height * 0.88f, width * 0.80f);
            floor(width * 1.98f, height * 0.82f, wallX - width * 1.98f);
            floor(wallX + 34.0f, wallTop, width * 1.08f);
            floor(width * 3.98f, height * 0.40f, width * 0.80f);
            floor(width * 4.88f, height * 0.49f, width * 0.90f);
            floor(width * 5.88f, height * 0.37f, width * 0.83f);
            floor(width * 6.82f, height * 0.44f, width * 0.72f);
            floor(width * 7.66f, height * 0.36f, width * 0.90f);
            m_platforms.push_back({ width * 2.98f, height * 0.55f, width * 0.40f, 18.0f });
            m_platforms.push_back({ width * 3.42f, height * 0.42f, width * 0.32f, 18.0f });
            m_platforms.push_back({ width * 3.82f, height * 0.53f, width * 0.30f, 18.0f });
            m_platforms.push_back({ width * 4.62f, height * 0.56f, width * 0.25f, 18.0f });
            m_climbRoutes.push_back({ wallX - PlayerHalfWidth,
                height * 0.82f - PlayerHalfHeight,
                wallX + PlayerHalfWidth + 37.0f, wallTop - PlayerHalfHeight, wallX });
            m_machineGunPickupX = wallX + 100.0f;
            m_machineGunPickupY = wallTop - PlayerHalfHeight;
        }
        else if (m_currentLevel == 3)
        {
            m_worldWidth = width * 8.55f;
            constexpr std::array<float, 10> starts{
                0.0f, 0.82f, 1.65f, 2.48f, 3.31f, 4.14f, 4.97f, 5.80f, 6.63f, 7.46f
            };
            constexpr std::array<float, 10> lengths{
                0.74f, 0.73f, 0.76f, 0.74f, 0.75f, 0.74f, 0.76f, 0.75f, 0.76f, 1.08f
            };
            constexpr std::array<float, 10> heights{
                0.88f, 0.82f, 0.90f, 0.84f, 0.89f, 0.82f, 0.87f, 0.84f, 0.89f, 0.88f
            };
            for (size_t i = 0; i < starts.size(); ++i)
            {
                floor(width * starts[i], height * heights[i], width * lengths[i]);
            }
            m_platforms.push_back({ width * 0.54f, height * 0.67f, width * 0.22f, 18.0f });
            m_platforms.push_back({ width * 1.48f, height * 0.61f, width * 0.20f, 18.0f });
            m_platforms.push_back({ width * 2.44f, height * 0.68f, width * 0.19f, 18.0f });
            m_platforms.push_back({ width * 3.42f, height * 0.59f, width * 0.20f, 18.0f });
            m_platforms.push_back({ width * 4.38f, height * 0.66f, width * 0.19f, 18.0f });
        }
        else if (m_currentLevel == 4)
        {
            m_worldWidth = width * 6.75f;
            floor(0.0f, floorY, width * 2.30f);
            m_mountainSurface = {
                { width * 2.18f, floorY },
                { width * 2.92f, height * 0.78f },
                { width * 3.86f, height * 0.65f },
                { width * 4.70f, height * 0.51f },
                { width * 5.10f, height * 0.36f },
                { width * 5.27f, height * 0.22f },
                { width * 5.31f, height * 0.12f }
            };
            constexpr std::array<float, 5> restX{ 2.62f, 3.55f, 4.42f, 4.98f, 5.31f };
            constexpr std::array<float, 5> restY{ 0.0f, 0.65f, 0.51f, 0.36f, 0.12f };
            constexpr std::array<float, 5> restWidth{ 0.62f, 0.52f, 0.50f, 0.48f, 1.44f };
            std::array<size_t, 5> restPlatforms{};
            for (size_t i = 0; i < restX.size(); ++i)
            {
                float y = i == 0 ? floorY : height * restY[i];
                m_platforms.push_back({ width * restX[i], y, width * restWidth[i],
                    i == 0 ? m_viewHeight - y : 18.0f });
                restPlatforms[i] = m_platforms.size() - 1;
            }
        }
        else
        {
            m_worldWidth = width * 5.35f;
            constexpr std::array<std::array<float, 6>, 5> floorStarts{ {
                { 0.00f, 0.90f, 1.88f, 2.77f, 3.62f, 4.52f },
                { 0.00f, 0.78f, 1.74f, 2.66f, 3.58f, 4.47f },
                { 0.00f, 0.86f, 1.80f, 2.71f, 3.69f, 4.55f },
                { 0.00f, 0.92f, 1.85f, 2.73f, 3.67f, 4.51f },
                { 0.00f, 0.81f, 1.73f, 2.62f, 3.61f, 4.50f }
            } };
            constexpr std::array<std::array<float, 6>, 5> floorLengths{ {
                { 0.82f, 0.76f, 0.73f, 0.79f, 0.72f, 0.83f },
                { 0.68f, 0.82f, 0.78f, 0.74f, 0.72f, 0.88f },
                { 0.74f, 0.79f, 0.75f, 0.80f, 0.69f, 0.80f },
                { 0.84f, 0.73f, 0.74f, 0.77f, 0.71f, 0.84f },
                { 0.70f, 0.77f, 0.74f, 0.82f, 0.70f, 0.85f }
            } };
            constexpr std::array<float, 7> platformX{ 0.58f, 1.23f, 1.72f, 2.33f, 2.98f, 3.50f, 4.12f };
            constexpr std::array<float, 7> platformY{ 0.80f, 0.73f, 0.58f, 0.80f, 0.72f, 0.58f, 0.78f };
            auto layout = static_cast<size_t>((m_currentLevel - 1) % 5);
            for (size_t i = 0; i < 6; ++i)
            {
                floor(width * floorStarts[layout][i], floorY,
                    width * floorLengths[layout][i]);
            }
            for (size_t i = 0; i < platformX.size(); ++i)
            {
                float xShift = static_cast<float>((m_currentLevel * 3 + i * 7) % 5) * 0.018f;
                float yShift = static_cast<float>((m_currentLevel + i * 2) % 4) * 0.018f;
                float platformWidth = 0.20f
                    + static_cast<float>((layout + m_currentLevel + i) % 4) * 0.023f;
                m_platforms.push_back({ width * (platformX[i] + xShift),
                    height * (platformY[i] - yShift), width * platformWidth, 18.0f });
            }
        }

        if (m_isTutorial)
        {
            SpawnEnemy(width * 0.52f, 0);
            SpawnEnemy(width * 1.10f, 1, 20.0f, true);
            SpawnEnemy(width * 1.94f, 2);
            SpawnEnemy(width * 2.42f, 3);
        }
        else if (m_currentLevel == 1)
        {
            for (size_t i = 0; i < 9; ++i)
            {
                size_t platformIndex = i;
                float fraction = i % 2 == 0 ? 0.42f : 0.68f;
                SpawnEnemy(m_platforms[platformIndex].x
                    + m_platforms[platformIndex].width * fraction, platformIndex,
                    20.0f + (i % 3 == 0 ? 5.0f : 0.0f));
            }
            for (size_t i = 10; i < 18; ++i)
            {
                size_t platformIndex = i;
                float fraction = i % 2 == 0 ? 0.38f : 0.72f;
                SpawnEnemy(m_platforms[platformIndex].x
                    + m_platforms[platformIndex].width * fraction, platformIndex);
            }
            auto const& guardPlatform = m_platforms[9];
            for (int guard = 0; guard < 5; ++guard)
            {
                SpawnEnemy(guardPlatform.x + guardPlatform.width
                    * (0.14f + static_cast<float>(guard) * 0.18f), 9, 25.0f);
            }
        }
        else if (m_currentLevel == 2)
        {
            SpawnEnemy(m_platforms[9].x + m_platforms[9].width * 0.32f,
                9, 24.0f, false, EnemyKind::Bomber);
            SpawnEnemy(m_platforms[10].x + m_platforms[10].width * 0.52f,
                10, 24.0f, false, EnemyKind::Bomber);
            SpawnEnemy(m_platforms[11].x + m_platforms[11].width * 0.65f,
                11, 24.0f, false, EnemyKind::Bomber);
            for (int guard = 0; guard < 3; ++guard)
            {
                auto const& platform = m_platforms[6 + guard];
                SpawnEnemy(platform.x + platform.width * (0.32f + guard * 0.16f),
                    6 + guard, 24.0f);
            }
            SpawnEnemy(m_platforms[8].x + m_platforms[8].width * 0.48f,
                8, 24.0f, false, EnemyKind::Gunner);
            SpawnEnemy(m_platforms[8].x + m_platforms[8].width * 0.77f,
                8, 24.0f, false, EnemyKind::Gunner);
        }
        else if (m_currentLevel == 3)
        {
            for (size_t i = 0; i < 8; ++i)
            {
                size_t platformIndex = i;
                float fraction = 0.30f + static_cast<float>((i * 13) % 50) / 100.0f;
                SpawnEnemy(m_platforms[platformIndex].x
                    + m_platforms[platformIndex].width * fraction, platformIndex,
                    20.0f, false, i % 4 == 2 ? EnemyKind::Gunner : EnemyKind::Walker);
            }
        }
        else if (m_currentLevel == 4)
        {
            std::array<size_t, 5> restPlatforms{ 1, 2, 3, 4, 5 };
            for (size_t platformIndex : restPlatforms)
            {
                for (float fraction : { 0.28f, 0.47f, 0.83f })
                {
                    auto const& rest = m_platforms[platformIndex];
                    SpawnEnemy(rest.x + rest.width * fraction, platformIndex,
                        20.0f, false, EnemyKind::SnowWalker);
                }
                auto const& rest = m_platforms[platformIndex];
                SpawnEnemy(rest.x + rest.width * 0.68f, platformIndex,
                    28.0f, false, EnemyKind::SnowArtillery);
            }
        }
        else
        {
            int32_t count = 4 + m_currentLevel / 2;
            for (int32_t i = 0; i < count; ++i)
            {
                size_t platformIndex = i % 3 == 2
                    ? 6 + static_cast<size_t>((i * 3 + m_currentLevel) % 7)
                    : static_cast<size_t>(i % 6);
                float fraction = 0.24f + static_cast<float>((i * 19 + m_currentLevel * 13) % 50) / 100.0f;
                float x = m_platforms[platformIndex].x
                    + m_platforms[platformIndex].width * fraction;
                EnemyKind kind = EnemyKind::Walker;
                if (m_currentLevel >= 3 && (i + m_currentLevel) % 5 == 0)
                {
                    kind = EnemyKind::Armored;
                }
                else if (m_currentLevel >= 2 && (i + m_currentLevel) % 4 == 1)
                {
                    kind = EnemyKind::Bomber;
                }
                else if (m_currentLevel >= 3 && (i + m_currentLevel) % 4 == 2)
                {
                    kind = EnemyKind::Gunner;
                }
                SpawnEnemy(x, platformIndex,
                    20.0f + static_cast<float>(m_currentLevel - 1) * 2.0f,
                    false, kind);
            }
            if (m_currentLevel == 5)
            {
                SpawnEnemy(m_platforms[5].x + m_platforms[5].width * 0.58f,
                    5, 280.0f, false, EnemyKind::MiniBoss);
            }
            else if (m_currentLevel == 10)
            {
                SpawnEnemy(m_platforms[5].x + m_platforms[5].width * 0.58f,
                    5, 720.0f, false, EnemyKind::FinalBoss);
            }
        }

        float exitX = m_worldWidth - 80.0f;
        auto const& finalPlatform = *std::min_element(m_platforms.begin(), m_platforms.end(),
            [exitX](Platform const& left, Platform const& right)
            {
                auto distance = [exitX](Platform const& platform)
                {
                    return exitX < platform.x ? platform.x - exitX
                        : exitX > platform.x + platform.width
                            ? exitX - (platform.x + platform.width) : 0.0f;
                };
                return distance(left) < distance(right);
            });
        m_exitY = finalPlatform.y - 32.0f;
    }

    void MainWindow::StartGame(bool tutorial, int32_t level)
    {
        auto width = static_cast<float>(Arena().ActualWidth());
        auto height = static_cast<float>(Arena().ActualHeight());
        if (width <= 0.0f || height <= 0.0f)
        {
            return;
        }

        const bool transitionFromSelection =
            SelectionPage().Visibility() == Visibility::Visible;
        m_isTutorial = tutorial;
        if (!tutorial)
        {
            m_currentLevel = std::clamp(level, 1, 10);
        }
        m_score = 0;
        m_playerHealth = 40;
        m_pistolAmmo = 20;
        m_pistolShotsSinceReload = 0;
        m_grenades = tutorial ? 0 : 1;
        m_machineGunAmmo = 50;
        m_tutorialStep = 0;
        m_elapsed = 0.0f;
        m_shotCooldown = 0.0f;
        m_reloadTimer = 0.0f;
        m_reloadDuration = 0.0f;
        m_reloadPhase = 0.0f;
        m_sequenceTime = 0.0f;
        m_endSequence = EndSequence::None;
        m_playerDead = false;
        m_pistolReloadTimer = 0.0f;
        m_grenadeReloadTimer = 0.0f;
        m_machineGunReloadTimer = 0.0f;
        m_stationaryTime = 0.0f;
        m_snowDamageTimer = 0.0f;
        m_machineGunPickupTime = 0.0f;
        m_reloading = false;
        m_reloadWeapon = WeaponKind::Pistol;
        m_muzzleFlash = 0.0f;
        m_recoil = 0.0f;
        m_invulnerability = 0.0f;
        m_cameraShake = 0.0f;
        m_velocityX = 0.0f;
        m_velocityY = 0.0f;
        m_facing = 1;
        m_grounded = false;
        m_jumpKeyDown = false;
        m_jumpBuffer = 0.0f;
        m_coyoteTime = 0.0f;
        m_activeClimbRoute = -1;
        m_climbProgress = 0.0f;
        m_mountainProgress = 0.0f;
        m_climbWallPlatform = -1;
        m_climbWallSide = 0;
        m_climbing = false;
        m_left = m_right = m_jumpRequested = m_mouseFiring = false;
        m_pointerActive = false;
        m_selectedWeapon = WeaponKind::Pistol;
        m_projectiles.clear();
        m_particles.clear();
        m_damageNumbers.clear();
        m_explosions.clear();
        BuildLevel();

        m_playerX = width * 0.12f;
        m_playerY = m_platforms.front().y - PlayerHalfHeight;
        m_checkpointX = m_playerX;
        m_checkpointY = m_playerY;
        m_checkpointPlatform = 0;
        m_grounded = true;
        m_cameraX = 0.0f;
        m_cameraY = 0.0f;
        m_machineGunUnlocked = m_machineGunUnlocked || (!tutorial && m_currentLevel >= 3);
        m_paused = false;
        m_gameRunning = true;
        m_lastFrame = std::chrono::steady_clock::now();

        GamePage().Visibility(Visibility::Visible);
        SelectionPage().Visibility(Visibility::Collapsed);
        SelectionPageTranslate().X(0.0);
        SelectionContentTranslate().X(0.0);
        MessagePanel().Visibility(Visibility::Collapsed);
        Overlay().Visibility(Visibility::Collapsed);
        Overlay().Opacity(1.0);
        OverlayCardTranslate().Y(0.0);
        GameControls().Visibility(Visibility::Visible);
        WeaponHud().Visibility(Visibility::Visible);
        PauseButton().Content(box_value(hstring{ L"\xE769" }));
        UpdateHud();
        if (transitionFromSelection)
        {
            SelectionPage().Visibility(Visibility::Visible);
            m_endSequence = EndSequence::SelectionExit;
            m_sequenceTime = 0.0f;
        }
        else
        {
            InputSink().Focus(FocusState::Programmatic);
        }
        m_timer.Start();
        Render();
    }

    void MainWindow::SpawnEnemy(float x, size_t platformIndex, float health,
        bool grenadeTarget, EnemyKind kind)
    {
        if (platformIndex >= m_platforms.size())
        {
            return;
        }
        auto const& platform = m_platforms[platformIndex];
        float halfWidth = kind == EnemyKind::FinalBoss ? 40.0f
            : kind == EnemyKind::MiniBoss ? 31.0f : 19.0f;
        float halfHeight = kind == EnemyKind::FinalBoss ? 31.0f
            : kind == EnemyKind::MiniBoss ? 24.0f : 15.0f;
        auto minX = platform.x + halfWidth;
        auto maxX = platform.x + platform.width - halfWidth;
        if (maxX < minX)
        {
            minX = maxX = platform.x + platform.width * 0.5f;
        }
        x = std::clamp(x, minX, maxX);
        std::uniform_real_distribution<float> speed(
            kind == EnemyKind::FinalBoss ? 65.0f : 35.0f,
            kind == EnemyKind::FinalBoss ? 100.0f : kind == EnemyKind::MiniBoss ? 78.0f : 58.0f);
        std::uniform_real_distribution<float> phase(0.0f, 6.28f);
        float direction = (m_random() & 1) == 0 ? -1.0f : 1.0f;
        m_enemies.push_back({ x, platform.y - halfHeight, speed(m_random) * direction,
            minX, maxX, health, health, health, 0.0f, phase(m_random),
            1.0f + static_cast<float>(m_random() % 100) / 100.0f, 0.0f, 0.0f,
            0.0f, 0.0f, platformIndex, kind, true, false, grenadeTarget, false });
        if (kind == EnemyKind::SnowWalker || kind == EnemyKind::SnowArtillery)
        {
            m_enemies.back().attackCooldown = 3.0f;
        }
    }

    size_t MainWindow::FindPlatformAt(float x, float feetY, float tolerance) const
    {
        size_t closest = m_platforms.size();
        float closestDistance = tolerance;
        for (size_t i = 0; i < m_platforms.size(); ++i)
        {
            auto const& platform = m_platforms[i];
            if (x + 12.0f < platform.x || x - 12.0f > platform.x + platform.width)
            {
                continue;
            }
            float distance = std::abs(feetY - platform.y);
            if (distance <= closestDistance)
            {
                closest = i;
                closestDistance = distance;
            }
        }
        return closest;
    }

    float MainWindow::MountainLength() const
    {
        float length = 0.0f;
        for (size_t i = 1; i < m_mountainSurface.size(); ++i)
        {
            float dx = m_mountainSurface[i].x - m_mountainSurface[i - 1].x;
            float dy = m_mountainSurface[i].y - m_mountainSurface[i - 1].y;
            length += std::sqrt(dx * dx + dy * dy);
        }
        return length;
    }

    float MainWindow::MountainProgressAt(float x, float y) const
    {
        float traveled = 0.0f;
        float closestDistanceSquared = (std::numeric_limits<float>::max)();
        float closestProgress = 0.0f;
        for (size_t i = 1; i < m_mountainSurface.size(); ++i)
        {
            auto const& start = m_mountainSurface[i - 1];
            auto const& end = m_mountainSurface[i];
            float dx = end.x - start.x;
            float dy = end.y - start.y;
            float segmentLengthSquared = dx * dx + dy * dy;
            float segmentLength = std::sqrt(segmentLengthSquared);
            float fraction = segmentLengthSquared > 0.0f
                ? std::clamp(((x - start.x) * dx + (y - start.y) * dy)
                    / segmentLengthSquared, 0.0f, 1.0f) : 0.0f;
            float nearestX = start.x + dx * fraction;
            float nearestY = start.y + dy * fraction;
            float offsetX = x - nearestX;
            float offsetY = y - nearestY;
            float distanceSquared = offsetX * offsetX + offsetY * offsetY;
            if (distanceSquared < closestDistanceSquared)
            {
                closestDistanceSquared = distanceSquared;
                closestProgress = traveled + segmentLength * fraction;
            }
            traveled += segmentLength;
        }
        return closestProgress;
    }

    MainWindow::MountainPoint MainWindow::MountainPointAt(float distance) const
    {
        if (m_mountainSurface.empty())
        {
            return {};
        }
        if (m_mountainSurface.size() == 1 || distance <= 0.0f)
        {
            return m_mountainSurface.front();
        }

        for (size_t i = 1; i < m_mountainSurface.size(); ++i)
        {
            auto const& start = m_mountainSurface[i - 1];
            auto const& end = m_mountainSurface[i];
            float dx = end.x - start.x;
            float dy = end.y - start.y;
            float segmentLength = std::sqrt(dx * dx + dy * dy);
            if (distance <= segmentLength)
            {
                float fraction = segmentLength > 0.0f ? distance / segmentLength : 1.0f;
                return { start.x + dx * fraction, start.y + dy * fraction };
            }
            distance -= segmentLength;
        }
        return m_mountainSurface.back();
    }

    void MainWindow::SpawnBurst(float x, float y, Windows::UI::Color color, int count)
    {
        std::uniform_real_distribution<float> angle(0.0f, 6.2831853f);
        std::uniform_real_distribution<float> speed(55.0f, 245.0f);
        std::uniform_real_distribution<float> lifetime(0.16f, 0.42f);
        std::uniform_real_distribution<float> size(2.0f, 5.0f);
        for (int i = 0; i < count; ++i)
        {
            auto a = angle(m_random);
            auto s = speed(m_random);
            auto life = lifetime(m_random);
            m_particles.push_back({ x, y, std::cos(a) * s, std::sin(a) * s,
                life, life, size(m_random), a, color });
        }
    }

    void MainWindow::SelectWeapon(WeaponKind weapon)
    {
        if (weapon == WeaponKind::MachineGun && !m_machineGunUnlocked)
        {
            return;
        }
        m_selectedWeapon = weapon;
        m_reloadWeapon = weapon;
        m_reloadDuration = weapon == WeaponKind::MachineGun ? 10.0f : 3.0f;
        m_reloadTimer = weapon == WeaponKind::Grenade ? m_grenadeReloadTimer
            : weapon == WeaponKind::MachineGun ? m_machineGunReloadTimer
            : m_pistolReloadTimer;
        m_reloading = m_reloadTimer > 0.0f;
        m_reloadPhase = m_reloading
            ? 1.0f - m_reloadTimer / m_reloadDuration : 0.0f;
        UpdateHud();
    }

    void MainWindow::StartReload()
    {
        if (!m_gameRunning || m_paused)
        {
            return;
        }

        if (m_selectedWeapon == WeaponKind::Pistol
            && m_pistolAmmo < 20 && m_pistolReloadTimer <= 0.0f)
        {
            m_pistolReloadTimer = 3.0f;
        }
        else if (m_selectedWeapon == WeaponKind::MachineGun
            && m_machineGunAmmo < 50 && m_machineGunReloadTimer <= 0.0f)
        {
            m_machineGunReloadTimer = 10.0f;
        }
        else
        {
            return;
        }

        m_reloading = true;
        m_reloadWeapon = m_selectedWeapon;
        m_reloadDuration = m_selectedWeapon == WeaponKind::MachineGun ? 10.0f : 3.0f;
        m_reloadTimer = m_reloadDuration;
        m_reloadPhase = 0.0f;
        m_shotCooldown = (std::max)(m_shotCooldown, 0.12f);
        UpdateHud();
    }

    void MainWindow::ClearMovementInput(bool stopImmediately)
    {
        m_left = false;
        m_right = false;
        m_jumpRequested = false;
        m_jumpKeyDown = false;
        m_jumpBuffer = 0.0f;
        if (stopImmediately)
        {
            m_velocityX = 0.0f;
        }
    }

    void MainWindow::UpdateGame()
    {
        if (m_paused)
        {
            return;
        }

        auto now = std::chrono::steady_clock::now();
        float deltaTime = std::chrono::duration<float>(now - m_lastFrame).count();
        m_lastFrame = now;
        deltaTime = std::clamp(deltaTime, 0.001f, 0.04f);
        float width = static_cast<float>(Arena().ActualWidth());
        float height = static_cast<float>(Arena().ActualHeight());
        if (m_endSequence != EndSequence::None)
        {
            m_sequenceTime += deltaTime;
            m_elapsed += deltaTime;
            m_cameraShake = (std::max)(0.0f, m_cameraShake - deltaTime * 28.0f);

            if (m_endSequence == EndSequence::Death)
            {
                for (auto& particle : m_particles)
                {
                    particle.x += particle.vx * deltaTime;
                    particle.y += particle.vy * deltaTime;
                    particle.vy += 420.0f * deltaTime;
                    particle.vx *= 0.985f;
                    particle.life -= deltaTime;
                    particle.rotation += deltaTime * 420.0f;
                }
                m_particles.erase(std::remove_if(m_particles.begin(), m_particles.end(),
                    [](Particle const& particle) { return particle.life <= 0.0f; }),
                    m_particles.end());

                if (m_sequenceTime >= 0.68f)
                {
                    Overlay().Visibility(Visibility::Visible);
                    MessagePanel().Visibility(Visibility::Visible);
                    float reveal = std::clamp((m_sequenceTime - 0.68f) / 0.52f,
                        0.0f, 1.0f);
                    Overlay().Opacity(reveal);
                    OverlayCardTranslate().Y(16.0 * (1.0 - reveal));
                }
                if (m_sequenceTime >= 1.20f)
                {
                    Overlay().Opacity(1.0);
                    OverlayCardTranslate().Y(0.0);
                    m_endSequence = EndSequence::None;
                    m_timer.Stop();
                }
            }
            else if (m_endSequence == EndSequence::DeathRestart)
            {
                float progress = std::clamp(m_sequenceTime / 0.48f, 0.0f, 1.0f);
                float ease = progress * progress * (3.0f - 2.0f * progress);
                OverlayCardTranslate().Y((height + 260.0) * ease);
                if (progress >= 1.0f)
                {
                    StartGame(m_isTutorial, m_currentLevel);
                    return;
                }
            }
            else if (m_endSequence == EndSequence::Victory)
            {
                float exitCenterX = m_worldWidth - 50.0f;
                float travelDirection = m_victoryStartX <= exitCenterX ? 1.0f : -1.0f;
                float walkProgress = std::clamp((m_sequenceTime - 0.48f) / 0.82f,
                    0.0f, 1.0f);
                float walkEase = walkProgress * walkProgress * (3.0f - 2.0f * walkProgress);
                m_playerX = m_victoryStartX
                    + (exitCenterX - m_victoryStartX) * walkEase;
                m_playerY = m_victoryStartY
                    + (m_exitY + 9.0f - m_victoryStartY) * walkEase;
                m_velocityX = walkProgress > 0.0f && walkProgress < 1.0f
                    ? travelDirection * 135.0f : 0.0f;
                m_velocityY = 0.0f;
                m_facing = travelDirection > 0.0f ? 1 : -1;
                float targetCameraX = (std::max)(0.0f, exitCenterX - width * 0.68f);
                m_cameraX += (targetCameraX - m_cameraX)
                    * (1.0f - std::exp(-4.0f * deltaTime));
                float targetCameraY = m_exitY - height * 0.53f;
                m_cameraY += (targetCameraY - m_cameraY)
                    * (1.0f - std::exp(-4.0f * deltaTime));

                if (m_sequenceTime >= 1.82f)
                {
                    ShowSelection();
                    if (m_isTutorial)
                    {
                        SelectionEyebrow().Text(L"TUTORIAL CLEAR");
                        SelectionTitle().Text(L"训练完成");
                        SelectionDescription().Text(L"已经掌握基本操作。现在可以自由选择行动区域。");
                    }
                    else
                    {
                        SelectionEyebrow().Text(L"CHAPTER CLEAR");
                        SelectionTitle().Text(L"第 " + to_hstring(m_currentLevel) + L" 关完成");
                        SelectionDescription().Text(m_currentLevel == 10
                            ? L"最终区域已安全撤离。仍可重玩任意关卡，或再次挑战风暴核心。"
                            : L"这一带已经安全。选择其他区域继续挑战，或再次进入本关。");
                    }
                    SelectionContentTranslate().X(width * 0.16);
                    m_endSequence = EndSequence::Selection;
                    m_sequenceTime = 0.0f;
                    m_lastFrame = now;
                    m_timer.Start();
                }
            }
            else if (m_endSequence == EndSequence::Selection)
            {
                float progress = std::clamp(m_sequenceTime / 0.72f, 0.0f, 1.0f);
                float ease = 1.0f - std::pow(1.0f - progress, 3.0f);
                SelectionContentTranslate().X(width * 0.16 * (1.0 - ease));
                if (progress >= 1.0f)
                {
                    SelectionContentTranslate().X(0.0);
                    GamePage().Visibility(Visibility::Collapsed);
                    m_endSequence = EndSequence::None;
                    m_timer.Stop();
                }
            }
            else if (m_endSequence == EndSequence::SelectionExit)
            {
                float progress = std::clamp(m_sequenceTime / 0.62f, 0.0f, 1.0f);
                float ease = progress * progress * (3.0f - 2.0f * progress);
                SelectionPageTranslate().X(-width * ease);
                if (progress >= 1.0f)
                {
                    SelectionPageTranslate().X(0.0);
                    SelectionPage().Visibility(Visibility::Collapsed);
                    m_endSequence = EndSequence::None;
                    InputSink().Focus(FocusState::Programmatic);
                    m_lastFrame = now;
                }
            }
            Render();
            return;
        }

        if (!m_gameRunning)
        {
            return;
        }

        if (width <= 0.0f || height <= 0.0f || m_platforms.empty())
        {
            return;
        }
        m_elapsed += deltaTime;
        m_machineGunPickupTime = (std::max)(0.0f, m_machineGunPickupTime - deltaTime);
        auto keyDown = [](int key)
        {
            return (GetAsyncKeyState(key) & 0x8000) != 0;
        };
        m_left = m_windowActive && (keyDown('A') || keyDown(VK_LEFT));
        m_right = m_windowActive && (keyDown('D') || keyDown(VK_RIGHT));
        bool jumpDown = m_windowActive
            && (keyDown(VK_SPACE) || keyDown('W') || keyDown(VK_UP));
        if (jumpDown && !m_jumpKeyDown)
        {
            m_jumpRequested = true;
        }
        m_jumpKeyDown = jumpDown;

        m_shotCooldown = (std::max)(0.0f, m_shotCooldown - deltaTime);
        if (m_pistolReloadTimer > 0.0f)
        {
            m_pistolReloadTimer = (std::max)(0.0f, m_pistolReloadTimer - deltaTime);
            if (m_pistolReloadTimer <= 0.0f)
            {
                m_pistolAmmo = 20;
                m_pistolShotsSinceReload = 0;
                SpawnBurst(m_playerX, m_playerY - 5.0f, Color(197, 239, 124), 5);
            }
        }
        if (m_grenadeReloadTimer > 0.0f)
        {
            m_grenadeReloadTimer = (std::max)(0.0f, m_grenadeReloadTimer - deltaTime);
            if (m_grenadeReloadTimer <= 0.0f)
            {
                SpawnBurst(m_playerX, m_playerY - 5.0f, Color(255, 190, 104), 7);
            }
        }
        if (m_machineGunReloadTimer > 0.0f)
        {
            m_machineGunReloadTimer = (std::max)(0.0f, m_machineGunReloadTimer - deltaTime);
            if (m_machineGunReloadTimer <= 0.0f)
            {
                m_machineGunAmmo = 50;
                SpawnBurst(m_playerX, m_playerY - 5.0f, Color(219, 232, 198), 8);
            }
        }
        m_reloadWeapon = m_selectedWeapon;
        m_reloadDuration = m_selectedWeapon == WeaponKind::MachineGun ? 10.0f : 3.0f;
        m_reloadTimer = m_selectedWeapon == WeaponKind::Grenade ? m_grenadeReloadTimer
            : m_selectedWeapon == WeaponKind::MachineGun ? m_machineGunReloadTimer
            : m_pistolReloadTimer;
        m_reloading = m_reloadTimer > 0.0f;
        m_reloadPhase = m_reloading
            ? 1.0f - m_reloadTimer / m_reloadDuration : 0.0f;
        m_muzzleFlash = (std::max)(0.0f, m_muzzleFlash - deltaTime);
        m_recoil = (std::max)(0.0f, m_recoil - deltaTime * 6.5f);
        m_invulnerability = (std::max)(0.0f, m_invulnerability - deltaTime);
        m_cameraShake = (std::max)(0.0f, m_cameraShake - deltaTime * 28.0f);

        float moveDirection = static_cast<float>(m_right) - static_cast<float>(m_left);
        if (moveDirection != 0.0f)
        {
            m_velocityX += moveDirection * 1850.0f * deltaTime;
            m_velocityX = std::clamp(m_velocityX, -300.0f, 300.0f);
            m_facing = moveDirection > 0.0f ? 1 : -1;
        }
        else
        {
            float drag = (m_grounded ? 2100.0f : 720.0f) * deltaTime;
            if (std::abs(m_velocityX) <= drag)
            {
                m_velocityX = 0.0f;
            }
            else
            {
                m_velocityX -= std::copysign(drag, m_velocityX);
            }
        }
        if (m_jumpRequested)
        {
            m_jumpBuffer = 0.14f;
        }
        else
        {
            m_jumpBuffer = (std::max)(0.0f, m_jumpBuffer - deltaTime);
        }
        m_jumpRequested = false;
        if (m_grounded)
        {
            m_coyoteTime = 0.12f;
        }
        else
        {
            m_coyoteTime = (std::max)(0.0f, m_coyoteTime - deltaTime);
        }
        if (!m_climbing && jumpDown && moveDirection != 0.0f)
        {
            for (size_t i = 0; i < m_platforms.size(); ++i)
            {
                auto const& platform = m_platforms[i];
                float sideX = moveDirection > 0.0f
                    ? platform.x : platform.x + platform.width;
                bool verticalOverlap = m_playerY + PlayerHalfHeight > platform.y + 3.0f
                    && m_playerY - PlayerHalfHeight < platform.y + platform.height - 3.0f;
                bool nearWall = std::abs(m_playerX + moveDirection * PlayerHalfWidth - sideX)
                    <= 30.0f;
                if (!verticalOverlap || !nearWall
                    || (moveDirection > 0.0f && m_playerX > sideX)
                    || (moveDirection < 0.0f && m_playerX < sideX))
                {
                    continue;
                }
                m_climbing = true;
                m_activeClimbRoute = -1;
                m_climbWallPlatform = static_cast<int32_t>(i);
                m_climbWallSide = moveDirection > 0.0f ? -1 : 1;
                m_playerX = sideX + static_cast<float>(m_climbWallSide) * PlayerHalfWidth;
                m_velocityX = 0.0f;
                m_velocityY = 0.0f;
                m_grounded = false;
                m_jumpBuffer = 0.0f;
                break;
            }
        }
        if (!m_climbing && jumpDown && m_right && !m_mountainSurface.empty())
        {
            float progress = MountainProgressAt(
                m_playerX + PlayerHalfWidth, m_playerY + PlayerHalfHeight);
            auto surfacePoint = MountainPointAt(progress);
            float dx = m_playerX + PlayerHalfWidth - surfacePoint.x;
            float dy = m_playerY + PlayerHalfHeight - surfacePoint.y;
            if (dx * dx + dy * dy <= 76.0f * 76.0f)
            {
                m_climbing = true;
                m_activeClimbRoute = -2;
                m_climbWallPlatform = -1;
                m_mountainProgress = progress;
                m_playerX = surfacePoint.x - PlayerHalfWidth;
                m_playerY = surfacePoint.y - PlayerHalfHeight;
                m_velocityX = 0.0f;
                m_velocityY = 0.0f;
                m_grounded = false;
                m_jumpBuffer = 0.0f;
            }
        }
        if (!m_climbing && m_jumpBuffer > 0.0f && m_coyoteTime > 0.0f)
        {
            m_velocityY = -680.0f;
            m_grounded = false;
            m_jumpBuffer = 0.0f;
            m_coyoteTime = 0.0f;
            SpawnBurst(m_playerX, m_playerY + PlayerHalfHeight - 3.0f,
                Color(202, 211, 187, 190), 7);
        }

        float oldPlayerX = m_playerX;
        float oldFeet = m_playerY + PlayerHalfHeight;
        if (m_climbing && m_activeClimbRoute == -2
            && !m_mountainSurface.empty())
        {
            if (m_right && jumpDown)
            {
                m_mountainProgress = (std::min)(MountainLength(),
                    m_mountainProgress + 205.0f * deltaTime);
                auto surfacePoint = MountainPointAt(m_mountainProgress);
                m_playerX = surfacePoint.x - PlayerHalfWidth;
                m_playerY = surfacePoint.y - PlayerHalfHeight;
                float mountainLength = MountainLength();
                if (m_mountainProgress >= mountainLength)
                {
                    m_climbing = false;
                    m_activeClimbRoute = -1;
                    m_playerX = surfacePoint.x + PlayerHalfWidth + 3.0f;
                    m_playerY = surfacePoint.y - PlayerHalfHeight;
                    m_grounded = true;
                    m_velocityX = 0.0f;
                    m_velocityY = 0.0f;
                    SpawnBurst(m_playerX, m_playerY + PlayerHalfHeight,
                        Color(225, 241, 247), 12);
                }
            }
            else
            {
                m_climbing = false;
                m_activeClimbRoute = -1;
            }
        }
        else if (m_climbing && m_climbWallPlatform >= 0
            && static_cast<size_t>(m_climbWallPlatform) < m_platforms.size())
        {
            auto const& wall = m_platforms[static_cast<size_t>(m_climbWallPlatform)];
            bool holdingWall = (m_climbWallSide < 0 && m_right)
                || (m_climbWallSide > 0 && m_left);
            if (holdingWall && jumpDown)
            {
                m_playerY -= 205.0f * deltaTime;
                m_velocityX = 0.0f;
                m_velocityY = 0.0f;
                if (m_playerY <= wall.y - PlayerHalfHeight)
                {
                    m_playerY = wall.y - PlayerHalfHeight;
                    m_playerX = m_climbWallSide < 0
                        ? wall.x + PlayerHalfWidth + 2.0f
                        : wall.x + wall.width - PlayerHalfWidth - 2.0f;
                    m_climbing = false;
                    m_activeClimbRoute = -1;
                    m_climbWallPlatform = -1;
                    m_grounded = true;
                    SpawnBurst(m_playerX, m_playerY + PlayerHalfHeight,
                        Color(211, 230, 199), 9);
                }
            }
            else
            {
                m_climbing = false;
                m_climbWallPlatform = -1;
            }
        }
        else if (!m_climbing)
        {
            m_playerX += m_velocityX * deltaTime;
            m_playerX = std::clamp(m_playerX, PlayerHalfWidth, m_worldWidth - PlayerHalfWidth);
            for (auto const& platform : m_platforms)
            {
                bool verticalOverlap = m_playerY + PlayerHalfHeight > platform.y + 2.0f
                    && m_playerY - PlayerHalfHeight < platform.y + platform.height - 2.0f;
                if (!verticalOverlap)
                {
                    continue;
                }
                if (oldPlayerX + PlayerHalfWidth <= platform.x
                    && m_playerX + PlayerHalfWidth > platform.x)
                {
                    m_playerX = platform.x - PlayerHalfWidth;
                    m_velocityX = 0.0f;
                }
                else if (oldPlayerX - PlayerHalfWidth >= platform.x + platform.width
                    && m_playerX - PlayerHalfWidth < platform.x + platform.width)
                {
                    m_playerX = platform.x + platform.width + PlayerHalfWidth;
                    m_velocityX = 0.0f;
                }
            }
            if (m_currentLevel == 4 && !m_mountainSurface.empty())
            {
                float boundaryX = m_mountainSurface.front().x;
                for (size_t i = 1; i < m_mountainSurface.size(); ++i)
                {
                    auto const& start = m_mountainSurface[i - 1];
                    auto const& end = m_mountainSurface[i];
                    if (m_playerY < end.y || m_playerY > start.y)
                    {
                        continue;
                    }
                    float fraction = (start.y - m_playerY) / (start.y - end.y);
                    boundaryX = start.x + (end.x - start.x) * fraction;
                    break;
                }
                if (m_playerX + PlayerHalfWidth > boundaryX
                    && m_playerY >= m_mountainSurface.back().y)
                {
                    m_playerX = boundaryX - PlayerHalfWidth;
                    m_velocityX = 0.0f;
                }
            }
            m_velocityY = (std::min)(m_velocityY + 1500.0f * deltaTime, 950.0f);
            m_playerY += m_velocityY * deltaTime;
            m_grounded = false;
            Platform const* landingPlatform = nullptr;
            if (m_velocityY >= 0.0f)
            {
                float feet = m_playerY + PlayerHalfHeight;
                for (auto const& platform : m_platforms)
                {
                    bool horizontalOverlap = m_playerX + PlayerHalfWidth > platform.x + 2.0f
                        && m_playerX - PlayerHalfWidth < platform.x + platform.width - 2.0f;
                    if (horizontalOverlap && oldFeet <= platform.y && feet >= platform.y
                        && (!landingPlatform || platform.y < landingPlatform->y))
                    {
                        landingPlatform = &platform;
                    }
                }
                if (landingPlatform)
                {
                    m_playerY = landingPlatform->y - PlayerHalfHeight;
                    m_velocityY = 0.0f;
                    m_grounded = true;
                    if (m_currentLevel == 4)
                    {
                        size_t landedOn = FindPlatformAt(m_playerX,
                            m_playerY + PlayerHalfHeight, 8.0f);
                        if (landedOn > m_checkpointPlatform
                            && landedOn < m_platforms.size())
                        {
                            m_checkpointPlatform = landedOn;
                            auto const& checkpoint = m_platforms[landedOn];
                            m_checkpointX = checkpoint.x + checkpoint.width * 0.12f;
                            m_checkpointY = checkpoint.y - PlayerHalfHeight;
                        }
                    }
                }
            }
            if (m_currentLevel == 4 && m_velocityY >= 0.0f
                && m_mountainSurface.size() >= 2)
            {
                float mountainY = m_mountainSurface.front().y;
                bool overMountain = false;
                for (size_t i = 1; i < m_mountainSurface.size(); ++i)
                {
                    auto const& start = m_mountainSurface[i - 1];
                    auto const& end = m_mountainSurface[i];
                    if (m_playerX + PlayerHalfWidth < start.x
                        || m_playerX - PlayerHalfWidth > end.x)
                    {
                        continue;
                    }
                    float contactX = std::clamp(m_playerX, start.x, end.x);
                    float fraction = (contactX - start.x) / (end.x - start.x);
                    mountainY = start.y + (end.y - start.y) * fraction;
                    overMountain = true;
                    break;
                }
                float feet = m_playerY + PlayerHalfHeight;
                if (overMountain && oldFeet <= mountainY && feet >= mountainY
                    && (!landingPlatform || mountainY < landingPlatform->y))
                {
                    m_playerY = mountainY - PlayerHalfHeight;
                    m_velocityY = 0.0f;
                    m_grounded = true;
                }
            }
        }

        if (m_currentLevel == 4)
        {
            float stormProgress = MountainProgressAt(
                m_playerX + PlayerHalfWidth, m_playerY + PlayerHalfHeight);
            bool inBlizzard = stormProgress >= MountainLength() * 0.78f;
            if (inBlizzard)
            {
                m_snowDamageTimer += deltaTime;
                if (m_snowDamageTimer >= 3.0f)
                {
                    m_snowDamageTimer -= 3.0f;
                    DamagePlayer(1.0f, 0.0f, true);
                    m_damageNumbers.push_back({ m_playerX, m_playerY - 36.0f,
                        0.8f, L"暴风雪 -1" });
                }
            }
            else
            {
                m_snowDamageTimer = 0.0f;
            }
        }
        if (m_playerY > height + 80.0f)
        {
            DamagePlayer(10.0f, 0.0f);
            if (m_playerHealth <= 0)
            {
                UpdateHud();
                EndGame(false);
                return;
            }
            if (m_currentLevel == 4 && m_checkpointPlatform > 0)
            {
                m_playerX = m_checkpointX;
                m_playerY = m_checkpointY;
            }
            else
            {
                m_playerX = (std::max)(oldPlayerX, width * 0.12f);
                m_playerX = (std::min)(m_playerX, width * 0.25f);
                m_playerY = m_platforms.front().y - PlayerHalfHeight;
            }
            m_velocityX = m_velocityY = 0.0f;
            m_grounded = true;
            m_invulnerability = 1.0f;
        }

        m_cameraX = std::clamp(m_playerX - width * 0.42f, 0.0f,
            (std::max)(0.0f, m_worldWidth - width));
        float topOfWorld = m_viewHeight;
        for (auto const& platform : m_platforms)
        {
            topOfWorld = (std::min)(topOfWorld, platform.y);
        }
        float cameraYMin = (std::min)(topOfWorld - height * 0.68f, 0.0f);
        float targetCameraY = std::clamp(m_playerY - height * 0.55f,
            cameraYMin, 0.0f);
        m_cameraY += (targetCameraY - m_cameraY)
            * (1.0f - std::exp(-7.0f * deltaTime));
        if (m_currentLevel == 2 && !m_machineGunUnlocked
            && !m_machineGunPickupCollected
            && std::abs(m_playerX - m_machineGunPickupX) < 58.0f
            && std::abs(m_playerY - m_machineGunPickupY) < 66.0f)
        {
            m_machineGunUnlocked = true;
            m_machineGunPickupCollected = true;
            m_machineGunAmmo = 50;
            m_machineGunPickupTime = 4.0f;
            m_damageNumbers.push_back({ m_playerX, m_playerY - 58.0f,
                2.2f, L"获得轻机枪！按 3 或滚轮切换" });
            SpawnBurst(m_playerX, m_playerY, Color(221, 235, 198), 28);
        }
        if (m_pointerActive)
        {
            float aimDX = m_pointerX + m_cameraX - m_playerX;
            float aimDY = m_pointerY + m_cameraY - (m_playerY - 2.0f);
            float length = std::sqrt(aimDX * aimDX + aimDY * aimDY);
            if (length > 1.0f)
            {
                m_aimX = aimDX / length;
                m_aimY = aimDY / length;
                if (std::abs(m_aimX) > 0.12f)
                {
                    m_facing = m_aimX > 0.0f ? 1 : -1;
                }
            }
        }
        else
        {
            m_aimX = static_cast<float>(m_facing);
            m_aimY = 0.0f;
        }

        float selectedReloadTimer = m_selectedWeapon == WeaponKind::Grenade
            ? m_grenadeReloadTimer : m_selectedWeapon == WeaponKind::MachineGun
                ? m_machineGunReloadTimer : m_pistolReloadTimer;
        if (m_mouseFiring && m_shotCooldown <= 0.0f && selectedReloadTimer <= 0.0f)
        {
            if (m_selectedWeapon == WeaponKind::Grenade && m_grenades > 0)
            {
                float originX = m_playerX + m_aimX * 28.0f;
                float originY = m_playerY - 5.0f + m_aimY * 28.0f;
                m_projectiles.push_back({ originX, originY, m_aimX * 500.0f,
                    m_aimY * 500.0f, 3.0f, 0.0f, GrenadeRadius, ProjectileKind::Grenade });
                --m_grenades;
                m_shotCooldown = 0.11f;
                m_grenadeReloadTimer = 3.0f;
                m_reloading = true;
                m_reloadWeapon = WeaponKind::Grenade;
                m_reloadDuration = m_grenadeReloadTimer;
                m_reloadTimer = m_grenadeReloadTimer;
                m_muzzleFlash = 0.075f;
                m_recoil = 1.0f;
                m_cameraShake = (std::max)(m_cameraShake, 4.0f);
                SpawnBurst(originX, originY, Color(255, 190, 104), 9);
                if (m_isTutorial && m_tutorialStep == 3)
                {
                    m_tutorialStep = 4;
                }
            }
            else if (m_selectedWeapon == WeaponKind::Pistol && m_pistolAmmo > 0)
            {
                float originX = m_playerX + m_aimX * 36.0f;
                float originY = m_playerY - 5.0f + m_aimY * 36.0f;
                m_projectiles.push_back({ originX, originY, m_aimX * 840.0f,
                    m_aimY * 840.0f, 1.4f, BulletDamage, 0.0f, ProjectileKind::Pistol });
                --m_pistolAmmo;
                ++m_pistolShotsSinceReload;
                m_shotCooldown = 0.11f;
                m_muzzleFlash = 0.055f;
                m_recoil = 1.0f;
                m_cameraShake = (std::max)(m_cameraShake, 1.3f);
                SpawnBurst(originX, originY, Color(255, 218, 145), 3);
                if (m_pistolShotsSinceReload >= 20)
                {
                    m_pistolReloadTimer = 3.0f;
                    m_reloading = true;
                    m_reloadWeapon = WeaponKind::Pistol;
                    m_reloadDuration = 3.0f;
                    m_reloadTimer = m_reloadDuration;
                }
            }
            else if (m_selectedWeapon == WeaponKind::MachineGun && m_machineGunAmmo > 0)
            {
                float originX = m_playerX + m_aimX * 40.0f;
                float originY = m_playerY - 5.0f + m_aimY * 40.0f;
                m_projectiles.push_back({ originX, originY, m_aimX * 1040.0f,
                    m_aimY * 1040.0f, 1.2f, 6.0f, 0.0f, ProjectileKind::MachineGun });
                --m_machineGunAmmo;
                m_shotCooldown = 0.075f;
                m_muzzleFlash = 0.048f;
                m_recoil = 0.8f;
                m_cameraShake = (std::max)(m_cameraShake, 1.8f);
                SpawnBurst(originX, originY, Color(230, 235, 196), 2);
                if (m_machineGunAmmo <= 0)
                {
                    m_machineGunReloadTimer = 10.0f;
                    m_reloading = true;
                    m_reloadWeapon = WeaponKind::MachineGun;
                    m_reloadDuration = 10.0f;
                    m_reloadTimer = m_reloadDuration;
                }
            }
        }

        if (m_currentLevel == 3 && m_playerX > m_worldWidth * 0.42f)
        {
            m_parachuterSpawnTimer += deltaTime;
            size_t activeDrops = static_cast<size_t>(std::count_if(
                m_enemies.begin(), m_enemies.end(),
                [](Enemy const& enemy) { return enemy.parachuting; }));
            if (m_parachuterSpawnTimer >= 6.5f && activeDrops < 3)
            {
                float dropX = (std::min)(m_worldWidth - width * 0.18f,
                    (std::max)(m_playerX + width * 0.50f, m_worldWidth * 0.60f));
                size_t landingPlatform = 0;
                float nearestDistance = (std::numeric_limits<float>::max)();
                for (size_t i = 0; i < m_platforms.size(); ++i)
                {
                    auto const& platform = m_platforms[i];
                    if (platform.height < 50.0f)
                    {
                        continue;
                    }
                    float distance = dropX < platform.x ? platform.x - dropX
                        : dropX > platform.x + platform.width
                            ? dropX - (platform.x + platform.width) : 0.0f;
                    if (distance < nearestDistance)
                    {
                        nearestDistance = distance;
                        landingPlatform = i;
                    }
                }
                auto const& landing = m_platforms[landingPlatform];
                float dropMinX = landing.x + 20.0f;
                float dropMaxX = landing.x + landing.width - 20.0f;
                if (dropMaxX < dropMinX)
                {
                    dropMinX = dropMaxX = landing.x + landing.width * 0.5f;
                }
                dropX = std::clamp(dropX, dropMinX, dropMaxX);
                SpawnEnemy(dropX, landingPlatform, 20.0f, false, EnemyKind::Parachuter);
                auto& drop = m_enemies.back();
                drop.y = m_cameraY - 55.0f;
                drop.verticalVelocity = 65.0f;
                drop.grounded = false;
                drop.parachuting = true;
                drop.fleeing = false;
                drop.speed = m_playerX < drop.x ? -145.0f : 145.0f;
                drop.attackCooldown = 0.0f;
                m_parachuterSpawnTimer = 0.0f;
            }
        }

        for (auto& enemy : m_enemies)
        {
            enemy.hitFlash = (std::max)(0.0f, enemy.hitFlash - deltaTime);
            enemy.displayedHealth += (enemy.health - enemy.displayedHealth)
                * (1.0f - std::exp(-13.0f * deltaTime));
            enemy.damageCooldown = (std::max)(0.0f, enemy.damageCooldown - deltaTime);
            enemy.attackCooldown -= deltaTime;
            float dx = m_playerX - enemy.x;
            float dy = (m_playerY - 5.0f) - enemy.y;
            float distance = std::sqrt(dx * dx + dy * dy);
            float enemyHalfHeight = enemy.kind == EnemyKind::FinalBoss ? 31.0f
                : enemy.kind == EnemyKind::MiniBoss ? 24.0f : 15.0f;
            size_t currentPlatform = FindPlatformAt(enemy.x,
                enemy.y + enemyHalfHeight, 28.0f);
            if (currentPlatform < m_platforms.size())
            {
                enemy.platformIndex = currentPlatform;
                enemy.minX = m_platforms[currentPlatform].x
                    + (enemy.kind == EnemyKind::FinalBoss ? 40.0f
                        : enemy.kind == EnemyKind::MiniBoss ? 31.0f : 19.0f);
                enemy.maxX = m_platforms[currentPlatform].x
                    + m_platforms[currentPlatform].width
                    - (enemy.kind == EnemyKind::FinalBoss ? 40.0f
                        : enemy.kind == EnemyKind::MiniBoss ? 31.0f : 19.0f);
                if (enemy.maxX < enemy.minX)
                {
                    enemy.minX = enemy.maxX = m_platforms[currentPlatform].x
                        + m_platforms[currentPlatform].width * 0.5f;
                }
            }

            bool incomingThreat = false;
            for (auto const& projectile : m_projectiles)
            {
                if (projectile.kind != ProjectileKind::Pistol
                    && projectile.kind != ProjectileKind::Grenade)
                {
                    continue;
                }
                float threatX = projectile.x - enemy.x;
                float threatY = projectile.y - enemy.y;
                if (threatX * threatX + threatY * threatY < 115.0f * 115.0f)
                {
                    incomingThreat = true;
                    break;
                }
            }
            enemy.fleeing = !(enemy.kind == EnemyKind::MiniBoss
                || enemy.kind == EnemyKind::FinalBoss
                || enemy.kind == EnemyKind::Parachuter
                || enemy.kind == EnemyKind::SnowWalker
                || enemy.kind == EnemyKind::SnowArtillery)
                && (enemy.health / enemy.maxHealth < 0.34f || incomingThreat);

            size_t playerPlatform = FindPlatformAt(m_playerX,
                m_playerY + PlayerHalfHeight, 34.0f);
            if (playerPlatform >= m_platforms.size())
            {
                playerPlatform = currentPlatform;
            }
            size_t nextPlatform = currentPlatform;
            auto canJumpBetween = [this, &enemy](size_t from, size_t to)
            {
                if (from >= m_platforms.size() || to >= m_platforms.size() || from == to)
                {
                    return false;
                }
                float jumpSpeed = enemy.kind == EnemyKind::MiniBoss
                    || enemy.kind == EnemyKind::FinalBoss ? 480.0f : 555.0f;
                constexpr float gravity = 1350.0f;
                auto const& source = m_platforms[from];
                auto const& destination = m_platforms[to];
                float rise = source.y - destination.y;
                float discriminant = jumpSpeed * jumpSpeed - 2.0f * gravity * rise;
                if (discriminant < 0.0f)
                {
                    return false;
                }
                float landingTime = (jumpSpeed + std::sqrt(discriminant)) / gravity;
                float horizontalSpeed = enemy.kind == EnemyKind::FinalBoss ? 95.0f
                    : enemy.kind == EnemyKind::MiniBoss ? 72.0f
                    : enemy.kind == EnemyKind::Armored ? 58.0f
                    : enemy.kind == EnemyKind::Gunner ? 68.0f
                    : enemy.kind == EnemyKind::Bomber ? 82.0f : 105.0f;
                if (enemy.fleeing)
                {
                    horizontalSpeed *= 1.45f;
                }
                float gap = (std::max)(0.0f,
                    (std::max)(destination.x - (source.x + source.width),
                        source.x - (destination.x + destination.width)));
                return gap <= horizontalSpeed * landingTime;
            };
            if (currentPlatform < m_platforms.size()
                && playerPlatform < m_platforms.size()
                && currentPlatform != playerPlatform)
            {
                std::vector<int32_t> previous(m_platforms.size(), -1);
                std::queue<size_t> open;
                previous[currentPlatform] = static_cast<int32_t>(currentPlatform);
                open.push(currentPlatform);
                while (!open.empty() && previous[playerPlatform] < 0)
                {
                    size_t from = open.front();
                    open.pop();
                    for (size_t to = 0; to < m_platforms.size(); ++to)
                    {
                        if (previous[to] >= 0 || to == from)
                        {
                            continue;
                        }
                        if (!canJumpBetween(from, to))
                        {
                            continue;
                        }
                        previous[to] = static_cast<int32_t>(from);
                        open.push(to);
                    }
                }

                if (previous[playerPlatform] >= 0)
                {
                    size_t step = playerPlatform;
                    while (static_cast<size_t>(previous[step]) != currentPlatform
                        && step != currentPlatform)
                    {
                        step = static_cast<size_t>(previous[step]);
                    }
                    nextPlatform = step;
                }
            }
            if (enemy.fleeing && currentPlatform < m_platforms.size())
            {
                float bestEscapeDistance = distance;
                for (size_t i = 0; i < m_platforms.size(); ++i)
                {
                    auto const& destination = m_platforms[i];
                    float escapeDistance = std::abs(
                        destination.x + destination.width * 0.5f - m_playerX);
                    if (canJumpBetween(currentPlatform, i)
                        && escapeDistance > bestEscapeDistance)
                    {
                        nextPlatform = i;
                        bestEscapeDistance = escapeDistance;
                    }
                }
            }

            float direction = 0.0f;
            bool boss = enemy.kind == EnemyKind::MiniBoss
                || enemy.kind == EnemyKind::FinalBoss;
            bool ranged = enemy.kind == EnemyKind::Gunner
                || enemy.kind == EnemyKind::Bomber
                || enemy.kind == EnemyKind::SnowArtillery;
            if (enemy.fleeing)
            {
                direction = m_playerX < enemy.x ? 1.0f : -1.0f;
                if (nextPlatform != currentPlatform && nextPlatform < m_platforms.size())
                {
                    float nextCenter = m_platforms[nextPlatform].x
                        + m_platforms[nextPlatform].width * 0.5f;
                    direction = nextCenter < enemy.x ? -1.0f : 1.0f;
                }
            }
            else if (currentPlatform != playerPlatform && nextPlatform < m_platforms.size())
            {
                float target = m_platforms[nextPlatform].x
                    + m_platforms[nextPlatform].width * 0.5f;
                direction = target < enemy.x - 12.0f ? -1.0f
                    : target > enemy.x + 12.0f ? 1.0f : 0.0f;
            }
            else if (ranged && distance < (enemy.kind == EnemyKind::Gunner ? 290.0f : 175.0f))
            {
                direction = m_playerX < enemy.x ? 1.0f : -1.0f;
            }
            else if (distance > (ranged ? 250.0f : boss ? 95.0f : 44.0f))
            {
                direction = m_playerX < enemy.x ? -1.0f : 1.0f;
            }

            bool engaged = distance < 760.0f;
            if (!engaged && !enemy.fleeing)
            {
                direction = enemy.speed >= 0.0f ? 1.0f : -1.0f;
            }
            if (!enemy.grounded)
            {
                direction = enemy.speed == 0.0f
                    ? (m_playerX < enemy.x ? 1.0f : -1.0f)
                    : (enemy.speed > 0.0f ? 1.0f : -1.0f);
            }
            float enemySpeed = enemy.kind == EnemyKind::Parachuter ? 145.0f
                : enemy.kind == EnemyKind::FinalBoss ? 95.0f
                : enemy.kind == EnemyKind::MiniBoss ? 72.0f
                : enemy.kind == EnemyKind::Armored ? 58.0f : 105.0f;
            if (ranged)
            {
                enemySpeed = enemy.kind == EnemyKind::Gunner ? 68.0f : 82.0f;
            }
            if (enemy.fleeing)
            {
                enemySpeed *= 1.45f;
            }
            enemy.speed = direction * enemySpeed;
            float oldEnemyFeet = enemy.y + enemyHalfHeight;
            enemy.x += enemy.speed * deltaTime;
            if (enemy.grounded)
            {
                enemy.x = std::clamp(enemy.x, enemy.minX, enemy.maxX);
            }
            else
            {
                enemy.x = std::clamp(enemy.x, 20.0f, m_worldWidth - 20.0f);
            }
            enemy.verticalVelocity = enemy.parachuting
                ? (std::min)(enemy.verticalVelocity + 180.0f * deltaTime, 90.0f)
                : (std::min)(enemy.verticalVelocity + 1350.0f * deltaTime, 900.0f);
            enemy.y += enemy.verticalVelocity * deltaTime;
            enemy.grounded = false;

            if (enemy.verticalVelocity >= 0.0f)
            {
                Platform const* landing = nullptr;
                for (auto const& platform : m_platforms)
                {
                    bool overlap = enemy.x + 14.0f > platform.x
                        && enemy.x - 14.0f < platform.x + platform.width;
                    if (overlap && oldEnemyFeet <= platform.y
                        && enemy.y + enemyHalfHeight >= platform.y
                        && (!landing || platform.y < landing->y))
                    {
                        landing = &platform;
                    }
                }
                if (landing)
                {
                    enemy.y = landing->y - enemyHalfHeight;
                    enemy.verticalVelocity = 0.0f;
                    enemy.grounded = true;
                    if (enemy.parachuting)
                    {
                        enemy.parachuting = false;
                        enemy.speed = m_playerX < enemy.x ? -145.0f : 145.0f;
                        enemy.attackCooldown = 0.0f;
                    }
                    size_t landedOn = FindPlatformAt(enemy.x,
                        enemy.y + enemyHalfHeight, 8.0f);
                    if (landedOn < m_platforms.size())
                    {
                        enemy.platformIndex = landedOn;
                        enemy.minX = m_platforms[landedOn].x + 18.0f;
                        enemy.maxX = m_platforms[landedOn].x
                            + m_platforms[landedOn].width - 18.0f;
                    }
                }
            }
            if (enemy.grounded && currentPlatform < m_platforms.size()
                && (nextPlatform != currentPlatform || enemy.fleeing))
            {
                auto const& platform = m_platforms[currentPlatform];
                float edgeDistance = direction > 0.0f
                    ? platform.x + platform.width - enemy.x
                    : enemy.x - platform.x;
                if ((edgeDistance < 62.0f || direction == 0.0f)
                    && nextPlatform != currentPlatform)
                {
                    enemy.verticalVelocity = boss ? -480.0f : -555.0f;
                    enemy.grounded = false;
                }
            }

            float attackRange = enemy.kind == EnemyKind::FinalBoss ? 760.0f
                : enemy.kind == EnemyKind::MiniBoss ? 620.0f
                : enemy.kind == EnemyKind::Gunner ? 650.0f
                : enemy.kind == EnemyKind::SnowArtillery ? 690.0f
                : enemy.kind == EnemyKind::SnowWalker ? 610.0f
                : enemy.kind == EnemyKind::Bomber
                    ? (m_currentLevel == 2 ? 760.0f : 350.0f) : 470.0f;
            if (boss && enemy.stompWindup > 0.0f)
            {
                enemy.stompWindup = (std::max)(0.0f, enemy.stompWindup - deltaTime);
                if (enemy.stompWindup <= 0.0f)
                {
                    auto const& platform = m_platforms[enemy.platformIndex];
                    float waveY = platform.y - 7.0f;
                    for (float waveDirection : { -1.0f, 1.0f })
                    {
                        m_projectiles.push_back({ enemy.x + waveDirection * 30.0f, waveY,
                            waveDirection * (enemy.kind == EnemyKind::FinalBoss ? 430.0f : 350.0f),
                            0.0f, 2.2f, enemy.kind == EnemyKind::FinalBoss ? 12.0f : 9.0f,
                            0.0f, ProjectileKind::Shockwave });
                    }
                    SpawnBurst(enemy.x, platform.y, Color(255, 115, 77), 25);
                    m_cameraShake = (std::max)(m_cameraShake,
                        enemy.kind == EnemyKind::FinalBoss ? 12.0f : 8.0f);
                    enemy.anger = 0.0f;
                }
            }
            if (boss && enemy.attackCooldown <= 0.0f && distance < 235.0f
                && enemy.stompWindup <= 0.0f)
            {
                enemy.stompWindup = 0.82f;
                enemy.attackCooldown = enemy.kind == EnemyKind::FinalBoss ? 2.7f : 3.2f;
                m_damageNumbers.push_back({ enemy.x, enemy.y - 58.0f, 0.85f, L"震地预警！" });
            }
            else if (boss && enemy.anger >= 1.0f && distance < 520.0f
                && enemy.stompWindup <= 0.0f)
            {
                enemy.stompWindup = 0.58f;
                enemy.attackCooldown = 3.4f;
                enemy.anger = 0.0f;
                m_damageNumbers.push_back({ enemy.x, enemy.y - 58.0f, 0.65f, L"震怒猛击！" });
            }
            if (enemy.attackCooldown > 0.0f || distance > attackRange
                || enemy.kind == EnemyKind::Walker || enemy.kind == EnemyKind::Armored
                || enemy.kind == EnemyKind::Parachuter
                || (boss && enemy.stompWindup > 0.0f))
            {
                continue;
            }

            float aimAngle = std::atan2(dy, dx);
            auto fire = [this, &enemy](float angle, float speed, float damage,
                ProjectileKind kind)
            {
                m_projectiles.push_back({ enemy.x, enemy.y - 7.0f,
                    std::cos(angle) * speed, std::sin(angle) * speed,
                    4.0f, damage,
                    kind == ProjectileKind::EnemyBomb ? 104.0f
                    : kind == ProjectileKind::SnowBomb ? 88.0f
                    : kind == ProjectileKind::BossOrb ? 88.0f : 0.0f, kind });
            };

            switch (enemy.kind)
            {
            case EnemyKind::Bomber:
                fire(aimAngle, 270.0f, 8.0f, ProjectileKind::EnemyBomb);
                enemy.attackCooldown = 3.1f;
                break;
            case EnemyKind::Gunner:
                fire(aimAngle, 410.0f, 5.0f, ProjectileKind::EnemyBullet);
                enemy.attackCooldown = 1.65f;
                break;
            case EnemyKind::SnowWalker:
                fire(aimAngle, 290.0f, 3.0f, ProjectileKind::Snowball);
                enemy.attackCooldown = 3.0f;
                break;
            case EnemyKind::SnowArtillery:
                fire(aimAngle, 235.0f, 7.0f, ProjectileKind::SnowBomb);
                enemy.attackCooldown = 3.0f;
                break;
            case EnemyKind::MiniBoss:
                for (int spread = -1; spread <= 1; ++spread)
                {
                    fire(aimAngle + static_cast<float>(spread) * 0.22f,
                        365.0f, 7.0f, ProjectileKind::BossOrb);
                }
                enemy.attackCooldown = 2.25f;
                m_cameraShake = (std::max)(m_cameraShake, 1.5f);
                break;
            case EnemyKind::FinalBoss:
                if (static_cast<int>(m_elapsed / 4.2f) % 2 == 0)
                {
                    for (int spread = -2; spread <= 2; ++spread)
                    {
                        fire(aimAngle + static_cast<float>(spread) * 0.19f,
                            430.0f, 8.0f, ProjectileKind::BossOrb);
                    }
                    enemy.attackCooldown = 1.2f;
                }
                else
                {
                    for (int shot = 0; shot < 8; ++shot)
                    {
                        fire(static_cast<float>(shot) * (Pi / 4.0f)
                            + m_elapsed * 0.35f, 300.0f, 7.0f, ProjectileKind::BossOrb);
                    }
                    enemy.attackCooldown = 2.4f;
                    m_cameraShake = (std::max)(m_cameraShake, 3.0f);
                }
                break;
            default:
                break;
            }
        }

        for (size_t projectileIndex = 0; projectileIndex < m_projectiles.size();)
        {
            auto& projectile = m_projectiles[projectileIndex];
            const float previousX = projectile.x;
            const float previousY = projectile.y;
            const bool hostile = projectile.kind == ProjectileKind::EnemyBullet
                || projectile.kind == ProjectileKind::EnemyBomb
                || projectile.kind == ProjectileKind::Snowball
                || projectile.kind == ProjectileKind::SnowBomb
                || projectile.kind == ProjectileKind::BossOrb;
            const bool explosive = projectile.kind == ProjectileKind::Grenade
                || projectile.kind == ProjectileKind::EnemyBomb
                || projectile.kind == ProjectileKind::SnowBomb
                || projectile.kind == ProjectileKind::BossOrb;
            if (projectile.kind == ProjectileKind::Grenade
                || projectile.kind == ProjectileKind::EnemyBomb
                || projectile.kind == ProjectileKind::SnowBomb)
            {
                projectile.vy += 850.0f * deltaTime;
            }
            projectile.x += projectile.vx * deltaTime;
            projectile.y += projectile.vy * deltaTime;
            projectile.life -= deltaTime;

            bool impact = false;
            bool hitPlayer = false;
            size_t hitEnemyIndex = m_enemies.size();
            float impactX = projectile.x;
            float impactY = projectile.y;
            float travelX = projectile.x - previousX;
            float travelY = projectile.y - previousY;
            auto recordMissedBossShot = [this, &projectile, previousX, previousY,
                travelX, travelY, hostile](size_t hitIndex)
            {
                if (hostile || (projectile.kind != ProjectileKind::Pistol
                    && projectile.kind != ProjectileKind::Grenade))
                {
                    return;
                }
                float segmentLengthSquared = travelX * travelX + travelY * travelY;
                for (size_t i = 0; i < m_enemies.size(); ++i)
                {
                    auto& bossEnemy = m_enemies[i];
                    if (i == hitIndex || (bossEnemy.kind != EnemyKind::MiniBoss
                        && bossEnemy.kind != EnemyKind::FinalBoss))
                    {
                        continue;
                    }
                    float fraction = segmentLengthSquared > 0.0f
                        ? std::clamp(((bossEnemy.x - previousX) * travelX
                            + (bossEnemy.y - previousY) * travelY) / segmentLengthSquared,
                            0.0f, 1.0f) : 0.0f;
                    float nearestX = previousX + travelX * fraction;
                    float nearestY = previousY + travelY * fraction;
                    float missX = bossEnemy.x - nearestX;
                    float missY = bossEnemy.y - nearestY;
                    if (missX * missX + missY * missY < 92.0f * 92.0f)
                    {
                        bossEnemy.anger = (std::min)(1.5f,
                            bossEnemy.anger + (projectile.kind == ProjectileKind::Grenade
                                ? 0.48f : 0.22f));
                    }
                }
            };
            int sampleCount = (std::max)(1, static_cast<int>(
                std::ceil(std::sqrt(travelX * travelX + travelY * travelY) / 5.0f)));
            float projectileRadius = explosive ? 8.0f : 3.0f;
            for (int sample = 1; sample <= sampleCount && !impact; ++sample)
            {
                float fraction = static_cast<float>(sample) / static_cast<float>(sampleCount);
                float sampleX = previousX + travelX * fraction;
                float sampleY = previousY + travelY * fraction;
                if (hostile
                    && std::abs(sampleX - m_playerX) < PlayerHalfWidth + projectileRadius
                    && std::abs(sampleY - m_playerY) < PlayerHalfHeight + projectileRadius)
                {
                    impact = true;
                    hitPlayer = true;
                }
                else if (!hostile)
                {
                    for (size_t i = 0; i < m_enemies.size(); ++i)
                    {
                        auto const& enemy = m_enemies[i];
                        float hitRadius = enemy.kind == EnemyKind::FinalBoss ? 52.0f
                            : enemy.kind == EnemyKind::MiniBoss ? 39.0f : 19.0f;
                        float dx = enemy.x - sampleX;
                        float dy = enemy.y - sampleY;
                        if (dx * dx + dy * dy < hitRadius * hitRadius)
                        {
                            impact = true;
                            hitEnemyIndex = i;
                            break;
                        }
                    }
                }
                if (impact)
                {
                    impactX = sampleX;
                    impactY = sampleY;
                    break;
                }
                for (auto const& platform : m_platforms)
                {
                    if (projectile.kind == ProjectileKind::Shockwave)
                    {
                        break;
                    }
                    if (sampleX + projectileRadius >= platform.x
                        && sampleX - projectileRadius <= platform.x + platform.width
                        && sampleY + projectileRadius >= platform.y
                        && sampleY - projectileRadius <= platform.y + platform.height)
                    {
                        impact = true;
                        impactX = sampleX;
                        impactY = sampleY;
                        break;
                    }
                }
            }

            if (impact)
            {
                recordMissedBossShot(hitEnemyIndex);
                if (hitPlayer)
                {
                    if (explosive)
                    {
                        Explode(impactX, impactY, true, projectile.damage,
                            projectile.blastRadius);
                    }
                    else
                    {
                        DamagePlayer(projectile.damage,
                            m_playerX < impactX ? -220.0f : 220.0f);
                    }
                }
                else if (hitEnemyIndex < m_enemies.size())
                {
                    if (explosive)
                    {
                        Explode(impactX, impactY);
                    }
                    else
                    {
                        DamageEnemy(hitEnemyIndex, projectile.damage, false);
                        SpawnBurst(impactX, impactY, Color(255, 233, 190), 4);
                    }
                }
                else if (explosive)
                {
                    Explode(impactX, impactY, hostile, projectile.damage,
                        projectile.blastRadius);
                }
                else
                {
                    SpawnBurst(impactX, impactY, Color(255, 233, 190), 3);
                }
                m_projectiles.erase(m_projectiles.begin() + projectileIndex);
                continue;
            }
            if (projectile.life <= 0.0f || projectile.x < 0.0f
                || projectile.x > m_worldWidth || projectile.y > height + 60.0f
                || projectile.y < -60.0f)
            {
                recordMissedBossShot(m_enemies.size());
                if (explosive && projectile.life <= 0.0f)
                {
                    Explode(projectile.x, projectile.y, hostile, projectile.damage,
                        projectile.blastRadius);
                }
                m_projectiles.erase(m_projectiles.begin() + projectileIndex);
                continue;
            }
            ++projectileIndex;
        }

        if (m_invulnerability <= 0.0f)
        {
            auto enemy = std::find_if(m_enemies.begin(), m_enemies.end(),
                [this](Enemy const& candidate)
                {
                    float halfWidth = candidate.kind == EnemyKind::FinalBoss ? 38.0f
                        : candidate.kind == EnemyKind::MiniBoss ? 29.0f : 14.0f;
                    float halfHeight = candidate.kind == EnemyKind::FinalBoss ? 31.0f
                        : candidate.kind == EnemyKind::MiniBoss ? 24.0f : 12.0f;
                    return std::abs(candidate.x - m_playerX) < PlayerHalfWidth + halfWidth
                        && std::abs(candidate.y - m_playerY)
                            < PlayerHalfHeight + halfHeight;
                });
            if (enemy != m_enemies.end())
            {
                float damage = enemy->kind == EnemyKind::FinalBoss ? 12.0f
                    : enemy->kind == EnemyKind::MiniBoss ? 8.0f : 5.0f;
                DamagePlayer(damage, m_playerX < enemy->x ? -270.0f : 270.0f);
                if (m_playerHealth <= 0)
                {
                    UpdateHud();
                    EndGame(false);
                    return;
                }
            }
        }

        if (m_playerHealth <= 0)
        {
            UpdateHud();
            EndGame(false);
            return;
        }

        for (auto& particle : m_particles)
        {
            particle.x += particle.vx * deltaTime;
            particle.y += particle.vy * deltaTime;
            particle.vy += 420.0f * deltaTime;
            particle.vx *= 0.985f;
            particle.life -= deltaTime;
            particle.rotation += deltaTime * 420.0f;
        }
        m_particles.erase(std::remove_if(m_particles.begin(), m_particles.end(),
            [](Particle const& particle) { return particle.life <= 0.0f; }),
            m_particles.end());
        for (auto& number : m_damageNumbers)
        {
            number.y -= 30.0f * deltaTime;
            number.life -= deltaTime;
        }
        m_damageNumbers.erase(std::remove_if(m_damageNumbers.begin(), m_damageNumbers.end(),
            [](DamageNumber const& number) { return number.life <= 0.0f; }),
            m_damageNumbers.end());
        for (auto& explosion : m_explosions)
        {
            explosion.life -= deltaTime;
        }
        m_explosions.erase(std::remove_if(m_explosions.begin(), m_explosions.end(),
            [](Explosion const& explosion) { return explosion.life <= 0.0f; }),
            m_explosions.end());

        CheckTutorialProgress();
        if (!m_gameRunning)
        {
            return;
        }
        const float exitCenterX = m_worldWidth - 50.0f;
        const float exitLeft = exitCenterX - 44.0f;
        const float exitTop = m_exitY - 38.0f;
        const float exitBottom = m_exitY + 38.0f;
        const bool touchingExit =
            m_playerX + PlayerHalfWidth >= exitLeft
            && m_playerX - PlayerHalfWidth <= exitCenterX + 44.0f
            && m_playerY + PlayerHalfHeight >= exitTop
            && m_playerY - PlayerHalfHeight <= exitBottom;
        if (touchingExit)
        {
            if (m_isTutorial)
            {
                m_tutorialComplete = true;
            }
            EndGame(true);
            return;
        }
        UpdateHud();
        Render();
    }

    void MainWindow::DamageEnemy(size_t index, float damage, bool grenadeDamage)
    {
        if (index >= m_enemies.size())
        {
            return;
        }
        auto& enemy = m_enemies[index];
        if (enemy.kind == EnemyKind::Armored && !grenadeDamage)
        {
            enemy.hitFlash = 0.12f;
            m_damageNumbers.push_back({ enemy.x, enemy.y - 22.0f, 0.6f, L"装甲免疫" });
            SpawnBurst(enemy.x, enemy.y, Color(181, 198, 208), 6);
            return;
        }
        enemy.health = (std::max)(0.0f, enemy.health - damage);
        enemy.hitFlash = 0.16f;
        m_damageNumbers.push_back({ enemy.x, enemy.y - 22.0f, 0.7f,
            L"-" + std::to_wstring((std::max)(1, static_cast<int>(std::round(damage)))) });
        SpawnBurst(enemy.x, enemy.y, Color(255, 211, 145), 5);
        if (enemy.health > 0.0f)
        {
            return;
        }

        bool tutorialTarget = enemy.grenadeTutorialTarget;
        bool boss = enemy.kind == EnemyKind::MiniBoss || enemy.kind == EnemyKind::FinalBoss;
        m_grenades += boss ? (enemy.kind == EnemyKind::FinalBoss ? 5 : 3) : 1;
        m_score += boss ? 1000 : 100;
        m_cameraShake = (std::max)(m_cameraShake, 3.4f);
        SpawnBurst(enemy.x, enemy.y, boss ? Color(255, 190, 104) : Color(197, 244, 122),
            boss ? 42 : 17);
        m_explosions.push_back({ enemy.x, enemy.y, 0.22f, false, 82.0f });
        m_enemies.erase(m_enemies.begin() + index);
        if (m_isTutorial && m_tutorialStep == 1)
        {
            m_tutorialStep = 2;
        }
        if (m_isTutorial && tutorialTarget && grenadeDamage)
        {
            m_tutorialStep = 5;
        }
    }

    void MainWindow::Explode(float x, float y, bool hostile, float damage, float radius)
    {
        m_explosions.push_back({ x, y, 0.34f, hostile,
            hostile ? (radius > 0.0f ? radius : 88.0f) : GrenadeRadius });
        if (hostile)
        {
            float blastRadius = radius > 0.0f ? radius : 88.0f;
            float dx = m_playerX - x;
            float dy = m_playerY - y;
            float distance = std::sqrt(dx * dx + dy * dy);
            if (distance <= blastRadius && m_invulnerability <= 0.0f)
            {
                DamagePlayer(damage * (1.0f - 0.55f * distance / blastRadius),
                    m_playerX < x ? -260.0f : 260.0f);
            }
            SpawnBurst(x, y, Color(255, 106, 92), 22);
            m_cameraShake = (std::max)(m_cameraShake, 7.0f);
            return;
        }
        if (m_isTutorial && m_tutorialStep == 4)
        {
            m_tutorialStep = 5;
        }
        SpawnBurst(x, y, Color(255, 190, 90), 28);
        SpawnBurst(x, y, Color(255, 235, 179), 13);
        m_cameraShake = (std::max)(m_cameraShake, 10.0f);
        for (size_t i = m_enemies.size(); i > 0; --i)
        {
            auto const& enemy = m_enemies[i - 1];
            float dx = enemy.x - x;
            float dy = enemy.y - y;
            float distance = std::sqrt(dx * dx + dy * dy);
            if (distance <= GrenadeRadius)
            {
                float falloffDamage = 42.0f * (1.0f - 0.65f * distance / GrenadeRadius);
                DamageEnemy(i - 1, falloffDamage, true);
            }
        }
    }

    void MainWindow::DamagePlayer(float damage, float knockbackX, bool ignoreInvulnerability)
    {
        if ((!ignoreInvulnerability && m_invulnerability > 0.0f) || damage <= 0.0f)
        {
            return;
        }
        int32_t appliedDamage = (std::max)(1, static_cast<int32_t>(std::round(damage)));
        m_playerHealth = (std::max)(0, m_playerHealth - appliedDamage);
        if (!ignoreInvulnerability)
        {
            m_invulnerability = 1.05f;
        }
        if (knockbackX != 0.0f)
        {
            m_climbing = false;
            m_activeClimbRoute = -1;
            m_climbWallPlatform = -1;
            m_velocityX = knockbackX;
            m_velocityY = -300.0f;
            m_grounded = false;
        }
        m_cameraShake = (std::max)(m_cameraShake, 8.0f);
        SpawnBurst(m_playerX, m_playerY, Color(255, 137, 108), 16);
    }

    void MainWindow::CheckTutorialProgress()
    {
        if (!m_isTutorial)
        {
            return;
        }
        float width = static_cast<float>(Arena().ActualWidth());
        if (m_tutorialStep == 0 && m_playerX > width * 0.24f)
        {
            m_tutorialStep = 1;
        }
        else if (m_tutorialStep == 2 && m_grenades > 0
            && m_selectedWeapon == WeaponKind::Grenade)
        {
            m_tutorialStep = 3;
        }
    }

    void MainWindow::UpdateHud()
    {
        std::wostringstream score;
        score << std::setw(6) << std::setfill(L'0') << m_score;
        ScoreText().Text(hstring{ score.str() });
        HealthText().Text(to_hstring(m_playerHealth) + L" / 40");
        HealthBarFill().Width(104.0 * static_cast<double>(m_playerHealth) / 40.0);
        HealthBarFill().Background(SolidColorBrush{ m_playerHealth <= 12
            ? Color(255, 119, 96) : Color(184, 243, 106) });
        WeaponText().Text(m_selectedWeapon == WeaponKind::Grenade ? L"02  榴弹炮"
            : m_selectedWeapon == WeaponKind::MachineGun ? L"03  轻机枪"
            : L"01  手枪");
        GrenadeText().Text(L"榴弹 × " + to_hstring(m_grenades));
        AmmoText().Text(m_selectedWeapon == WeaponKind::Grenade ? L"单发装填"
            : m_selectedWeapon == WeaponKind::MachineGun
                ? to_hstring(m_machineGunAmmo) + L" / 50"
                : to_hstring(m_pistolAmmo) + L" / 20");
        ReloadText().Text(m_reloading
            ? (m_reloadWeapon == WeaponKind::Grenade ? L"装填榴弹"
                : m_reloadWeapon == WeaponKind::MachineGun ? L"机枪换弹" : L"换弹中")
            : L"");
        GrenadeText().Foreground(SolidColorBrush{ m_selectedWeapon == WeaponKind::Grenade
            ? Color(255, 210, 135) : Color(215, 242, 174) });
    }

    void MainWindow::ShowSelection()
    {
        m_timer.Stop();
        m_endSequence = EndSequence::None;
        m_sequenceTime = 0.0f;
        m_gameRunning = false;
        m_paused = false;
        m_mouseFiring = false;
        ClearMovementInput(true);
        Overlay().Visibility(Visibility::Collapsed);
        Overlay().Opacity(1.0);
        OverlayCardTranslate().Y(0.0);
        MessagePanel().Visibility(Visibility::Collapsed);
        GameControls().Visibility(Visibility::Collapsed);
        WeaponHud().Visibility(Visibility::Collapsed);
        GamePage().Visibility(Visibility::Visible);
        SelectionPageTranslate().X(0.0);
        SelectionContentTranslate().X(0.0);
        SelectionPage().Visibility(Visibility::Visible);

        if (m_tutorialComplete)
        {
            SelectionEyebrow().Text(L"CHAPTER SELECT");
            SelectionTitle().Text(L"选择行动区域");
            SelectionDescription().Text(L"十个区域，各有不同敌人与地形。第五关和第十关有头目战。");
            TutorialButton().Content(box_value(L"重玩教程"));
            SelectionHint().Text(L"A / D 移动　空格跳跃　鼠标瞄准　左键射击　·　1 / 2 / 3 切换　R 换弹");
        }
        else
        {
            SelectionEyebrow().Text(L"SIDEQUEST / CHAPTER SELECT");
            SelectionTitle().Text(L"选择行动区域");
            SelectionDescription().Text(L"可以先进入练习场，也可以直接选择任意一关。每关从头开始。");
            TutorialButton().Content(box_value(L"练习场  ·  移动 / 射击 / 榴弹"));
            SelectionHint().Text(L"A / D 移动　空格跳跃　鼠标瞄准　左键射击　·　1 / 2 / 3 切换　R 换弹");
        }
        GamePage().Visibility(Visibility::Visible);
    }

    void MainWindow::TogglePause()
    {
        if (!m_gameRunning)
        {
            return;
        }
        m_paused = !m_paused;
        m_mouseFiring = false;
        ClearMovementInput(true);
        if (m_paused)
        {
            m_timer.Stop();
            MessagePanel().Visibility(Visibility::Visible);
            MessageTitle().Text(L"游戏已暂停");
            MessageDescription().Text(L"休息一下，准备好了再继续。");
            ResumeButton().Content(box_value(L"继续游戏"));
            Overlay().Visibility(Visibility::Visible);
            PauseButton().Content(box_value(hstring{ L"\xE768" }));
        }
        else
        {
            Overlay().Visibility(Visibility::Collapsed);
            m_lastFrame = std::chrono::steady_clock::now();
            m_timer.Start();
            PauseButton().Content(box_value(hstring{ L"\xE769" }));
        }
    }

    void MainWindow::ResetCurrentLevel()
    {
        StartGame(m_isTutorial, m_currentLevel);
    }

    void MainWindow::EndGame(bool victory)
    {
        m_gameRunning = false;
        m_paused = false;
        m_mouseFiring = false;
        ClearMovementInput(true);
        m_sequenceTime = 0.0f;
        if (victory)
        {
            m_victoryStartX = m_playerX;
            m_victoryStartY = m_playerY;
            m_endSequence = EndSequence::Victory;
            GameControls().Visibility(Visibility::Collapsed);
            WeaponHud().Visibility(Visibility::Collapsed);
            m_timer.Start();
            m_lastFrame = std::chrono::steady_clock::now();
            Render();
            return;
        }
        m_playerDead = true;
        m_endSequence = EndSequence::Death;
        m_cameraShake = (std::max)(m_cameraShake, 8.0f);
        Overlay().Opacity(0.0);
        Overlay().Visibility(Visibility::Collapsed);
        MessagePanel().Visibility(Visibility::Collapsed);
        MessageTitle().Text(L"这次先撤退");
        MessageDescription().Text(L"体力耗尽。重整一下，再从本关起点出发。");
        ResumeButton().Content(box_value(L"重试本关"));
        ReturnToSelectionButton().Content(box_value(L"返回选关"));
        m_timer.Start();
        m_lastFrame = std::chrono::steady_clock::now();
        Render();
    }

    void MainWindow::Render()
    {
        GameCanvas().Invalidate();
    }

    void MainWindow::DrawGame(
        Microsoft::Graphics::Canvas::UI::Xaml::CanvasControl const& sender,
        Microsoft::Graphics::Canvas::UI::Xaml::CanvasDrawEventArgs const& args)
    {
        auto width = static_cast<float>(sender.ActualWidth());
        auto height = static_cast<float>(sender.ActualHeight());
        if (width <= 0.0f || height <= 0.0f || m_platforms.empty())
        {
            return;
        }
        auto drawing = args.DrawingSession();
        m_screenShakeX = std::sin(m_elapsed * 73.0f) * m_cameraShake;
        m_screenShakeY = std::cos(m_elapsed * 61.0f) * m_cameraShake * 0.55f;
        auto screenX = [this](float x)
        {
            return x - m_cameraX + m_screenShakeX;
        };
        auto screenY = [this](float y)
        {
            return y - m_cameraY + m_screenShakeY;
        };
        auto blendColor = [](Windows::UI::Color from, Windows::UI::Color to, float amount)
        {
            amount = std::clamp(amount, 0.0f, 1.0f);
            return Color(
                static_cast<uint8_t>(from.R + (to.R - from.R) * amount),
                static_cast<uint8_t>(from.G + (to.G - from.G) * amount),
                static_cast<uint8_t>(from.B + (to.B - from.B) * amount),
                static_cast<uint8_t>(from.A + (to.A - from.A) * amount));
        };
        float centerSnowBlend = m_currentLevel == 4 ? 1.0f
            : m_currentLevel == 3
                ? std::clamp(((m_cameraX + width * 0.5f) / m_worldWidth - 0.53f) / 0.24f,
                    0.0f, 1.0f)
                : 0.0f;
        drawing.FillRectangle(0.0f, 0.0f, width, height,
            blendColor(Color(23, 26, 24), Color(169, 190, 201), centerSnowBlend));

        auto addRounded = [&drawing, &screenX, &screenY](float x, float y, float w, float h,
            float radius, Windows::UI::Color fill, Windows::UI::Color stroke = Windows::UI::Color{})
        {
            drawing.FillRoundedRectangle(screenX(x), screenY(y), w, h, radius, radius, fill);
            if (stroke.A != 0)
            {
                drawing.DrawRoundedRectangle(screenX(x), screenY(y), w, h,
                    radius, radius, stroke, 1.0f);
            }
        };
        auto addEllipse = [&drawing, &screenX, &screenY](float x, float y, float w, float h,
            Windows::UI::Color fill, Windows::UI::Color stroke = Windows::UI::Color{})
        {
            auto centerX = screenX(x);
            auto centerY = screenY(y);
            drawing.FillEllipse(centerX, centerY, w * 0.5f, h * 0.5f, fill);
            if (stroke.A != 0)
            {
                drawing.DrawEllipse(centerX, centerY, w * 0.5f, h * 0.5f, stroke, 1.0f);
            }
        };
        auto addLine = [&drawing, &screenX, &screenY](float x1, float y1, float x2, float y2,
            Windows::UI::Color color, float thickness)
        {
            drawing.DrawLine(screenX(x1), screenY(y1), screenX(x2), screenY(y2),
                color, thickness);
        };
        auto addCapsule = [&addEllipse, &addLine](float centerX, float centerY, float length,
            float angle, float thickness, Windows::UI::Color color)
        {
            float directionX = std::cos(angle);
            float directionY = std::sin(angle);
            float halfLength = (std::max)(0.0f, length * 0.5f - thickness * 0.5f);
            float startX = centerX - directionX * halfLength;
            float startY = centerY - directionY * halfLength;
            float endX = centerX + directionX * halfLength;
            float endY = centerY + directionY * halfLength;
            addLine(startX, startY, endX, endY, color, thickness);
            addEllipse(startX, startY, thickness, thickness, color);
            addEllipse(endX, endY, thickness, thickness, color);
        };

        for (int i = 0; i < 16; ++i)
        {
            float worldX = m_cameraX + static_cast<float>((i * 137 + 43) % static_cast<int>(width));
            float y = static_cast<float>((i * 83 + 29) % static_cast<int>(height - 90.0f));
            addEllipse(worldX, y, 2.0f, 2.0f, Color(203, 212, 191, 45));
        }
        float currentMountainProgress = m_currentLevel == 4
            ? MountainProgressAt(m_playerX + PlayerHalfWidth,
                m_playerY + PlayerHalfHeight) : 0.0f;
        bool blizzardActive = m_currentLevel == 4
            && currentMountainProgress >= MountainLength() * 0.78f;
        if (centerSnowBlend > 0.05f)
        {
            int snowflakeCount = blizzardActive ? 72 : 24;
            for (int i = 0; i < snowflakeCount; ++i)
            {
                float x = m_cameraX + static_cast<float>((i * 173 + 71) % static_cast<int>(width));
                float y = m_cameraY + std::fmod(
                    static_cast<float>((i * 97 + 31) % static_cast<int>(height))
                        + m_elapsed * (blizzardActive
                            ? 150.0f + static_cast<float>(i % 7) * 34.0f
                            : 32.0f + static_cast<float>(i % 5) * 9.0f),
                    height);
                float drift = blizzardActive
                    ? -55.0f - static_cast<float>(i % 6) * 12.0f
                        + std::sin(m_elapsed * 5.0f + static_cast<float>(i)) * 16.0f
                    : std::sin(m_elapsed * 1.4f + static_cast<float>(i)) * 8.0f;
                addLine(x + drift, y, x + drift + (blizzardActive ? 11.0f : 3.0f),
                    y + (blizzardActive ? 15.0f : 6.0f),
                    Color(245, 250, 252, static_cast<uint8_t>(
                        blizzardActive ? 185 : 80.0f + centerSnowBlend * 110.0f)),
                    blizzardActive ? 2.2f : 1.3f);
            }
        }

        if (m_currentLevel == 4 && m_mountainSurface.size() >= 2)
        {
            std::vector<winrt::Windows::Foundation::Numerics::float2> polygon;
            polygon.reserve(m_mountainSurface.size() + 2);
            for (auto const& point : m_mountainSurface)
            {
                polygon.push_back({ screenX(point.x), screenY(point.y) });
            }
            polygon.push_back({ screenX(m_worldWidth), screenY(height + 180.0f) });
            polygon.push_back({ screenX(m_mountainSurface.front().x),
                screenY(height + 180.0f) });
            auto mountain = Microsoft::Graphics::Canvas::Geometry::CanvasGeometry::CreatePolygon(
                sender, winrt::array_view<winrt::Windows::Foundation::Numerics::float2 const>(
                    polygon.data(), polygon.data() + polygon.size()));
            drawing.FillGeometry(mountain, Color(48, 57, 65));
            drawing.DrawGeometry(mountain, Color(108, 124, 129), 2.0f);

            for (size_t i = 1; i < m_mountainSurface.size(); ++i)
            {
                auto const& start = m_mountainSurface[i - 1];
                auto const& end = m_mountainSurface[i];
                float dx = end.x - start.x;
                float dy = end.y - start.y;
                float segmentLength = std::sqrt(dx * dx + dy * dy);
                float angle = std::atan2(dy, dx);
                float inwardX = -dy / (std::max)(1.0f, segmentLength);
                float inwardY = dx / (std::max)(1.0f, segmentLength);
                float centerX = (start.x + end.x) * 0.5f;
                float centerY = (start.y + end.y) * 0.5f;

                addCapsule(centerX, centerY, segmentLength + 12.0f, angle,
                    37.0f, Color(91, 109, 119));
                float snowX = centerX - inwardX * 8.0f;
                float snowY = centerY - inwardY * 8.0f;
                addCapsule(snowX, snowY, segmentLength + 6.0f, angle,
                    27.0f, Color(221, 236, 241));
                addCapsule(snowX - inwardX * 12.0f, snowY - inwardY * 12.0f,
                    segmentLength * 0.92f, angle, 5.0f, Color(255, 255, 255, 220));

                for (int band = 0; band < 3; ++band)
                {
                    float offset = 56.0f + static_cast<float>(band) * 49.0f;
                    float stripeX = centerX + inwardX * offset;
                    float stripeY = centerY + inwardY * offset;
                    float stripeLength = (std::min)(segmentLength * 0.72f,
                        170.0f + static_cast<float>((i * 37 + band * 53) % 80));
                    addCapsule(stripeX, stripeY, stripeLength,
                        angle + 0.10f * static_cast<float>(band % 2),
                        5.0f + static_cast<float>(band % 2) * 2.0f,
                        band == 1 ? Color(68, 80, 90, 190) : Color(119, 133, 138, 175));
                }

                if (i % 2 == 0)
                {
                    float crystalX = centerX + inwardX * 78.0f;
                    float crystalY = centerY + inwardY * 78.0f;
                    addCapsule(crystalX, crystalY, 20.0f, angle + 1.0f,
                        7.0f, Color(103, 207, 204));
                    addCapsule(crystalX + 8.0f, crystalY + 9.0f, 14.0f,
                        angle - 0.8f, 5.0f, Color(231, 189, 113));
                    addEllipse(crystalX - 2.0f, crystalY - 3.0f,
                        4.0f, 4.0f, Color(225, 251, 242));
                }
            }
        }

        for (auto const& platform : m_platforms)
        {
            if (platform.x + platform.width < m_cameraX - 20.0f
                || platform.x > m_cameraX + width + 20.0f)
            {
                continue;
            }
            float snowBlend = m_currentLevel == 4 ? 1.0f
                : m_currentLevel == 3
                    ? std::clamp((platform.x / m_worldWidth - 0.53f) / 0.24f,
                        0.0f, 1.0f)
                    : 0.0f;
            auto platformColor = blendColor(Color(65, 69, 60), Color(87, 100, 107), snowBlend);
            auto edgeColor = blendColor(Color(119, 132, 103), Color(206, 222, 227), snowBlend);
            auto surfaceColor = blendColor(Color(151, 196, 111), Color(244, 250, 250, 245),
                snowBlend);
            addRounded(platform.x, platform.y + 7.0f, platform.width, platform.height,
                9.0f, Color(10, 12, 10, 100));
            addRounded(platform.x, platform.y, platform.width, platform.height,
                9.0f, platformColor, Color(37, 46, 42));
            if (platform.height > 12.0f)
            {
                addRounded(platform.x + 2.0f, platform.y + 8.0f,
                    platform.width - 4.0f, platform.height - 10.0f, 3.0f,
                    blendColor(Color(53, 61, 50), Color(72, 85, 95), snowBlend));
            }
            addRounded(platform.x + 2.0f, platform.y + 1.0f,
                platform.width - 4.0f, (std::min)(12.0f, platform.height - 1.0f),
                7.0f, surfaceColor, edgeColor);
            addRounded(platform.x + 8.0f, platform.y + 3.0f,
                (std::max)(0.0f, platform.width - 30.0f), 2.0f, 1.0f,
                blendColor(Color(211, 231, 173, 165), Color(255, 255, 255, 205),
                    snowBlend));
            if (snowBlend < 0.95f)
            {
                for (float markX = platform.x + 24.0f;
                    markX < platform.x + platform.width - 12.0f; markX += 58.0f)
                {
                    float tuftY = platform.y + 2.0f
                        + static_cast<float>(static_cast<int>(markX) % 4);
                    addLine(markX, tuftY + 1.0f, markX - 3.0f, tuftY - 4.0f,
                        Color(89, 143, 75, 190), 2.0f);
                    addLine(markX, tuftY + 1.0f, markX + 4.0f, tuftY - 3.0f,
                        Color(115, 166, 87, 190), 2.0f);
                }
            }
        }

        for (auto const& route : m_climbRoutes)
        {
            float minX = (std::min)(route.startX, route.endX);
            float maxX = (std::max)(route.startX, route.endX);
            if (maxX < m_cameraX - 80.0f || minX > m_cameraX + width + 80.0f)
            {
                continue;
            }
            if (route.solidX > 0.0f)
            {
                float topY = route.endY + PlayerHalfHeight;
                float bottomY = route.startY + PlayerHalfHeight;
                addRounded(route.solidX - 18.0f, topY, 36.0f, bottomY - topY,
                    7.0f, Color(74, 79, 72), Color(131, 140, 124));
                for (float markY = topY + 28.0f; markY < bottomY; markY += 54.0f)
                {
                    addLine(route.solidX - 11.0f, markY, route.solidX + 10.0f,
                        markY - 13.0f, Color(178, 186, 167), 3.0f);
                }
            }
            else
            {
                float dx = route.endX - route.startX;
                float dy = route.endY - route.startY;
                float length = std::sqrt(dx * dx + dy * dy);
                float angle = std::atan2(dy, dx);
                float centerX = (route.startX + route.endX) * 0.5f;
                float centerY = (route.startY + route.endY) * 0.5f + PlayerHalfHeight;
                addCapsule(centerX, centerY, length, angle, 27.0f,
                    Color(99, 108, 108));
                addCapsule(centerX, centerY - 9.0f, length, angle, 8.0f,
                    Color(221, 235, 238));
            }
        }

        if (m_currentLevel == 2 && !m_machineGunUnlocked)
        {
            float pickupBob = std::sin(m_elapsed * 4.0f) * 4.0f;
            addRounded(m_machineGunPickupX - 19.0f,
                m_machineGunPickupY - 15.0f + pickupBob, 38.0f, 18.0f,
                6.0f, Color(54, 63, 59), Color(193, 215, 187));
            addCapsule(m_machineGunPickupX, m_machineGunPickupY - 6.0f + pickupBob,
                42.0f, 0.0f, 7.0f, Color(183, 199, 183));
            drawing.DrawText(L"轻机枪", screenX(m_machineGunPickupX - 20.0f),
                screenY(m_machineGunPickupY - 34.0f + pickupBob), Color(227, 239, 219));
        }

        for (auto const& explosion : m_explosions)
        {
            float progress = 1.0f - explosion.life / 0.34f;
            float diameter = 12.0f + progress * explosion.radius * 2.0f;
            uint8_t alpha = static_cast<uint8_t>(180.0f * (1.0f - progress));
            addEllipse(explosion.x, explosion.y, diameter, diameter,
                explosion.hostile
                    ? Color(255, 79, 83, static_cast<uint8_t>(alpha / 4))
                    : Color(255, 185, 87, static_cast<uint8_t>(alpha / 4)),
                explosion.hostile ? Color(255, 126, 118, alpha)
                    : Color(255, 214, 137, alpha));
        }

        for (auto const& particle : m_particles)
        {
            auto alpha = static_cast<uint8_t>(255.0f * std::clamp(
                particle.life / particle.maxLife, 0.0f, 1.0f));
            auto color = Color(particle.color.R, particle.color.G, particle.color.B, alpha);
            addCapsule(particle.x, particle.y, particle.size * 1.5f,
                particle.rotation, particle.size, color);
        }

        for (auto const& projectile : m_projectiles)
        {
            if (projectile.kind == ProjectileKind::Grenade
                || projectile.kind == ProjectileKind::EnemyBomb
                || projectile.kind == ProjectileKind::SnowBomb)
            {
                auto bodyColor = projectile.kind == ProjectileKind::Grenade
                    ? Color(230, 156, 74)
                    : projectile.kind == ProjectileKind::SnowBomb
                        ? Color(222, 239, 247) : Color(240, 91, 82);
                addEllipse(projectile.x, projectile.y, 15.0f, 15.0f,
                    bodyColor, projectile.kind == ProjectileKind::SnowBomb
                        ? Color(255, 255, 255) : Color(255, 220, 159));
                addEllipse(projectile.x - 3.0f, projectile.y - 4.0f, 4.0f, 4.0f,
                    projectile.kind == ProjectileKind::SnowBomb
                        ? Color(255, 255, 255) : Color(255, 240, 197));
            }
            else if (projectile.kind == ProjectileKind::Snowball)
            {
                addEllipse(projectile.x, projectile.y, 12.0f, 12.0f,
                    Color(224, 242, 250), Color(255, 255, 255));
                addEllipse(projectile.x - 2.0f, projectile.y - 2.0f,
                    3.0f, 3.0f, Color(255, 255, 255));
            }
            else if (projectile.kind == ProjectileKind::BossOrb)
            {
                float pulse = 1.0f + std::sin(m_elapsed * 16.0f + projectile.x) * 0.15f;
                addEllipse(projectile.x, projectile.y, 24.0f * pulse, 24.0f * pulse,
                    Color(255, 77, 117, 55), Color(255, 150, 178));
                addEllipse(projectile.x, projectile.y, 9.0f, 9.0f,
                    Color(255, 218, 183));
            }
            else if (projectile.kind == ProjectileKind::Shockwave)
            {
                addCapsule(projectile.x, projectile.y, 38.0f, 0.0f, 13.0f,
                    Color(255, 101, 69, 125));
                addCapsule(projectile.x, projectile.y - 5.0f, 25.0f, 0.0f, 3.0f,
                    Color(255, 206, 132, 210));
            }
            else
            {
                auto angle = std::atan2(projectile.vy, projectile.vx);
                addCapsule(projectile.x - projectile.vx * 0.025f,
                    projectile.y - projectile.vy * 0.025f, 16.0f, angle, 4.0f,
                    projectile.kind == ProjectileKind::Pistol
                        ? Color(255, 223, 157, 90)
                        : projectile.kind == ProjectileKind::MachineGun
                            ? Color(219, 232, 198, 120) : Color(255, 100, 103, 140));
                addCapsule(projectile.x, projectile.y, 8.0f, angle, 2.4f,
                    projectile.kind == ProjectileKind::Pistol
                        ? Color(255, 244, 213)
                        : projectile.kind == ProjectileKind::MachineGun
                            ? Color(234, 241, 220) : Color(255, 192, 172));
            }
        }

        for (auto const& enemy : m_enemies)
        {
            auto bob = std::sin(m_elapsed * 8.0f + enemy.phase) * 1.3f;
            bool miniBoss = enemy.kind == EnemyKind::MiniBoss;
            bool finalBoss = enemy.kind == EnemyKind::FinalBoss;
            bool boss = miniBoss || finalBoss;
            bool snowEnemy = enemy.kind == EnemyKind::SnowWalker
                || enemy.kind == EnemyKind::SnowArtillery;
            float bodyWidth = finalBoss ? 76.0f : miniBoss ? 58.0f : 28.0f;
            float bodyHeight = finalBoss ? 62.0f : miniBoss ? 48.0f : 24.0f;
            float halfWidth = bodyWidth * 0.5f;
            auto body = enemy.hitFlash > 0.0f ? Color(255, 235, 206)
                : finalBoss ? Color(141, 77, 112)
                : miniBoss ? Color(165, 104, 70)
                : enemy.kind == EnemyKind::Armored ? Color(131, 153, 165)
                : enemy.kind == EnemyKind::Bomber ? Color(238, 164, 87)
                : enemy.kind == EnemyKind::Gunner ? Color(111, 158, 166)
                : snowEnemy ? Color(218, 231, 236)
                : Color(235, 133, 111);
            if (enemy.parachuting)
            {
                float canopyY = enemy.y - bodyHeight * 0.5f - 19.0f
                    + std::sin(m_elapsed * 4.0f + enemy.phase) * 2.0f;
                addCapsule(enemy.x, canopyY, 43.0f, 0.0f, 5.0f,
                    Color(199, 215, 220));
                addLine(enemy.x - 18.0f, canopyY + 2.0f, enemy.x - 8.0f,
                    enemy.y - 10.0f, Color(227, 236, 237), 1.5f);
                addLine(enemy.x + 18.0f, canopyY + 2.0f, enemy.x + 8.0f,
                    enemy.y - 10.0f, Color(227, 236, 237), 1.5f);
            }
            if (boss)
            {
                float aura = 1.0f + std::sin(m_elapsed * 5.0f + enemy.phase) * 0.09f;
                addEllipse(enemy.x, enemy.y - 8.0f, bodyWidth * 1.42f * aura,
                    bodyHeight * 1.48f * aura,
                    finalBoss ? Color(243, 65, 110, 22) : Color(255, 172, 92, 20),
                    finalBoss ? Color(255, 95, 137, 100) : Color(255, 191, 107, 100));
                if (enemy.anger > 0.0f)
                {
                    addEllipse(enemy.x, enemy.y - 6.0f, bodyWidth * (1.55f + enemy.anger * 0.12f),
                        bodyHeight * 1.62f, Color(255, 62, 54, 20),
                        Color(255, 82, 73, static_cast<uint8_t>(90.0f + enemy.anger * 80.0f)));
                }
                if (enemy.stompWindup > 0.0f && enemy.platformIndex < m_platforms.size())
                {
                    float warningRadius = (1.0f - enemy.stompWindup / 0.82f) * 155.0f + 32.0f;
                    addEllipse(enemy.x, m_platforms[enemy.platformIndex].y - 3.0f,
                        warningRadius * 2.0f, 13.0f,
                        Color(255, 74, 57, 34),
                        Color(255, 119, 72, 220));
                }
            }
            addRounded(enemy.x - halfWidth, enemy.y - bodyHeight * 0.5f + bob,
                bodyWidth, bodyHeight, boss ? bodyWidth * 0.34f : 10.0f, body,
                finalBoss ? Color(255, 155, 181)
                    : miniBoss ? Color(255, 200, 125)
                    : enemy.kind == EnemyKind::Armored ? Color(206, 223, 234)
                    : snowEnemy ? Color(255, 255, 255)
                    : Color(255, 187, 154));
            if (!boss)
            {
                addRounded(enemy.x - 9.0f, enemy.y + 7.0f, 7.0f, 7.0f,
                    3.0f, Color(204, 104, 92));
                addRounded(enemy.x + 2.0f, enemy.y + 7.0f, 7.0f, 7.0f,
                    3.0f, Color(204, 104, 92));
            }
            float eyeDirection = enemy.speed > 0.0f ? 1.0f : -1.0f;
            float eyeSpacing = boss ? (finalBoss ? 15.0f : 11.0f) : 3.0f;
            addEllipse(enemy.x + eyeDirection * eyeSpacing, enemy.y - 3.0f + bob,
                boss ? 9.0f : 5.5f, boss ? 11.0f : 7.0f, Color(250, 245, 225));
            addEllipse(enemy.x + eyeDirection * (eyeSpacing + 1.0f), enemy.y - 2.0f + bob,
                boss ? 4.0f : 2.2f, boss ? 5.0f : 3.0f, Color(41, 46, 39));
            if (enemy.kind == EnemyKind::Armored)
            {
                addRounded(enemy.x - 16.0f, enemy.y - 11.0f, 11.0f, 20.0f,
                    4.0f, Color(77, 95, 107), Color(216, 226, 232));
            }
            else if (enemy.kind == EnemyKind::Bomber)
            {
                addEllipse(enemy.x - 14.0f, enemy.y - 17.0f, 8.0f, 8.0f,
                    Color(255, 199, 101));
            }
            else if (enemy.kind == EnemyKind::Gunner)
            {
                addCapsule(enemy.x + eyeDirection * 19.0f, enemy.y + 1.0f,
                    14.0f, eyeDirection > 0 ? 0.0f : Pi, 5.0f, Color(50, 61, 58));
            }
            else if (enemy.kind == EnemyKind::SnowArtillery)
            {
                addEllipse(enemy.x - eyeDirection * 15.0f, enemy.y - 17.0f,
                    13.0f, 13.0f, Color(223, 242, 248), Color(255, 255, 255));
                addCapsule(enemy.x + eyeDirection * 18.0f, enemy.y + 2.0f,
                    19.0f, eyeDirection > 0 ? 0.0f : Pi, 7.0f, Color(164, 188, 198));
            }
            float barWidth = finalBoss ? 104.0f : miniBoss ? 82.0f : 38.0f;
            float healthRatio = std::clamp(enemy.health / enemy.maxHealth, 0.0f, 1.0f);
            float displayedRatio = std::clamp(enemy.displayedHealth / enemy.maxHealth, 0.0f, 1.0f);
            float greenWidth = barWidth * displayedRatio;
            float healthWidth = barWidth * healthRatio;
            float barY = enemy.y - bodyHeight * 0.5f - 19.0f;
            addRounded(enemy.x - barWidth * 0.5f - 1.0f, barY - 1.0f,
                barWidth + 2.0f, boss ? 9.0f : 7.0f,
                boss ? 4.5f : 3.5f, Color(20, 23, 19, 210),
                Color(203, 209, 187, 120));
            addRounded(enemy.x - barWidth * 0.5f, barY,
                greenWidth, boss ? 7.0f : 5.0f, boss ? 3.5f : 2.5f,
                finalBoss ? Color(255, 91, 126)
                    : miniBoss ? Color(255, 176, 88) : Color(117, 207, 102));
            if (greenWidth > healthWidth + 0.5f)
            {
                addRounded(enemy.x - barWidth * 0.5f + healthWidth, barY,
                    greenWidth - healthWidth, boss ? 7.0f : 5.0f,
                    boss ? 3.5f : 2.5f, Color(255, 126, 101));
            }
            if (boss)
            {
                drawing.DrawText(finalBoss ? L"风暴核心" : L"铁拳",
                    screenX(enemy.x - 27.0f), screenY(barY - 17.0f),
                    finalBoss ? Color(255, 170, 190) : Color(255, 214, 151));
            }
        }

        for (auto const& number : m_damageNumbers)
        {
            drawing.DrawText(hstring{ number.text }, screenX(number.x - 12.0f),
                screenY(number.y), Color(255, 233, 202,
                    static_cast<uint8_t>(255.0f * std::clamp(number.life / 0.7f, 0.0f, 1.0f))));
        }

        float exitX = m_worldWidth - 50.0f;
        if (exitX > m_cameraX - 40.0f && exitX < m_cameraX + width + 40.0f)
        {
            float doorPulse = 0.5f + 0.5f * std::sin(m_elapsed * 2.4f);
            float doorOpen = m_endSequence == EndSequence::Victory
                ? std::clamp(m_sequenceTime / 0.48f, 0.0f, 1.0f) : 0.0f;
            doorOpen = doorOpen * doorOpen * (3.0f - 2.0f * doorOpen);
            auto glowColor = Color(197, 239, 124);
            addEllipse(exitX, m_exitY - 5.0f, 48.0f + doorPulse * 8.0f,
                72.0f + doorPulse * 10.0f,
                Color(197, 239, 124, static_cast<uint8_t>(18.0f + doorOpen * 48.0f)));
            addRounded(exitX - 18.0f, m_exitY - 36.0f, 36.0f, 70.0f,
                10.0f, Color(40, 48, 37), glowColor);
            addRounded(exitX - 11.0f, m_exitY - 30.0f, 22.0f, 59.0f,
                6.0f, Color(18, 24, 19), Color(110, 135, 80));
            if (doorOpen < 0.995f)
            {
                float projectedWidth = 20.0f * (1.0f - doorOpen) + 2.0f;
                float panelX = exitX - 10.0f;
                auto panelColor = Color(static_cast<uint8_t>(59 + doorPulse * 18),
                    static_cast<uint8_t>(76 + doorPulse * 23),
                    static_cast<uint8_t>(49 + doorPulse * 13));
                addRounded(panelX, m_exitY - 29.0f, projectedWidth, 58.0f,
                    2.0f, panelColor, Color(133, 154, 105));
                if (projectedWidth > 9.0f)
                {
                    addRounded(panelX + 4.0f, m_exitY - 23.0f,
                        projectedWidth - 8.0f, 46.0f, 2.0f,
                        Color(49, 63, 43), Color(91, 112, 69));
                    float knobX = panelX + projectedWidth - 4.0f;
                    addEllipse(knobX, m_exitY + 1.0f, 4.0f, 4.0f, glowColor);
                }
            }
        }

        float playerX = m_playerX;
        float playerY = m_playerY;
        if (m_playerDead)
        {
            float slump = std::clamp(m_sequenceTime / 0.62f, 0.0f, 1.0f);
            playerY += slump * 13.0f;
            addCapsule(playerX, playerY + 7.0f, 30.0f,
                0.18f + slump * 0.45f, 13.0f, Color(108, 132, 79));
            addEllipse(playerX - 10.0f * slump, playerY - 7.0f,
                20.0f, 20.0f, Color(220, 174, 139));
            addLine(playerX - 13.0f, playerY - 10.0f, playerX - 9.0f,
                playerY - 6.0f, Color(59, 49, 42), 1.8f);
            addLine(playerX - 9.0f, playerY - 10.0f, playerX - 13.0f,
                playerY - 6.0f, Color(59, 49, 42), 1.8f);
            addCapsule(playerX + 12.0f, playerY + 11.0f, 22.0f,
                -0.62f, 5.0f, Color(42, 48, 41));
            addCapsule(playerX - 4.0f, playerY + 14.0f, 18.0f,
                0.75f, 5.0f, Color(42, 48, 41));
        }
        else
        {
            float walkCycle = std::sin(m_elapsed * 15.0f) * (std::min)(1.0f,
                std::abs(m_velocityX) / 100.0f);
            playerY += std::abs(walkCycle) * 1.5f;
            bool blinking = m_invulnerability > 0.0f
                && static_cast<int>(m_elapsed * 18.0f) % 2 == 0;

            addRounded(playerX - 10.0f, playerY + 10.0f + walkCycle * 2.0f,
                8.0f, 15.0f, 4.0f, Color(42, 48, 41));
            addRounded(playerX + 2.0f, playerY + 10.0f - walkCycle * 2.0f,
                8.0f, 15.0f, 4.0f, Color(42, 48, 41));
            addRounded(playerX - 13.0f, playerY - 8.0f, 26.0f, 25.0f,
                10.0f, blinking ? Color(255, 231, 205, 150) : Color(183, 219, 126),
                Color(210, 239, 167));
            addEllipse(playerX, playerY - 18.0f, 25.0f, 25.0f,
                blinking ? Color(255, 231, 205, 150) : Color(242, 197, 154));
            addRounded(playerX - 12.0f, playerY - 24.0f, 24.0f, 9.0f,
                5.0f, Color(54, 60, 49));
            float eyeOffset = m_facing > 0 ? 5.0f : -5.0f;
            addEllipse(playerX + eyeOffset, playerY - 18.0f, 3.0f, 4.0f, Color(46, 48, 41));
            if (m_climbing)
            {
                float reach = std::sin(m_elapsed * 10.0f) * 5.0f;
                addCapsule(playerX - 15.0f, playerY - 3.0f + reach,
                    16.0f, -0.75f, 5.0f, Color(183, 219, 126));
                addCapsule(playerX + 15.0f, playerY - 4.0f - reach,
                    16.0f, -2.35f, 5.0f, Color(183, 219, 126));
            }

            float aimElevation = std::atan2(m_aimY, std::abs(m_aimX)) * 180.0f / Pi;
        bool weaponFacesLeft = m_facing < 0;
        float reloadMotion = m_reloading ? std::sin(m_reloadPhase * Pi) : 0.0f;
        float gunAngle = (weaponFacesLeft ? -aimElevation : aimElevation)
            + (weaponFacesLeft ? -1.0f : 1.0f) * reloadMotion * 52.0f;
        float gunX = playerX + m_aimX * 4.0f;
        float gunY = playerY - 4.0f + m_aimY * 4.0f + reloadMotion * 13.0f;
        float recoilX = m_aimX * m_recoil * 3.0f;
        float recoilY = m_aimY * m_recoil * 3.0f;
        bool grenadeLauncher = m_selectedWeapon == WeaponKind::Grenade;
        bool machineGun = m_selectedWeapon == WeaponKind::MachineGun;
        float pivotX = gunX + 3.0f - recoilX;
        float pivotY = gunY - recoilY;
        float weaponDirection = weaponFacesLeft ? -1.0f : 1.0f;
        auto weaponPoint = [pivotX, pivotY, weaponDirection, gunAngle](float x, float y)
        {
            float localX = (x - 3.0f) * weaponDirection;
            float localY = y - 16.0f;
            float cosine = std::cos(gunAngle * Pi / 180.0f);
            float sine = std::sin(gunAngle * Pi / 180.0f);
            return std::pair<float, float>{
                pivotX + localX * cosine - localY * sine,
                pivotY + localX * sine + localY * cosine
            };
        };
        auto bodyCenter = weaponPoint(19.0f, 16.5f);
        auto barrelCenter = weaponPoint(grenadeLauncher ? 41.0f : machineGun ? 46.0f : 39.5f,
            grenadeLauncher ? 16.0f : 16.5f);
        auto gripCenter = weaponPoint(15.5f, 26.0f);
        auto accentCenter = weaponPoint(13.0f, 10.5f);
        float weaponAngle = gunAngle * Pi / 180.0f;
        float facingAngle = weaponFacesLeft ? Pi + weaponAngle : weaponAngle;
        addCapsule(bodyCenter.first, bodyCenter.second, machineGun ? 40.0f : 34.0f,
            facingAngle, machineGun ? 20.0f : 18.0f,
            grenadeLauncher ? Color(221, 175, 104)
                : machineGun ? Color(174, 192, 170) : Color(116, 128, 99));
        addCapsule(bodyCenter.first, bodyCenter.second, machineGun ? 40.0f : 34.0f,
            facingAngle, machineGun ? 17.0f : 16.0f,
            grenadeLauncher ? Color(95, 74, 47)
                : machineGun ? Color(59, 70, 65) : Color(55, 62, 53));
        addCapsule(barrelCenter.first, barrelCenter.second,
            grenadeLauncher ? 24.0f : machineGun ? 32.0f : 21.0f,
            facingAngle, grenadeLauncher ? 14.0f : 9.0f,
            grenadeLauncher ? Color(181, 135, 80)
                : machineGun ? Color(135, 153, 138) : Color(96, 108, 87));
        addCapsule(barrelCenter.first, barrelCenter.second,
            grenadeLauncher ? 24.0f : machineGun ? 32.0f : 21.0f,
            facingAngle, grenadeLauncher ? 11.0f : 6.0f,
            grenadeLauncher ? Color(57, 46, 33)
                : machineGun ? Color(36, 44, 40) : Color(35, 40, 35));
        addCapsule(gripCenter.first, gripCenter.second, 12.0f,
            facingAngle + Pi * 0.5f, 9.0f,
            grenadeLauncher ? Color(119, 87, 50)
                : machineGun ? Color(80, 95, 86) : Color(78, 89, 69));
        addCapsule(accentCenter.first, accentCenter.second, 10.0f,
            facingAngle, 3.0f,
            grenadeLauncher ? Color(255, 195, 106)
                : machineGun ? Color(222, 233, 205) : Color(197, 239, 124));
        if (machineGun)
        {
            auto beltLink = weaponPoint(25.0f, 27.0f);
            addRounded(beltLink.first - 4.0f, beltLink.second - 3.0f,
                11.0f, 8.0f, 3.0f, Color(176, 191, 170));
        }
        if (m_reloading && m_reloadWeapon == WeaponKind::Grenade)
        {
            float loadX = playerX + static_cast<float>(m_facing) * 25.0f;
            float loadY = playerY - 10.0f - std::sin(m_reloadPhase * Pi) * 10.0f;
            addEllipse(loadX, loadY, 12.0f, 12.0f,
                Color(255, 196, 103), Color(255, 235, 174));
        }
        else if (m_reloading)
        {
            float phase = m_reloadPhase;
            float magY = playerY + 28.0f + std::sin(phase * Pi) * 17.0f;
            uint8_t magAlpha = static_cast<uint8_t>(255.0f
                * (phase < 0.18f ? phase / 0.18f : phase > 0.82f ? (1.0f - phase) / 0.18f : 1.0f));
            addRounded(playerX + static_cast<float>(m_facing) * 11.0f - 3.0f,
                magY, machineGun ? 10.0f : 7.0f, machineGun ? 15.0f : 12.0f,
                2.0f, Color(machineGun ? 204 : 177, machineGun ? 218 : 191,
                    machineGun ? 197 : 157, magAlpha));
        }

        if (m_muzzleFlash > 0.0f)
        {
            float muzzleX = playerX + m_aimX * 52.0f - recoilX;
            float muzzleY = playerY - 4.0f + m_aimY * 52.0f - recoilY;
            addEllipse(muzzleX, muzzleY, 18.0f, 18.0f, Color(255, 181, 87, 100));
            addEllipse(muzzleX, muzzleY, 8.0f, 8.0f, Color(255, 244, 202));
        }
        }

        if (m_endSequence == EndSequence::Victory && m_sequenceTime > 1.02f)
        {
            float entry = std::clamp((m_sequenceTime - 1.02f) / 0.48f, 0.0f, 1.0f);
            entry = entry * entry * (3.0f - 2.0f * entry);
            float entryWidth = 22.0f + 20.0f * entry;
            drawing.FillRoundedRectangle(screenX(exitX - entryWidth * 0.5f),
                screenY(m_exitY - 30.0f), entryWidth, 59.0f, 8.0f, 8.0f,
                Color(7, 11, 10, static_cast<uint8_t>(255.0f * entry)));
            addRounded(exitX - 18.0f, m_exitY - 36.0f, 7.0f, 70.0f,
                4.0f, Color(40, 48, 37), Color(197, 239, 124));
            addRounded(exitX + 11.0f, m_exitY - 36.0f, 7.0f, 70.0f,
                4.0f, Color(40, 48, 37), Color(197, 239, 124));
            addRounded(exitX - 18.0f, m_exitY - 36.0f, 36.0f, 7.0f,
                4.0f, Color(40, 48, 37), Color(197, 239, 124));
            addRounded(exitX - 15.0f, m_exitY + 29.0f, 30.0f, 5.0f,
                2.0f, Color(59, 70, 49), Color(197, 239, 124));
        }

        if (m_endSequence != EndSequence::Victory
            && m_isTutorial && m_tutorialStep < 5)
        {
            std::wstring instruction;
            switch (m_tutorialStep)
            {
            case 0: instruction = L"A / D　向前走一小段"; break;
            case 1: instruction = L"瞄准小怪，按住鼠标左键"; break;
            case 2: instruction = L"击败敌人获得榴弹！按 2 或滚轮切换"; break;
            case 3: instruction = L"瞄准远处目标，左键发射榴弹"; break;
            case 4: instruction = L"榴弹会弹道下坠，命中或落地时范围爆炸"; break;
            default: break;
            }
            drawing.FillRoundedRectangle(width * 0.5f - 190.0f, 18.0f,
                380.0f, 40.0f, 13.0f, 13.0f, Color(28, 33, 26, 235));
            drawing.DrawRoundedRectangle(width * 0.5f - 190.0f, 18.0f,
                380.0f, 40.0f, 13.0f, 13.0f, Color(91, 111, 65), 1.0f);
            drawing.DrawText(hstring{ instruction }, width * 0.5f - 173.0f,
                28.0f, Color(220, 240, 190));
        }
        else if (m_endSequence != EndSequence::Victory
            && m_currentLevel == 2 && !m_machineGunUnlocked
            && m_climbRoutes.size() > 0
            && std::abs(m_playerX - m_climbRoutes.front().startX) < 280.0f)
        {
            drawing.FillRoundedRectangle(width * 0.5f - 185.0f, 18.0f,
                370.0f, 40.0f, 13.0f, 13.0f, Color(28, 33, 26, 235));
            drawing.DrawText(L"高墙：按住 D + W / 空格攀爬",
                width * 0.5f - 164.0f, 28.0f, Color(226, 238, 215));
        }
        else if (m_currentLevel == 4 && !m_climbing)
        {
            float progress = MountainProgressAt(
                m_playerX + PlayerHalfWidth, m_playerY + PlayerHalfHeight);
            auto surfacePoint = MountainPointAt(progress);
            float dx = m_playerX + PlayerHalfWidth - surfacePoint.x;
            float dy = m_playerY + PlayerHalfHeight - surfacePoint.y;
            if (dx * dx + dy * dy < 250.0f * 250.0f)
            {
                drawing.FillRoundedRectangle(width * 0.5f - 185.0f, 18.0f,
                    370.0f, 40.0f, 13.0f, 13.0f, Color(28, 33, 26, 235));
                drawing.DrawText(L"沿雪坡攀爬：按住 D + W / 空格",
                    width * 0.5f - 164.0f, 28.0f, Color(226, 238, 215));
            }
        }
        if (m_machineGunPickupTime > 0.0f)
        {
            float alpha = std::clamp(m_machineGunPickupTime / 0.5f, 0.0f, 1.0f);
            drawing.FillRoundedRectangle(width * 0.5f - 205.0f, 72.0f,
                410.0f, 48.0f, 15.0f, 15.0f,
                Color(27, 35, 29, static_cast<uint8_t>(235.0f * alpha)));
            drawing.DrawText(L"轻机枪已加入武器栏  ·  50 发  ·  10 秒换弹",
                width * 0.5f - 182.0f, 86.0f,
                Color(231, 242, 219, static_cast<uint8_t>(255.0f * alpha)));
        }
    }
}
