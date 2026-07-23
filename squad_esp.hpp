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
    bool draw_weapon_info = true;
    bool draw_vehicles  = true;
    bool vehicle_chams  = true;   // cham boxes for vehicles
    bool draw_deployables = true;
    bool deploy_chams   = true;   // cham boxes for FOBs/deployables
    int  cham_style     = 1;      // 0 = 2D rect, 1 = 3D wireframe
    bool cham_fill      = false;  // translucent fill behind the box
    bool draw_role      = true;   // kit/role next to player name
    bool draw_enemy_weapon = true;
    bool draw_tickets   = true;
    bool team_check     = true;
    bool bot_check      = false;  // ON = hide bots, OFF = show bots (training dummies)
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

        sprintf_s(buf, "levelActors levels=%d total=%d soldiersFound=%d classified=%d unresolved=%d labels=%d",
            stats.level_count, stats.level_actor_count, stats.level_soldiers_found,
            stats.level_classified, stats.level_unresolved, (int)core.debug_entities().size());
        out << buf << "\n";

        sprintf_s(buf, "botStages matched=%d failPos=%d failHP=%d failBones=%d pushedBones=%d | ent veh=%d fob=%d dep=%d",
            stats.bot_matched, stats.bot_fail_pos, stats.bot_fail_hp,
            stats.bot_fail_bones, stats.bot_pushed_bones,
            stats.ent_vehicles, stats.ent_fobs, stats.ent_deploys);
        out << buf << "\n";

        sprintf_s(buf, "tickets teams=%d t0=%d/%d t1=%d/%d | localWeapon=%s mag=%d res=%d",
            core.ticket_team_count(),
            core.ticket_ids()[0], core.tickets()[0],
            core.ticket_ids()[1], core.tickets()[1],
            core.weapon_name().c_str(), core.ammo_mag(), core.ammo_reserve());
        out << buf << "\n";

        {
            uint64_t vp, en, st, inv, wp; std::string ec, sc;
            core.chain_diag(vp, en, st, ec, sc, inv, wp);
            sprintf_s(buf, "chain viewport=0x%llX engine=0x%llX(%s) settings=0x%llX(%s) inv=0x%llX weapon=0x%llX",
                vp, en, ec.c_str(), st, sc.c_str(), inv, wp);
            out << buf << "\n";
        }

        // nearest vehicle: dump all cham raw materials for exact diagnosis
        {
            const WorldEntity* nearest = nullptr;
            for (const auto& e : core.entities())
                if (e.type == EntType::Vehicle && (!nearest || e.dist < nearest->dist)) nearest = &e;
            if (nearest) {
                const auto& e = *nearest;
                sprintf_s(buf, "veh[0] pos=(%.0f,%.0f,%.0f) quat=(%.3f,%.3f,%.3f,%.3f) hasRot=%d hasBnd=%d",
                    e.position.x, e.position.y, e.position.z,
                    e.rotation.x, e.rotation.y, e.rotation.z, e.rotation.w,
                    e.has_rotation ? 1 : 0, e.has_bounds ? 1 : 0);
                out << buf << "\n";
                if (e.has_bounds) {
                    sprintf_s(buf, "veh[0] bndOrg=(%.0f,%.0f,%.0f) bndExt=(%.0f,%.0f,%.0f)",
                        e.bounds_origin.x, e.bounds_origin.y, e.bounds_origin.z,
                        e.bounds_extent.x, e.bounds_extent.y, e.bounds_extent.z);
                    out << buf << "\n";
                }
                // projected corners with root rotation, fixed size for reference
                sprintf_s(buf, "veh[0] wpCount=%d dist=%.0f", e.wp_count, e.dist);
                out << buf << "\n";
            }
        }

        out << "localClass=" << core.local_class_name() << "\n";
        out << "class census (top 40):\n" << core.level_class_census(40);
        out << "bot components:\n" << core.dump_bot_components(3);

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

        int pi = 0;
        for (const auto& p : core.players()) {
            if (pi++ >= 12) break;
            FVector2D root2d{}, head2d{};
            bool root_ok = core.world_to_screen(p.position, root2d);
            FVector head_world = p.head_pos;
            if (head_world.is_zero())
                head_world = { p.position.x, p.position.y, p.position.z + 175.0 };
            bool head_ok = core.world_to_screen(head_world, head2d);
            sprintf_s(buf, "p%d actor=0x%llX bot=%d team=%d dist=%.1f root(%.0f,%.0f,%.0f) ok=%d(%.0f,%.0f) head ok=%d(%.0f,%.0f)",
                pi - 1, p.actor, p.is_bot ? 1 : 0, p.team_id, p.distance,
                p.position.x, p.position.y, p.position.z,
                root_ok ? 1 : 0, root2d.x, root2d.y,
                head_ok ? 1 : 0, head2d.x, head2d.y);
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
            if (bot_check && p.is_bot) continue;
            if (p.distance > max_distance) continue;

            uint32_t color = is_enemy ? col_enemy : col_team;
            if (p.dying_flags & squad::WoundedBit) color = col_wounded;

            // Head screen (boneless targets: estimate head as root + 1.75m)
            FVector head_world = p.head_pos;
            if (head_world.is_zero())
                head_world = { p.position.x, p.position.y, p.position.z + 175.0 };
            FVector2D head_screen;
            if (!core.world_to_screen(head_world, head_screen)) continue;

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

            // Box from full skeleton projection (min/max of all drawn bones),
            // padded outward so it doesn't hug the skeleton. Fallback to the
            // head/feet box when bones aren't available.
            float box_left, box_top, box_w, box_h;
            bool box_ok = false;
            if (p.bones_valid) {
                using namespace squad::bones;
                static const int box_bones[] = {
                    Pelvis, Spine, Spine1, Spine2, Neck, Head,
                    R_Clavicle, R_UpperArm, R_Forearm, R_Hand,
                    L_Clavicle, L_UpperArm, L_Forearm, L_Hand,
                    L_Thigh, L_Calf, L_Foot, L_Toe0,
                    R_Thigh, R_Calf, R_Foot, R_Toe0,
                };
                float minx = 1e18f, miny = 1e18f, maxx = -1e18f, maxy = -1e18f;
                int cnt = 0;
                for (int b : box_bones) {
                    FVector2D s;
                    if (!core.world_to_screen(p.bones_ws[b], s)) continue;
                    if ((float)s.x < minx) minx = (float)s.x;
                    if ((float)s.x > maxx) maxx = (float)s.x;
                    if ((float)s.y < miny) miny = (float)s.y;
                    if ((float)s.y > maxy) maxy = (float)s.y;
                    cnt++;
                }
                if (cnt >= 4) {
                    box_w = maxx - minx;
                    box_h = maxy - miny;
                    float pad_x = box_w * 0.15f + 3.f;   // outward padding
                    float pad_y = box_h * 0.06f + 3.f;
                    box_left = minx - pad_x;
                    box_top  = miny - pad_y;
                    box_w += pad_x * 2.f;
                    box_h += pad_y * 2.f;
                    box_ok = true;
                }
            }
            if (!box_ok) {
                box_h = (float)fabs(foot_screen.y - head_screen.y);
                if (box_h < 5.f) continue;
                box_w = box_h * 0.60f;
                box_top = (float)(head_screen.y < foot_screen.y ? head_screen.y : foot_screen.y);
                box_left = (float)head_screen.x - box_w * 0.5f;
            }

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

                float bar_x = box_left - 1.f;   // 1px gap between bar and box
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

            // Role (kit) + enemy weapon, stacked above the box
            int info_y = (int)box_top - 16;
            if (draw_role && !p.role_name.empty()) {
                info_y -= 13;
                r->RenderText(p.role_name.c_str(), (int)box_left, info_y,
                              D3DCOLOR_ARGB(255, 255, 200, 80), true, 12);
            }
            if (draw_enemy_weapon && !p.weapon_name.empty()) {
                info_y -= 13;
                r->RenderText(p.weapon_name.c_str(), (int)box_left, info_y,
                              D3DCOLOR_ARGB(255, 255, 130, 130), true, 12);
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

            // On-screen entity labels: every classified actor in range gets
            // its class name + distance at its world position. Ground-truth
            // tool for "what class is that thing in front of me".
            char lbl[160];
            for (const auto& e : core.debug_entities()) {
                if (e.dist > 150.0) continue;
                FVector2D s;
                if (!core.world_to_screen(e.pos, s)) continue;
                sprintf_s(lbl, "%s [%.0fm]", e.cname.c_str(), e.dist);
                r->RenderText(lbl, (int)s.x, (int)s.y, D3DCOLOR_ARGB(255, 255, 255, 0), true, 13);
            }
        }

        // World entities (vehicles / FOBs / deployables)
        if (draw_vehicles || draw_deployables) {
            for (const auto& e : core.entities()) {
                bool is_veh = (e.type == EntType::Vehicle);
                if (is_veh && !draw_vehicles) continue;
                if (!is_veh && !draw_deployables) continue;
                if (e.dist > max_distance) continue;

                FVector2D s;
                if (!core.world_to_screen(e.position, s)) continue;

                bool friendly = (e.team == core.local_team());
                uint32_t col = friendly ? col_team
                    : (e.team <= 0 ? D3DCOLOR_ARGB(255, 255, 220, 60) : col_enemy);

                int half = is_veh ? 8 : 5;
                double cham_top = s.y - half, cham_cx = s.x;
                double e_bottom_y = s.y + half, e_left_x = s.x - half;
                bool box_drawn = false;

                // cham dimensions per entity type (half extents, cm)
                bool use_cham = e.has_rotation &&
                    ((is_veh && vehicle_chams) || (!is_veh && deploy_chams));
                double hf = 0, hr = 0, hh = 0;
                bool centered = false;  // real bounds are centered on bounds_origin
                FVector cham_center = e.position;
                if (use_cham) {
                    if (is_veh && e.has_bounds) {
                        hf = e.bounds_extent.x; hr = e.bounds_extent.y; hh = e.bounds_extent.z;
                        cham_center = e.bounds_origin;
                        centered = true;
                    }
                    else if (is_veh)                    { hf = 320; hr = 140; hh = 220; }
                    else if (e.type == EntType::Fob)    { hf = 150; hr = 150; hh = 220; }
                    else if (e.explosive_type > 0)      { hf = 50;  hr = 50;  hh = 60;  }
                    else                                { hf = 100; hr = 100; hh = 150; }
                }

                if (use_cham) {
                    const FQuat& q = e.rotation;   // root CTW rotation (proven by weak-point boxes)
                    auto rot_axis = [&](double ax, double ay, double az) {
                        // v' = q * v * q^-1
                        double ix = q.w * ax + q.y * az - q.z * ay;
                        double iy = q.w * ay + q.z * ax - q.x * az;
                        double iz = q.w * az + q.x * ay - q.y * ax;
                        double iw = -q.x * ax - q.y * ay - q.z * az;
                        FVector o;
                        o.x = ix * q.w + iw * -q.x + iy * -q.z - iz * -q.y;
                        o.y = iy * q.w + iw * -q.y + iz * -q.x - ix * -q.z;
                        o.z = iz * q.w + iw * -q.z + ix * -q.y - iy * -q.x;
                        return o;
                    };
                    FVector fwd = rot_axis(1, 0, 0), rgt = rot_axis(0, 1, 0), upv = rot_axis(0, 0, 1);

                    // project 8 corners: idx = (dx>0) + 2*(dy>0) + 4*(dz_top)
                    FVector2D pts[8];
                    bool ok[8]{};
                    double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
                    for (int dx = -1; dx <= 1; dx += 2)
                        for (int dy = -1; dy <= 1; dy += 2)
                            for (int dzt = 0; dzt <= 1; dzt++) {
                                double dz = centered ? (dzt ? 1.0 : -1.0) : (double)dzt;
                                FVector c = cham_center + fwd * (hf * dx) + rgt * (hr * dy) + upv * (hh * dz);
                                int idx = (dx > 0) + 2 * (dy > 0) + 4 * dzt;
                                ok[idx] = core.project_unclamped(c, pts[idx], true);
                                if (ok[idx]) {
                                    if (pts[idx].x < minx) minx = pts[idx].x;
                                    if (pts[idx].x > maxx) maxx = pts[idx].x;
                                    if (pts[idx].y < miny) miny = pts[idx].y;
                                    if (pts[idx].y > maxy) maxy = pts[idx].y;
                                }
                            }

                    if (maxx > minx && maxy > miny) {
                        // clamp rect to screen for fill/2D modes (giant rects
                        // from clamped corners would otherwise draw off-screen)
                        double rx1 = minx < 0 ? 0 : minx, ry1 = miny < 0 ? 0 : miny;
                        double rx2 = maxx > screenW ? (double)screenW : maxx;
                        double ry2 = maxy > screenH ? (double)screenH : maxy;
                        bool intersects = (rx2 > rx1 && ry2 > ry1);
                        if (cham_fill && intersects) {
                            uint32_t fill = (col & 0x00FFFFFF) | 0x40000000;  // 25% alpha
                            r->DrawFilledRect((int)rx1, (int)ry1, (int)(rx2 - rx1), (int)(ry2 - ry1), fill);
                        }
                        if (cham_style == 1) {
                            // 3D wireframe: 12 edges
                            static const int edges[12][2] = {
                                {0,1},{1,3},{3,2},{2,0},
                                {4,5},{5,7},{7,6},{6,4},
                                {0,4},{1,5},{2,6},{3,7},
                            };
                            for (auto& ed : edges)
                                if (ok[ed[0]] && ok[ed[1]])
                                    r->DrawLine((int)pts[ed[0]].x, (int)pts[ed[0]].y,
                                                (int)pts[ed[1]].x, (int)pts[ed[1]].y, 1, col, true);

                            // weak points: small red boxes (engine/track/ammo/turret)
                            if (is_veh) {
                                const uint32_t wp_col = D3DCOLOR_ARGB(255, 255, 40, 40);
                                for (int wi = 0; wi < e.wp_count; wi++) {
                                    double wf = 60, wr = 60, wh = 60;
                                    switch (e.wps[wi].type) {
                                    case 1: wf = 90;  wr = 70; wh = 70; break;  // engine
                                    case 2: wf = 120; wr = 40; wh = 50; break;  // track
                                    case 3: wf = 70;  wr = 70; wh = 70; break;  // ammo rack
                                    case 4: wf = 110; wr = 110; wh = 60; break; // turret
                                    }
                                    FVector2D wpts[8];
                                    bool wok[8]{};
                                    double wminx = 1e18, wminy = 1e18;
                                    for (int dx = -1; dx <= 1; dx += 2)
                                        for (int dy = -1; dy <= 1; dy += 2)
                                            for (int dzt = 0; dzt <= 1; dzt++) {
                                                double dz = dzt ? 1.0 : -1.0;
                                                FVector c = e.wps[wi].pos + fwd * (wf * dx) + rgt * (wr * dy) + upv * (wh * dz);
                                                int idx = (dx > 0) + 2 * (dy > 0) + 4 * dzt;
                                                wok[idx] = core.project_unclamped(c, wpts[idx]);
                                                if (wok[idx]) {
                                                    if (wpts[idx].x < wminx) wminx = wpts[idx].x;
                                                    if (wpts[idx].y < wminy) wminy = wpts[idx].y;
                                                }
                                            }
                                    for (auto& ed : edges)
                                        if (wok[ed[0]] && wok[ed[1]])
                                            r->DrawLine((int)wpts[ed[0]].x, (int)wpts[ed[0]].y,
                                                        (int)wpts[ed[1]].x, (int)wpts[ed[1]].y, 1, wp_col, true);
                                    if (wminx < 1e17)
                                        r->RenderText(e.wps[wi].cname, (int)wminx, (int)wminy - 13, wp_col, true, 11);
                                }
                            }
                        } else {
                            // 2D rect (clamped to screen)
                            if (intersects)
                                r->DrawRect((int)rx1, (int)ry1, (int)(rx2 - rx1), (int)(ry2 - ry1), 1, col, true);
                        }
                        cham_top = miny;
                        cham_cx = (minx + maxx) * 0.5;
                        box_drawn = true;
                    }
                    e_bottom_y = maxy;   // for text stacking below
                    e_left_x = minx;
                } else {
                    r->DrawRect((int)s.x - half, (int)s.y - half, half * 2, half * 2, 1, col, true);
                    box_drawn = true;
                    e_bottom_y = s.y + half;
                    e_left_x = s.x - half;
                }

                // text anchor: box bounds when drawn, entity point otherwise
                if (!box_drawn) { cham_top = s.y - half; cham_cx = s.x; }

                // --- text layout: everything stacked ABOVE the box ---
                int top_y = (int)cham_top - 16;

                // seat occupancy (e.g. 5/16)
                if (is_veh && e.seats_total > 0) {
                    char sb[24];
                    sprintf_s(sb, "%d/%d", e.seats_occupied, e.seats_total);
                    uint32_t scol = e.seats_occupied > 0 ? col_enemy : D3DCOLOR_ARGB(255, 120, 120, 120);
                    r->RenderText(sb, (int)cham_cx - 12, top_y, scol, true, 14);
                    top_y -= 14;
                }

                // name
                char nbuf[160];
                if (!e.name.empty()) {
                    int len = WideCharToMultiByte(CP_UTF8, 0, e.name.c_str(), -1, nbuf, 100, NULL, NULL);
                    if (len <= 0) nbuf[0] = 0;
                } else {
                    strcpy_s(nbuf, is_veh ? "Vehicle" : (e.type == EntType::Fob ? "FOB" : "Deployable"));
                }
                r->RenderText(nbuf, (int)cham_cx - (int)strlen(nbuf) * 3, top_y, col_text, true, 12);
                top_y -= 13;

                // hp + dist
                char line[200];
                if (e.max_health > 0.f)
                    sprintf_s(line, "%.0f%%  %.0fm", 100.f * e.health / e.max_health, e.dist);
                else
                    sprintf_s(line, "%.0fm", e.dist);
                r->RenderText(line, (int)cham_cx - (int)strlen(line) * 3, top_y, col_text, true, 12);
                top_y -= 13;

                if (e.type == EntType::Fob && e.fob_ammo >= 0.f) {
                    sprintf_s(line, "Ammo %.0f", e.fob_ammo);
                    r->RenderText(line, (int)cham_cx - (int)strlen(line) * 3, top_y, col_text, true, 12);
                }
            }
        }

        // Tickets HUD (top center)
        if (draw_tickets && core.ticket_team_count() > 0) {
            int x = screenW / 2 - 60 * core.ticket_team_count();
            for (int i = 0; i < core.ticket_team_count(); i++) {
                bool own = (core.ticket_ids()[i] == core.local_team());
                uint32_t c = own ? col_team : col_enemy;
                char tb[32];
                sprintf_s(tb, "T%d %d", core.ticket_ids()[i], core.tickets()[i]);
                r->RenderText(tb, x, 8, c, true, 16);
                x += 120;
            }
        }

        // Local weapon HUD (bottom center)
        if (draw_weapon_info) {
            char wbuf[128];
            const std::string& wn = core.weapon_name();
            if (!wn.empty() || core.ammo_mag() >= 0) {
                if (core.ammo_mag() >= 0)
                    sprintf_s(wbuf, "%s  |  %d + %d", wn.c_str(), core.ammo_mag(), core.ammo_reserve());
                else
                    sprintf_s(wbuf, "%s", wn.c_str());
                int tw = (int)strlen(wbuf) * 8;
                r->RenderText(wbuf, screenW / 2 - tw / 2, screenH - 60,
                              D3DCOLOR_ARGB(255, 255, 220, 120), true, 18);
            }
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
