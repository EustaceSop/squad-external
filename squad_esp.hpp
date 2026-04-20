#pragma once
#include "squad_core.hpp"
#include "squad_offsets.hpp"
#include "renderer.h"
#include <string>
#include <cstdio>
#include <cmath>
#include <sstream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// Squad ESP - DX11 Renderer based
// Skeleton ESP, Box ESP, Health bar, Name + Distance, Snaplines
// ============================================================================

class SquadESP
{
public:
    // Feature toggles
    bool draw_skeleton  = true;
    bool draw_box       = true;
    bool draw_health    = true;
    bool draw_name      = true;
    bool draw_distance  = true;
    bool draw_snaplines = false;
    bool draw_head_dot  = true;
    bool team_check     = true;
    bool draw_debug     = false;
    float max_distance  = 1000.f;

    // Colors (ARGB)
    uint32_t col_enemy   = D3DCOLOR_ARGB(255, 255, 50, 50);
    uint32_t col_team    = D3DCOLOR_ARGB(255, 50, 255, 50);
    uint32_t col_wounded = D3DCOLOR_ARGB(255, 255, 150, 0);
    uint32_t col_snap    = D3DCOLOR_ARGB(128, 255, 255, 255);
    uint32_t col_text    = D3DCOLOR_ARGB(255, 255, 255, 255);

    std::string build_debug_text(const SquadCore& core, int screenW, int screenH) const
    {
        std::ostringstream out;
        char buf[256];
        out << "BUILD_TAG=playerarray_v2\n";
        const auto& cam = core.camera();
        const auto& view = core.view_angles();
        const auto& local = core.local_position();
        const auto sway = core.local_weapon_punch_sway();
        const auto align = core.local_weapon_punch_alignment();
        const auto& stats = core.debug_stats();

        sprintf_s(buf, "DBG players=%d localTeam=%d ads=%d focus=%.2f sens=%.3f s=%.3f steady=%.3f global=%.3f",
            (int)core.players().size(), core.local_team(), core.is_ads() ? 1 : 0, core.focus_zoom_alpha(),
            core.sensitivity(), core.soldier_sensitivity(), core.steady_sensitivity(), core.global_sensitivity());
        out << buf << "\n";

        sprintf_s(buf, "playerArray total=%d scanned=%d validPS=%d validTeam=%d validSoldier=%d validHP=%d validBones=%d pushed=%d",
            stats.player_array_count, stats.player_states_scanned, stats.with_valid_player_state, stats.valid_team,
            stats.valid_soldier, stats.valid_health, stats.valid_bones, stats.pushed_players);
        out << buf << "\n";

        sprintf_s(buf, "ptrs gameState=0x%llX localPawn=0x%llX localPS=0x%llX localPS->Soldier=0x%llX",
            stats.game_state_ptr, stats.local_pawn_ptr, stats.local_player_state_ptr, stats.local_ps_soldier_ptr);
        out << buf << "\n";

        sprintf_s(buf, "firstPS=0x%llX firstTeam=%d firstPS->Soldier=0x%llX",
            stats.first_player_state_ptr, stats.first_player_team, stats.first_player_soldier_ptr);
        out << buf << "\n";

        sprintf_s(buf, "cam loc %.1f %.1f %.1f | cam rot %.2f %.2f %.2f | fov %.2f",
            cam.location.x, cam.location.y, cam.location.z,
            cam.rotation.pitch, cam.rotation.yaw, cam.rotation.roll, cam.fov);
        out << buf << "\n";

        sprintf_s(buf, "view rot %.2f %.2f %.2f | local %.1f %.1f %.1f",
            view.pitch, view.yaw, view.roll,
            local.x, local.y, local.z);
        out << buf << "\n";

        sprintf_s(buf, "punch sway %.3f %.3f %.3f | align %.3f %.3f %.3f",
            sway.pitch, sway.yaw, sway.roll,
            align.pitch, align.yaw, align.roll);
        out << buf << "\n";

        double pitch_rad = cam.rotation.pitch * (M_PI / 180.0);
        double yaw_rad   = cam.rotation.yaw   * (M_PI / 180.0);
        double cp = cos(pitch_rad), sp = sin(pitch_rad);
        double cy = cos(yaw_rad),   sy = sin(yaw_rad);
        FVector forward = { cp * cy, cp * sy, sp };

        FVector2D ahead_screen{};
        bool ahead_ok = core.world_to_screen(cam.location + (forward * 1000.0), ahead_screen);
        sprintf_s(buf, "ahead test ok=%d -> (%.1f, %.1f) screenCenter=(%d,%d)", ahead_ok ? 1 : 0, ahead_screen.x, ahead_screen.y, screenW / 2, screenH / 2);
        out << buf << "\n";

        if (!core.players().empty()) {
            const auto& p = core.players().front();
            FVector2D root2d{}, head2d{};
            bool root_ok = core.world_to_screen(p.position, root2d);
            bool head_ok = core.world_to_screen(p.head_pos, head2d);

            sprintf_s(buf, "p0 actor=0x%llX team=%d hp=%.1f dist=%.1f alive=%d bones=%d",
                p.actor, p.team_id, p.health, p.distance, p.is_alive ? 1 : 0, p.bones_valid ? 1 : 0);
            out << buf << "\n";

            sprintf_s(buf, "p0 root %.1f %.1f %.1f -> ok=%d (%.1f, %.1f)",
                p.position.x, p.position.y, p.position.z, root_ok ? 1 : 0, root2d.x, root2d.y);
            out << buf << "\n";

            sprintf_s(buf, "p0 head %.1f %.1f %.1f -> ok=%d (%.1f, %.1f)",
                p.head_pos.x, p.head_pos.y, p.head_pos.z, head_ok ? 1 : 0, head2d.x, head2d.y);
            out << buf << "\n";
        }

        return out.str();
    }

