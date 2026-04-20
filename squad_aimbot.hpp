#pragma once
#include "renderer.h"
#include "squad_core.hpp"
#include "squad_offsets.hpp"
#include "kmbox/kmboxNet.h"
#include "crypt/lazyimporter.hpp"
#include "crypt/xor.hpp"
#include <cmath>
#include <cfloat>
#include <random>
#include <algorithm>
#include <chrono>

// ============================================================================
// Squad Aimbot - KMBox Net B Pro output
// ViewAngle-based aiming: reads ASQSoldier::ControlRotation,
// uses runtime sensitivity fallback (until GameUserSettings chain is wired),
// calculates angle delta, converts to mouse counts, sends via KMBox
// ============================================================================

enum class SquadHitbox : int {
    Head = 0,
    Neck,
    Chest,
    Closest
};

struct AimbotConfig {
    bool  enabled       = false;
    bool  drawFov       = false;
    bool  teamCheck     = true;

    int   hitbox        = 0;        // SquadHitbox enum
    float fovSize       = 120.f;    // hip FOV (screen pixels)
    bool  useAdsFov     = true;
    float adsFovSize    = 60.f;     // ADS FOV

    float smoothX       = 5.f;
    float smoothY       = 5.f;
    int   aimKey1       = VK_RBUTTON;
    int   aimKey2       = 0;

    // Humanization
    bool  humanized     = false;
    float humanStrength = 0.5f;
    float humanCurve    = 0.6f;
    float humanJitter   = 0.3f;
    float humanTremor   = 0.2f;

    // Target lock
    bool  targetLock    = true;
    float targetSwitchDelay = 200.f; // ms

    // Sway compensation
    bool  swayCompensation = true;
    float swayScale        = 1.0f;

    // Distance
    float maxDistance    = 500.f;     // meters

    // Sensitivity override (0 = read from game)
    float sensFallback  = 1.0f;

    // KMBox
    char  kmboxIp[32]   = "192.168.2.188";
    char  kmboxPort[8]  = "2337";
    char  kmboxMac[16]  = "09A13CAB";
    bool  kmboxConnected = false;
};

class SquadAimbot {
public:
    AimbotConfig config;

    // Call once at startup
    bool init_kmbox() {
        int ret = kmNet_init(config.kmboxIp, config.kmboxPort, config.kmboxMac);
        config.kmboxConnected = (ret == 0);
        return config.kmboxConnected;
    }

    void shutdown_kmbox() {
        kmNet_cleanup();
        config.kmboxConnected = false;
    }

    // Draw FOV circle on screen
    void draw_fov(Renderer* r, int screenW, int screenH, bool isAds) {
        if (!config.enabled || !config.drawFov) return;
        float cx = screenW / 2.f;
        float cy = screenH / 2.f;
        float fov = (config.useAdsFov && isAds) ? config.adsFovSize : config.fovSize;
        if (fov < 5.f) fov = 5.f;
        r->DrawCircle((int)cx, (int)cy, fov, 64, D3DCOLOR_ARGB(80, 255, 255, 255));
    }

