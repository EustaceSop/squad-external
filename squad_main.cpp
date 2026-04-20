#include <Windows.h>
#include <cstdio>
#include <thread>
#include <atomic>
#include <chrono>
#include <fstream>

#include "crypt/lazyimporter.hpp"
#include "crypt/xor.hpp"
#include "discord_overlay.h"
#include "renderer.h"
#include "gui.h"
#include "squad_driver.hpp"
#include "squad_core.hpp"
#include "squad_esp.hpp"
#include "squad_exploit.hpp"
#include "squad_aimbot.hpp"
#include "squad_config.hpp"

// ============================================================================
// Squad External ESP + Exploit + Aimbot
// Discord Overlay + DX11 + Self-drawn GUI + KMBox
// ============================================================================

static std::atomic<bool> g_running{ true };
static const char* CONFIG_PATH = "squad_config.ini";
static const char* DEBUG_PATH  = "squad_debug.txt";

// ---------------------------------------------------------------------------
// Config binding
// ---------------------------------------------------------------------------
void bind_config(Config& cfg, SquadESP& esp, SquadExploit& exploit, SquadAimbot& aim)
{
    // ESP
    cfg.bind_bool("esp.skeleton",   &esp.draw_skeleton);
    cfg.bind_bool("esp.box",        &esp.draw_box);
    cfg.bind_bool("esp.head_dot",   &esp.draw_head_dot);
    cfg.bind_bool("esp.health",     &esp.draw_health);
    cfg.bind_bool("esp.name",       &esp.draw_name);
    cfg.bind_bool("esp.distance",   &esp.draw_distance);
    cfg.bind_bool("esp.snaplines",  &esp.draw_snaplines);
    cfg.bind_bool("esp.team_check", &esp.team_check);
    cfg.bind_bool("esp.debug",      &esp.draw_debug);
    cfg.bind_float("esp.max_dist",  &esp.max_distance);
    cfg.bind_color("esp.col_enemy",   &esp.col_enemy);
    cfg.bind_color("esp.col_team",    &esp.col_team);
    cfg.bind_color("esp.col_wounded", &esp.col_wounded);
    cfg.bind_color("esp.col_text",    &esp.col_text);
    cfg.bind_color("esp.col_snap",    &esp.col_snap);

    // Exploit
    cfg.bind_bool("exploit.no_sway",       &exploit.no_sway);
    cfg.bind_bool("exploit.no_recoil",     &exploit.no_recoil);
    cfg.bind_bool("exploit.inf_breath",    &exploit.infinite_breath);

    // Aimbot
    cfg.bind_bool("aim.enabled",           &aim.config.enabled);
    cfg.bind_bool("aim.draw_fov",          &aim.config.drawFov);
    cfg.bind_bool("aim.team_check",        &aim.config.teamCheck);
    cfg.bind_int("aim.hitbox",             &aim.config.hitbox);
    cfg.bind_float("aim.fov",              &aim.config.fovSize);
    cfg.bind_bool("aim.use_ads_fov",       &aim.config.useAdsFov);
    cfg.bind_float("aim.ads_fov",          &aim.config.adsFovSize);
    cfg.bind_float("aim.smooth_x",         &aim.config.smoothX);
    cfg.bind_float("aim.smooth_y",         &aim.config.smoothY);
    cfg.bind_int("aim.key1",               &aim.config.aimKey1);
    cfg.bind_int("aim.key2",               &aim.config.aimKey2);
    cfg.bind_bool("aim.humanized",         &aim.config.humanized);
    cfg.bind_float("aim.human_strength",   &aim.config.humanStrength);
    cfg.bind_float("aim.human_curve",      &aim.config.humanCurve);
    cfg.bind_float("aim.human_jitter",     &aim.config.humanJitter);
    cfg.bind_float("aim.human_tremor",     &aim.config.humanTremor);
    cfg.bind_bool("aim.target_lock",       &aim.config.targetLock);
    cfg.bind_float("aim.switch_delay",     &aim.config.targetSwitchDelay);
    cfg.bind_bool("aim.sway_comp",         &aim.config.swayCompensation);
    cfg.bind_float("aim.sway_scale",       &aim.config.swayScale);
    cfg.bind_float("aim.max_dist",         &aim.config.maxDistance);
    cfg.bind_float("aim.sens_fallback",    &aim.config.sensFallback);
    cfg.bind_string("aim.kmbox_ip",   aim.config.kmboxIp, sizeof(aim.config.kmboxIp));
    cfg.bind_string("aim.kmbox_port", aim.config.kmboxPort, sizeof(aim.config.kmboxPort));
    cfg.bind_string("aim.kmbox_mac",  aim.config.kmboxMac, sizeof(aim.config.kmboxMac));
}