    void render(Renderer* r, const SquadCore& core, int screenW, int screenH)
    {
        if (!r) return;

        for (const auto& p : core.players()) {
            bool is_enemy = (p.team_id != core.local_team());
            if (team_check && !is_enemy) continue;
            if (p.distance > max_distance) continue;

            uint32_t color = is_enemy ? col_enemy : col_team;
            if (p.dying_flags & 0x08) color = col_wounded;

            // Head screen
            FVector2D head_screen;
            if (!core.world_to_screen(p.head_pos, head_screen)) continue;

            // Foot/root screen - prefer bone-derived position over actor root
            FVector feet_world = p.position;
            if (p.bones_valid) {
                FVector root_bone = p.bones_ws[squad::bones::Root];
                FVector pelvis_bone = p.bones_ws[squad::bones::Pelvis];
                if (!root_bone.is_zero()) feet_world = root_bone;
                else if (!pelvis_bone.is_zero()) feet_world = pelvis_bone;
            }

            FVector2D foot_screen;
            if (!core.world_to_screen(feet_world, foot_screen)) continue;

            float box_h = (float)fabs(foot_screen.y - head_screen.y);
            if (box_h < 5.f) continue;
            float box_w = box_h * 0.60f;
            float box_top = (float)(head_screen.y < foot_screen.y ? head_screen.y : foot_screen.y);
            float box_left = (float)head_screen.x - box_w * 0.5f;

            // Skeleton
            if (draw_skeleton && p.bones_valid) {
                draw_skeleton_lines(r, core, p, color);
            }

            // Box
            if (draw_box) {
                r->DrawRect((int)box_left, (int)box_top, (int)box_w, (int)box_h, 1, color);
            }

            // Head dot
            if (draw_head_dot) {
                r->DrawFilledCircle((int)head_screen.x, (int)head_screen.y, 3.f, 12, color);
            }

            // Health bar
            if (draw_health) {
                float hp_pct = p.health / 100.f;
                if (hp_pct > 1.f) hp_pct = 1.f;
                if (hp_pct < 0.f) hp_pct = 0.f;

                float bar_x = box_left - 5.f;
                r->DrawFilledRect((int)bar_x - 3, (int)box_top, 3, (int)box_h, D3DCOLOR_ARGB(150, 0, 0, 0));

                uint32_t hp_col = D3DCOLOR_ARGB(255, (int)((1.f - hp_pct) * 255), (int)(hp_pct * 255), 0);
                float fill_h = box_h * hp_pct;
                r->DrawFilledRect((int)bar_x - 3, (int)(box_top + box_h - fill_h), 3, (int)fill_h, hp_col);
            }

            // Name
            if (draw_name && !p.name.empty()) {
                char nameBuf[128];
                int len = WideCharToMultiByte(CP_UTF8, 0, p.name.c_str(), -1, nameBuf, 127, NULL, NULL);
                if (len > 0) {
                    r->RenderText(nameBuf, (int)box_left, (int)box_top - 16, col_text, true, 12);
                }
            }

            // Distance
            if (draw_distance) {
                char buf[32];
                sprintf_s(buf, "%.0fm", p.distance);
                r->RenderText(buf, (int)box_left, (int)(box_top + box_h + 2), col_text, true, 12);
            }

            // Snaplines
            if (draw_snaplines) {
                r->DrawLine(screenW / 2, screenH, (int)foot_screen.x, (int)foot_screen.y, 1, col_snap);
            }
        }

        if (draw_debug) {
            render_debug(r, core, screenW, screenH);
        }
    }

private:
    void render_debug(Renderer* r, const SquadCore& core, int screenW, int screenH)
    {
        int x = 15;
        int y = 15;
        const int lh = 14;

        auto draw_line = [&](const char* text, uint32_t color = D3DCOLOR_ARGB(255, 255, 255, 0)) {
            r->RenderText(text, x, y, color, true, 12);
            y += lh;
        };

        char buf[256];
        const auto& cam = core.camera();

        r->DrawLine(screenW / 2 - 8, screenH / 2, screenW / 2 + 8, screenH / 2, 1, D3DCOLOR_ARGB(255, 0, 255, 255), true);
        r->DrawLine(screenW / 2, screenH / 2 - 8, screenW / 2, screenH / 2 + 8, 1, D3DCOLOR_ARGB(255, 0, 255, 255), true);

        std::string text = build_debug_text(core, screenW, screenH);
        size_t start = 0;
        while (start < text.size()) {
            size_t end = text.find('\n', start);
            std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!line.empty()) {
                bool bad = line.find("ok=0") != std::string::npos;
                draw_line(line.c_str(), bad ? D3DCOLOR_ARGB(255, 255, 80, 80) : D3DCOLOR_ARGB(255, 255, 255, 0));
            }
            if (end == std::string::npos) break;
            start = end + 1;
        }