    // Main aimbot tick — call every frame
    void run(const SquadCore& core, int screenW, int screenH)
    {
        if (!config.enabled || !config.kmboxConnected) return;

        // Read game sensitivity (fallback to config only if settings chain failed)
        float gameSens = core.sensitivity();
        if (gameSens < 0.001f) gameSens = config.sensFallback;

        // Check aim key
        bool isAiming = false;
        if (config.aimKey1 && (LI_FN(GetAsyncKeyState)(config.aimKey1) & 0x8000)) isAiming = true;
        if (config.aimKey2 && (LI_FN(GetAsyncKeyState)(config.aimKey2) & 0x8000)) isAiming = true;
        if (!isAiming) { reset_state(); return; }

        // Active FOV
        bool isAds = core.is_ads();
        float activeFov = (config.useAdsFov && isAds) ? config.adsFovSize : config.fovSize;
        if (activeFov < 5.f) activeFov = 5.f;
        m_activeFov = activeFov;

        float centerX = screenW / 2.f;
        float centerY = screenH / 2.f;

        // Current view angles from memory
        FRotator viewRot = core.view_angles();
        FVector localPos = core.local_position();

        // --- Target selection (screen-space FOV check) ---
        float bestDist = FLT_MAX;
        FVector bestTargetPos{};
        uint64_t bestAddr = 0;
        bool lockedFound = false;

        // Try locked target first
        if (config.targetLock && m_lockedAddr != 0) {
            for (const auto& p : core.players()) {
                if (p.actor != m_lockedAddr) continue;
                if (!valid_target(p, core)) continue;
                FVector tpos = get_hitbox_pos(p, core);
                FVector2D sp;
                if (!core.world_to_screen(tpos, sp)) continue;
                float dx = (float)sp.x - centerX, dy = (float)sp.y - centerY;
                float d = sqrtf(dx * dx + dy * dy);
                if (d <= activeFov) {
                    bestTargetPos = tpos; bestAddr = p.actor; bestDist = d;
                    lockedFound = true;
                }
                break;
            }
        }

        if (!lockedFound) {
            m_lockedAddr = 0;
            for (const auto& p : core.players()) {
                if (!valid_target(p, core)) continue;
                FVector tpos = get_hitbox_pos(p, core);
                FVector2D sp;
                if (!core.world_to_screen(tpos, sp)) continue;
                float dx = (float)sp.x - centerX, dy = (float)sp.y - centerY;
                float d = sqrtf(dx * dx + dy * dy);
                if (d < bestDist && d <= activeFov) {
                    bestDist = d; bestTargetPos = tpos; bestAddr = p.actor;
                }
            }
        }

        if (bestAddr == 0) return;

        // Target switch delay
        double now = now_ms();
        if (bestAddr != m_prevTargetAddr) {
            m_targetSwitchTime = now;
            m_prevTargetAddr = bestAddr;
        }
        if (config.targetSwitchDelay > 0.f && (now - m_targetSwitchTime) < (double)config.targetSwitchDelay)
            return;
        m_lockedAddr = bestAddr;

        // --- Calculate angle to target ---
        // Squad uses UE5 double-precision: X=forward, Y=right, Z=up
        double dx = bestTargetPos.x - localPos.x;
        double dy = bestTargetPos.y - localPos.y;
        double dz = bestTargetPos.z - localPos.z;
        double hyp = sqrt(dx * dx + dy * dy);

        double targetPitch = -atan2(dz, hyp) * (180.0 / M_PI);
        double targetYaw   =  atan2(dy, dx)  * (180.0 / M_PI);

        // --- Sway / breathing compensation ---
        // Current muzzle direction is effectively ControlRotation + weapon punch/sway offsets.
        // If no_sway is enabled these values should already be near zero, so this becomes a no-op.
        double effectiveYaw = viewRot.yaw;
        double effectivePitch = viewRot.pitch;

        if (config.swayCompensation) {
            FRotator punch = core.local_weapon_punch_sway();
            FRotator align = core.local_weapon_punch_alignment();
            effectiveYaw   += (punch.yaw   + align.yaw)   * config.swayScale;
            effectivePitch += (punch.pitch + align.pitch) * config.swayScale;
        }

        // --- Angle delta ---
        double dYaw   = normalize_angle(targetYaw   - effectiveYaw);
        double dPitch = normalize_angle(targetPitch - effectivePitch);

        double angleDist = sqrt(dYaw * dYaw + dPitch * dPitch);
        if (angleDist < 0.02) return; // deadzone

        // --- Convert angle delta to mouse counts ---
        // UE5 Squad: ControlRotation is directly set by mouse input
        // 1 mouse count ≈ sensitivity * 0.07 degrees (UE5 default InputAxisScale)
        // This varies per game. Squad uses: InputYawScale = 2.5, InputPitchScale = -2.5
        // So 1 mouse count = sens * (1.0 / InputYawScale) * 0.07 ≈ sens * 0.028
        // If sensitivity reads as the in-game slider value (e.g., 1.0 = default),
        // we calibrate: degPerCount ≈ sensitivity * 0.044 (measured empirically)
        float degPerCount = gameSens * 0.044f;
        if (degPerCount < 0.0001f) degPerCount = 0.0001f;

        float rawX = (float)(-dYaw   / degPerCount);
        float rawY = (float)( dPitch / degPerCount);

        // --- Smoothing ---
        float moveX = 0.f, moveY = 0.f;
        if (config.humanized) {
            calc_human_move(rawX, rawY, sqrtf(rawX * rawX + rawY * rawY), moveX, moveY);
        } else {
            moveX = rawX / config.smoothX;
            moveY = rawY / config.smoothY;
        }

        if (fabsf(moveX) < 0.1f && fabsf(moveY) < 0.1f) return;

        // Clamp to kmbox range [-127, 127]
        int imx = (int)(moveX > 0 ? ceilf(moveX) : floorf(moveX));
        int imy = (int)(moveY > 0 ? ceilf(moveY) : floorf(moveY));
        if (imx > 127) imx = 127; if (imx < -127) imx = -127;
        if (imy > 127) imy = 127; if (imy < -127) imy = -127;

        kmNet_enc_mouse_move((short)imx, (short)imy);
    }

private:
    uint64_t m_lockedAddr = 0;
    uint64_t m_prevTargetAddr = 0;
    double   m_targetSwitchTime = 0;
    float    m_activeFov = 120.f;