// ---------------------------------------------------------------------------
// Exploit thread (channel 2, ~200Hz)
// ---------------------------------------------------------------------------
void exploit_thread(SquadDriver& drv, SquadCore& core, SquadExploit& exploit)
{
    while (g_running) {
        exploit.tick(core.local_pawn());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main()
{
    printf(xorstr_("============================================\n"));
    printf(xorstr_(" Squad External ESP + Exploit + Aimbot\n"));
    printf(xorstr_(" v9.0.2 | UE 5.5.4 | Discord + KMBox\n"));
    printf(xorstr_("============================================\n\n"));

    // --- Connect to kernel driver ---
    SquadDriver drv;
    printf(xorstr_("[*] Connecting to Evoria driver...\n"));
    if (!drv.connect()) {
        printf(xorstr_("[-] Failed to connect to driver\n"));
        LI_FN(system)(xorstr_("pause"));
        return 1;
    }
    printf(xorstr_("[+] Driver connected\n"));

    // --- Attach to Squad ---
    printf(xorstr_("[*] Looking for SquadGame.exe...\n"));
    if (!drv.attach()) {
        printf(xorstr_("[-] Squad not found\n"));
        LI_FN(system)(xorstr_("pause"));
        return 1;
    }
    printf(xorstr_("[+] Attached: PID=%u BASE=0x%llX\n"), drv.pid(), drv.base());

    // --- Discord overlay hijack ---
    DiscordOverlay overlay;
    printf(xorstr_("[*] Looking for Discord overlay...\n"));
    if (!overlay.HijackWindow()) {
        LI_FN(system)(xorstr_("pause"));
        return 1;
    }
    if (!overlay.CreateDevice()) {
        printf(xorstr_("[-] Failed to create DX11 device\n"));
        LI_FN(system)(xorstr_("pause"));
        return 1;
    }
    overlay.CacheGameWindow(drv.pid());

    // --- Initialize Renderer + GUI ---
    g_Renderer = std::make_unique<Renderer>();
    if (!g_Renderer->Init(overlay.device(), overlay.context(), overlay.swapChain(), overlay.hwnd())) {
        printf(xorstr_("[-] Failed to init renderer\n"));
        LI_FN(system)(xorstr_("pause"));
        return 1;
    }
    g_GUI = std::make_unique<GUI>(g_Renderer.get(), overlay.hwnd());

    // --- Initialize game systems ---
    SquadCore    core(drv);
    SquadESP     esp;
    SquadExploit exploit(drv);
    SquadAimbot  aimbot;

    // --- Load config ---
    Config cfg;
    bind_config(cfg, esp, exploit, aimbot);
    if (cfg.load(CONFIG_PATH)) {
        printf(xorstr_("[+] Config loaded from %s\n"), CONFIG_PATH);
    }

    // --- Connect KMBox ---
    printf(xorstr_("[*] Connecting KMBox: %s:%s...\n"), aimbot.config.kmboxIp, aimbot.config.kmboxPort);
    if (aimbot.init_kmbox()) {
        printf(xorstr_("[+] KMBox connected!\n"));
    } else {
        printf(xorstr_("[!] KMBox failed - aimbot disabled (ESP/exploit still work)\n"));
    }

    // --- Start exploit thread ---
    std::thread exploit_thr(exploit_thread, std::ref(drv), std::ref(core), std::ref(exploit));

    printf(xorstr_("\n[+] Running! INSERT = menu, END = exit\n\n"));

    // --- Main loop ---
    MSG msg;
    bool menuVisible = true;
    bool insPressed = false;
    bool f9Pressed = false;
    auto lastDebugDump = std::chrono::steady_clock::now();

    // Hitbox combo items
    const char* hitbox_items[] = { "Head", "Neck", "Chest", "Closest" };

    while (g_running) {
        while (LI_FN(PeekMessageW)(&msg, overlay.hwnd(), 0, 0, PM_REMOVE)) {
            LI_FN(TranslateMessage)(&msg);
            LI_FN(DispatchMessageW)(&msg);
            if (msg.message == WM_QUIT) g_running = false;
        }

        if (LI_FN(GetAsyncKeyState)(VK_END) & 1) { g_running = false; break; }
        if (LI_FN(GetAsyncKeyState)(VK_F8) & 1) { esp.draw_debug = !esp.draw_debug; }
        bool f9Down = (LI_FN(GetAsyncKeyState)(VK_F9) & 0x8000) != 0;

        bool insDown = (LI_FN(GetAsyncKeyState)(VK_INSERT) & 0x8000) != 0;
        if (insDown && !insPressed) {
            menuVisible = !menuVisible;
            g_GUI->m_Open = menuVisible;
            overlay.SetMenuVisible(menuVisible);
        }
        insPressed = insDown;

        // --- Frame ---
        overlay.BeginFrame();
        g_Renderer->BeginFrame();
        core.update(overlay.width(), overlay.height());

        // --- Aimbot (runs every frame, behind menu) ---
        aimbot.run(core, overlay.width(), overlay.height());

        // --- ESP ---
        esp.render(g_Renderer.get(), core, overlay.width(), overlay.height());
        aimbot.draw_fov(g_Renderer.get(), overlay.width(), overlay.height(), core.is_ads());

        // --- Debug dump to txt ---
        if (esp.draw_debug) {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastDebugDump).count() >= 750) {
                std::ofstream dbg(DEBUG_PATH, std::ios::trunc);
                if (dbg.is_open()) {
                    dbg << esp.build_debug_text(core, overlay.width(), overlay.height());
                    dbg.flush();
                }
                lastDebugDump = now;
            }
        }

        if (f9Down && !f9Pressed) {
            std::ofstream dbg(DEBUG_PATH, std::ios::trunc);
            if (dbg.is_open()) {
                dbg << esp.build_debug_text(core, overlay.width(), overlay.height());
                dbg.flush();
                if (g_GUI) g_GUI->AddNotification(xorstr_("Debug dumped to squad_debug.txt"), 2.f);
            }
        }
        f9Pressed = f9Down;

        // --- Menu ---
        if (g_GUI->m_Open) {
            g_GUI->NewFrame();
            g_GUI->BeginUI(100, 100, xorstr_("Squad v9.0.2"));

            g_GUI->Tab(xorstr_("ESP"),      0, 5);
            g_GUI->Tab(xorstr_("Aimbot"),   1, 5);
            g_GUI->Tab(xorstr_("Exploit"),  2, 5);
            g_GUI->Tab(xorstr_("Colors"),   3, 5);
            g_GUI->Tab(xorstr_("Settings"), 4, 5);

            switch (g_GUI->m_ActiveTab) {
            case 0: // ESP
                g_GUI->Checkbox(xorstr_("Skeleton ESP"),  xorstr_("Draw bone skeleton"),   &esp.draw_skeleton);
                g_GUI->Checkbox(xorstr_("Box ESP"),       xorstr_("Draw bounding box"),    &esp.draw_box);
                g_GUI->Checkbox(xorstr_("Head Dot"),      xorstr_("Draw head dot"),        &esp.draw_head_dot);
                g_GUI->Checkbox(xorstr_("Health Bar"),    xorstr_("Draw health bar"),      &esp.draw_health);
                g_GUI->Checkbox(xorstr_("Name"),          xorstr_("Show player name"),     &esp.draw_name);
                g_GUI->Checkbox(xorstr_("Distance"),      xorstr_("Show distance in m"),   &esp.draw_distance);
                g_GUI->Checkbox(xorstr_("Snaplines"),     xorstr_("Lines to feet"),        &esp.draw_snaplines);
                g_GUI->Checkbox(xorstr_("Team Check"),    xorstr_("Only show enemies"),    &esp.team_check);
                g_GUI->SliderFloat(xorstr_("Max Distance"), &esp.max_distance, 100.f, 3000.f, 50.f);
                break;

            case 1: // Aimbot
                g_GUI->Checkbox(xorstr_("Enabled"),       xorstr_("Enable aimbot"),        &aimbot.config.enabled);
                g_GUI->Checkbox(xorstr_("Draw FOV"),      xorstr_("Show FOV circle"),      &aimbot.config.drawFov);
                g_GUI->Checkbox(xorstr_("Team Check"),    xorstr_("Skip teammates"),       &aimbot.config.teamCheck);
                g_GUI->ComboBox(xorstr_("Hitbox"),        &aimbot.config.hitbox, hitbox_items, 4);
                g_GUI->SliderFloat(xorstr_("FOV Size"),   &aimbot.config.fovSize, 10.f, 500.f, 5.f);
                g_GUI->Checkbox(xorstr_("ADS FOV"),       xorstr_("Separate ADS FOV"),     &aimbot.config.useAdsFov);
                g_GUI->SliderFloat(xorstr_("ADS FOV Size"), &aimbot.config.adsFovSize, 10.f, 300.f, 5.f);
                g_GUI->SliderFloat(xorstr_("Smooth X"),   &aimbot.config.smoothX, 1.f, 30.f, 0.5f);
                g_GUI->SliderFloat(xorstr_("Smooth Y"),   &aimbot.config.smoothY, 1.f, 30.f, 0.5f);
                g_GUI->Keybind(xorstr_("Aim Key 1"),      &aimbot.config.aimKey1);
                g_GUI->Keybind(xorstr_("Aim Key 2"),      &aimbot.config.aimKey2);
                g_GUI->Checkbox(xorstr_("Humanized"),     xorstr_("OU noise + tremor"),    &aimbot.config.humanized);
                g_GUI->SliderFloat(xorstr_("Human Strength"), &aimbot.config.humanStrength, 0.f, 1.f, 0.05f);
                g_GUI->SliderFloat(xorstr_("Human Curve"),    &aimbot.config.humanCurve, 0.1f, 2.f, 0.1f);
                g_GUI->SliderFloat(xorstr_("Human Jitter"),   &aimbot.config.humanJitter, 0.f, 1.f, 0.05f);
                g_GUI->SliderFloat(xorstr_("Human Tremor"),   &aimbot.config.humanTremor, 0.f, 1.f, 0.05f);
                g_GUI->Checkbox(xorstr_("Target Lock"),   xorstr_("Lock onto target"),     &aimbot.config.targetLock);
                g_GUI->SliderFloat(xorstr_("Switch Delay"), &aimbot.config.targetSwitchDelay, 0.f, 1000.f, 50.f);
                g_GUI->Checkbox(xorstr_("Sway Compensation"), xorstr_("Compensate breath / weapon sway in aim math"), &aimbot.config.swayCompensation);
                g_GUI->SliderFloat(xorstr_("Sway Scale"), &aimbot.config.swayScale, 0.f, 2.f, 0.05f);
                g_GUI->SliderFloat(xorstr_("Max Distance"), &aimbot.config.maxDistance, 50.f, 1500.f, 25.f);
                g_GUI->SliderFloat(xorstr_("Sens Fallback"), &aimbot.config.sensFallback, 0.1f, 10.f, 0.1f);
                {
                    char status[64];
                    sprintf_s(status, xorstr_("KMBox: %s | Sens: %.2f | %s"),
                        aimbot.config.kmboxConnected ? xorstr_("OK") : xorstr_("FAIL"),
                        core.sensitivity(),
                        core.is_ads() ? xorstr_("ADS") : xorstr_("HIP"));
                    g_GUI->SetTooltip(status);
                }
                break;

            case 2: // Exploit
                g_GUI->Checkbox(xorstr_("No Sway"),         xorstr_("Zero weapon sway"),     &exploit.no_sway);
                g_GUI->Checkbox(xorstr_("No Recoil"),       xorstr_("Zero free-aim recoil"), &exploit.no_recoil);
                g_GUI->Checkbox(xorstr_("Infinite Breath"), xorstr_("Lock breath stamina"),  &exploit.infinite_breath);
                break;

            case 3: // Colors
                g_GUI->ColorPicker(xorstr_("Enemy Color"),    &esp.col_enemy);
                g_GUI->ColorPicker(xorstr_("Team Color"),     &esp.col_team);
                g_GUI->ColorPicker(xorstr_("Wounded Color"),  &esp.col_wounded);
                g_GUI->ColorPicker(xorstr_("Text Color"),     &esp.col_text);
                g_GUI->ColorPicker(xorstr_("Snapline Color"), &esp.col_snap);
                break;

            case 4: // Settings
            {
                g_GUI->Checkbox(xorstr_("Debug Overlay"), xorstr_("Show camera / W2S / first target diagnostics (F8)"), &esp.draw_debug);

                // KMBox config
                g_GUI->TextInput(xorstr_("KMBox IP"),   aimbot.config.kmboxIp,   sizeof(aimbot.config.kmboxIp));
                g_GUI->TextInput(xorstr_("KMBox Port"), aimbot.config.kmboxPort, sizeof(aimbot.config.kmboxPort));
                g_GUI->TextInput(xorstr_("KMBox MAC"),  aimbot.config.kmboxMac,  sizeof(aimbot.config.kmboxMac));

                if (g_GUI->Button(xorstr_("Reconnect KMBox"))) {
                    aimbot.shutdown_kmbox();
                    if (aimbot.init_kmbox()) {
                        g_GUI->AddNotification(xorstr_("KMBox connected!"), 3.f);
                    } else {
                        g_GUI->AddNotification(xorstr_("KMBox connection failed"), 3.f);
                    }
                }

                if (g_GUI->Button(xorstr_("Save Config"))) {
                    if (cfg.save(CONFIG_PATH)) {
                        g_GUI->AddNotification(xorstr_("Config saved!"), 2.f);
                    }
                }
                if (g_GUI->Button(xorstr_("Load Config"))) {
                    if (cfg.load(CONFIG_PATH)) {
                        g_GUI->AddNotification(xorstr_("Config loaded!"), 2.f);
                    }
                }

                // Info
                char buf[128];
                sprintf_s(buf, xorstr_("Players: %d | Sens: %.2f | %s"),
                    (int)core.players().size(), core.sensitivity(),
                    core.is_ads() ? xorstr_("ADS") : xorstr_("HIP"));
                g_GUI->SetTooltip(buf);
            }
                break;
            }

            g_GUI->EndUI();
            g_GUI->EndFrame();

            // Cursor
            g_Renderer->DrawLine(g_GUI->m_Mouse.x, g_GUI->m_Mouse.y,
                g_GUI->m_Mouse.x, g_GUI->m_Mouse.y + 12, 1, WIDGET_COLOR, true);
            g_Renderer->DrawLine(g_GUI->m_Mouse.x, g_GUI->m_Mouse.y,
                g_GUI->m_Mouse.x + 12, g_GUI->m_Mouse.y, 1, WIDGET_COLOR, true);
        }

        g_Renderer->EndFrame();
        overlay.Present();
        std::this_thread::sleep_for(std::chrono::milliseconds(6));
    }

    // --- Cleanup ---
    g_running = false;
    exploit_thr.join();
    aimbot.shutdown_kmbox();
    g_GUI.reset();
    g_Renderer->Destroy();
    g_Renderer.reset();
    overlay.Destroy();
    drv.disconnect();

    return 0;
}