        // Project synthetic points to diagnose W2S orientation
        double pitch_rad = cam.rotation.pitch * (M_PI / 180.0);
        double yaw_rad   = cam.rotation.yaw   * (M_PI / 180.0);
        double cp = cos(pitch_rad), sp = sin(pitch_rad);
        double cy = cos(yaw_rad),   sy = sin(yaw_rad);
        FVector forward = { cp * cy, cp * sy, sp };

        FVector2D ahead_screen{};
        bool ahead_ok = core.world_to_screen(cam.location + (forward * 1000.0), ahead_screen);
        sprintf_s(buf, "ahead test ok=%d -> (%.1f, %.1f) screenCenter=(%d,%d)", ahead_ok ? 1 : 0, ahead_screen.x, ahead_screen.y, screenW / 2, screenH / 2);
        draw_line(buf, ahead_ok ? D3DCOLOR_ARGB(255, 0, 255, 0) : D3DCOLOR_ARGB(255, 255, 80, 80));
        if (ahead_ok) {
            r->DrawFilledCircle((int)ahead_screen.x, (int)ahead_screen.y, 4.f, 10, D3DCOLOR_ARGB(255, 0, 255, 255));
        }

        if (!core.players().empty()) {
            const auto& p = core.players().front();
            FVector2D root2d{}, head2d{};
            bool root_ok = core.world_to_screen(p.position, root2d);
            bool head_ok = core.world_to_screen(p.head_pos, head2d);

            sprintf_s(buf, "p0 actor=0x%llX team=%d hp=%.1f dist=%.1f alive=%d bones=%d",
                p.actor, p.team_id, p.health, p.distance, p.is_alive ? 1 : 0, p.bones_valid ? 1 : 0);
            draw_line(buf);

            sprintf_s(buf, "p0 root %.1f %.1f %.1f -> ok=%d (%.1f, %.1f)",
                p.position.x, p.position.y, p.position.z, root_ok ? 1 : 0, root2d.x, root2d.y);
            draw_line(buf, root_ok ? D3DCOLOR_ARGB(255, 0, 255, 0) : D3DCOLOR_ARGB(255, 255, 80, 80));

            sprintf_s(buf, "p0 head %.1f %.1f %.1f -> ok=%d (%.1f, %.1f)",
                p.head_pos.x, p.head_pos.y, p.head_pos.z, head_ok ? 1 : 0, head2d.x, head2d.y);
            draw_line(buf, head_ok ? D3DCOLOR_ARGB(255, 0, 255, 0) : D3DCOLOR_ARGB(255, 255, 80, 80));

            if (root_ok) r->DrawFilledCircle((int)root2d.x, (int)root2d.y, 4.f, 10, D3DCOLOR_ARGB(255, 255, 255, 0));
            if (head_ok) r->DrawFilledCircle((int)head2d.x, (int)head2d.y, 4.f, 10, D3DCOLOR_ARGB(255, 255, 0, 255));
        }
    }

    void draw_skeleton_lines(Renderer* r, const SquadCore& core, const PlayerData& p, uint32_t color)
    {
        using namespace squad::bones;

        auto bone_line = [&](int from, int to) {
            FVector2D s1, s2;
            if (!core.world_to_screen(p.bones_ws[from], s1)) return;
            if (!core.world_to_screen(p.bones_ws[to], s2)) return;
            r->DrawBoneLine((float)s1.x, (float)s1.y, (float)s2.x, (float)s2.y, color, 1.5f);
        };

        // Spine
        bone_line(Pelvis, Spine); bone_line(Spine, Spine1);
        bone_line(Spine1, Spine2); bone_line(Spine2, Neck); bone_line(Neck, Head);
        // Right arm
        bone_line(Spine2, R_Clavicle); bone_line(R_Clavicle, R_UpperArm);
        bone_line(R_UpperArm, R_Forearm); bone_line(R_Forearm, R_Hand);
        // Left arm
        bone_line(Spine2, L_Clavicle); bone_line(L_Clavicle, L_UpperArm);
        bone_line(L_UpperArm, L_Forearm); bone_line(L_Forearm, L_Hand);
        // Right leg
        bone_line(Pelvis, R_Thigh); bone_line(R_Thigh, R_Calf); bone_line(R_Calf, R_Foot);
        // Left leg
        bone_line(Pelvis, L_Thigh); bone_line(L_Thigh, L_Calf); bone_line(L_Calf, L_Foot);
    }
};