    // Humanization state
    double   m_ouX = 0, m_ouY = 0;
    double   m_tremorPhaseX = 0, m_tremorPhaseY = 0;
    double   m_lastTickMs = 0;
    std::mt19937 m_rng{ std::random_device{}() };

    double now_ms() {
        using namespace std::chrono;
        return (double)duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count() / 1000.0;
    }

    double normalize_angle(double a) {
        while (a > 180.0) a -= 360.0;
        while (a < -180.0) a += 360.0;
        return a;
    }

    void reset_state() {
        m_lockedAddr = 0;
        m_ouX = m_ouY = 0;
        m_lastTickMs = 0;
    }

    bool valid_target(const PlayerData& p, const SquadCore& core) {
        if (!p.is_alive || p.health <= 0.f) return false;
        if (config.teamCheck && p.team_id == core.local_team()) return false;
        if (p.distance > config.maxDistance) return false;
        return true;
    }

    FVector get_hitbox_pos(const PlayerData& p, const SquadCore& core) {
        using namespace squad::bones;
        auto bone = [&](int idx) -> FVector { return core.get_bone_world(p, idx); };

        switch ((SquadHitbox)config.hitbox) {
        case SquadHitbox::Head:    return bone(Head);
        case SquadHitbox::Neck:    return bone(Neck);
        case SquadHitbox::Chest:   return bone(Spine2);
        case SquadHitbox::Closest: {
            // Pick closest bone to crosshair
            float centerX = 960.f, centerY = 540.f; // approximation
            float bestD = FLT_MAX;
            int bestIdx = Head;
            const int cands[] = { Head, Neck, Spine2 };
            for (int idx : cands) {
                FVector pos = bone(idx);
                FVector2D sp;
                if (!core.world_to_screen(pos, sp)) continue;
                float dx = (float)sp.x - centerX, dy = (float)sp.y - centerY;
                float d = dx * dx + dy * dy;
                if (d < bestD) { bestD = d; bestIdx = idx; }
            }
            return bone(bestIdx);
        }
        default: return bone(Head);
        }
    }

    // Ornstein-Uhlenbeck humanization (from Apex aimbot)
    void calc_human_move(float deltaX, float deltaY, float dist, float& outX, float& outY) {
        double now = now_ms();
        double dt = (m_lastTickMs > 0) ? (now - m_lastTickMs) / 1000.0 : 0.008;
        if (dt > 0.1) dt = 0.1;
        if (dt < 0.001) dt = 0.001;
        m_lastTickMs = now;

        float fovBase = (m_activeFov > 1.f) ? m_activeFov : config.fovSize;
        float normDist = dist / fovBase;
        if (normDist > 1.f) normDist = 1.f;
        float easeFactor = powf(normDist, config.humanCurve);
        float baseSmX = config.smoothX * (1.f + easeFactor * config.humanStrength * 1.2f);
        float baseSmY = config.smoothY * (1.f + easeFactor * config.humanStrength * 1.2f);
        float bx = deltaX / baseSmX;
        float by = deltaY / baseSmY;

        // OU noise
        float strength = config.humanStrength;
        float jitter = config.humanJitter;
        double theta = 8.0;
        double sigma = (double)(jitter * strength * 2.f);
        std::normal_distribution<double> gauss(0.0, 1.0);
        m_ouX += -theta * m_ouX * dt + sigma * sqrt(dt) * gauss(m_rng);
        m_ouY += -theta * m_ouY * dt + sigma * sqrt(dt) * gauss(m_rng);

        // Tremor
        float tremor = config.humanTremor * strength;
        m_tremorPhaseX += 10.0 * dt * 2.0 * M_PI;
        m_tremorPhaseY += 10.0 * dt * 2.0 * M_PI;
        float tx = tremor * (float)sin(m_tremorPhaseX) * 0.5f;
        float ty = tremor * (float)sin(m_tremorPhaseY * 1.13) * 0.5f;

        // Speed noise
        float speedNoise = strength * 0.15f * sqrtf(fabsf(bx) + fabsf(by));
        float snx = speedNoise * (float)gauss(m_rng);
        float sny = speedNoise * (float)gauss(m_rng);

        outX = bx + (float)m_ouX + tx + snx;
        outY = by + (float)m_ouY + ty + sny;
    }
};
