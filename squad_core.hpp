#pragma once
#include "squad_driver.hpp"
#include "squad_structs.hpp"
#include "squad_offsets.hpp"
#include "squad_names.hpp"
#include <vector>
#include <string>
#include <cmath>
#include <unordered_map>
#include <algorithm>
#include <chrono>

// ============================================================================
// Squad Core - World traversal, actor loop, bone reading, W2S
// External read via Evoria kernel driver
// ============================================================================

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// Per-player cached data
// ---------------------------------------------------------------------------
struct PlayerData
{
    uint64_t    actor;
    uint64_t    mesh;           // USkeletalMeshComponent*
    uint64_t    player_state;
    FVector     position;       // root component world position
    FVector     head_pos;       // bone head world position
    float       health;
    int32_t     team_id;
    uint8_t     dying_flags;    // bit0=dying, bit2=wounded (v10.5.1)
    bool        is_alive;
    bool    is_visible;     // placeholder for future LOS check
    bool    is_bot = false; // no PlayerState (AI pawn, e.g. ASQBot training dummy)
    double      distance;       // meters from local player
    std::wstring name;

    // Bone world positions (only the ones we need for skeleton ESP)
    FVector bones_ws[141];      // full bone array world positions
    bool    bones_valid;

    std::string role_name;      // kit/role (Medic, Rifleman...) via CurrentRole
    std::string weapon_name;    // current weapon class name via CurrentHeldWeapon
};

// ---------------------------------------------------------------------------
// World entities: vehicles / FOBs / deployables (level-enumerated, structural)
// ---------------------------------------------------------------------------
enum class EntType : uint8_t { Vehicle = 2, Deployable = 3, Fob = 4 };

struct WorldEntity
{
    EntType     type;
    uint64_t    actor;
    FVector     position;
    FQuat       rotation{};         // vehicles: for oriented cham box
    bool        has_rotation = false;
    float       health = -1.f;
    float       max_health = -1.f;
    int32_t     team = -1;          // 0 neutral / 1 / 2, -1 unknown
    float       fob_ammo = -1.f;
    int         seats_occupied = -1;    // vehicles: occupied seats
    int         seats_total = -1;       // vehicles: seat capacity
    int         explosive_type = 0;     // deployables: mine/IED type (0 = none)
    FVector     bounds_origin{};        // vehicles: real bounds center (world)
    FVector     bounds_extent{};        // vehicles: real bounds half extents
    bool        has_bounds = false;

    // weak points (engine / track / ammo / turret): world pos + type + class name
    struct WpPos { FVector pos; uint8_t type; char cname[28]; };
    int         wp_count = 0;
    WpPos       wps[10];

    FQuat       box_rotation{};         // cham axes from the visual mesh (differs from root on some vehicles)
    bool        has_box_rotation = false;

    std::wstring name;
    double      dist = 0.0;
};

struct CoreDebugStats
{
    int player_array_count = 0;
    int player_states_scanned = 0;
    int with_valid_player_state = 0;
    int valid_team = 0;
    int valid_soldier = 0;
    int valid_health = 0;
    int valid_bones = 0;
    int pushed_players = 0;
    uint64_t game_state_ptr = 0;
    uint64_t local_player_state_ptr = 0;
    uint64_t local_pawn_ptr = 0;
    uint64_t local_ps_soldier_ptr = 0;
    uint64_t first_player_state_ptr = 0;
    uint64_t first_player_soldier_ptr = 0;
    int first_player_team = 0;
    int level_actor_count = 0;
    int level_count = 0;
    int level_soldiers_found = 0;
    int level_classified = 0;
    int level_unresolved = 0;
    int bot_matched = 0;
    int bot_fail_pos = 0;
    int bot_fail_hp = 0;
    int bot_fail_bones = 0;
    int bot_pushed_bones = 0;
    int ent_vehicles = 0;
    int ent_fobs = 0;
    int ent_deploys = 0;
    int tickets[4] = { 0, 0, 0, 0 };
    int ticket_teams = 0;
};

// ---------------------------------------------------------------------------
// Camera data for W2S
// ---------------------------------------------------------------------------
struct CameraData
{
    FVector  location;
    FRotator rotation;
    float    fov;
};

// ---------------------------------------------------------------------------
// SquadCore
// ---------------------------------------------------------------------------
class SquadCore
{
public:
    SquadCore(SquadDriver& drv) : m_drv(drv) {}

    // -----------------------------------------------------------------------
    // Update all game state (call once per frame)
    // -----------------------------------------------------------------------
    bool update(int screen_w, int screen_h)
    {
        m_screen_w = screen_w;
        m_screen_h = screen_h;
        m_players.clear();
        m_entities.clear();
        m_debug_stats = {};

        if (!m_names_tried) {   // name resolver needed early (settings/weapon)
            m_names_tried = true;
            m_names.init(m_drv, m_drv.base());
        }

        if (!read_world()) return false;
        if (!read_local_player()) return false;
        if (!read_camera()) return false;
        read_local_extras();
        read_players();
        read_level_soldiers();

        return true;
    }

    // Accessors
    const std::vector<PlayerData>& players() const { return m_players; }
    const std::vector<WorldEntity>& entities() const { return m_entities; }
    const CameraData& camera() const { return m_camera; }
    int32_t local_team() const { return m_local_team; }
    uint64_t local_pawn() const { return m_local_pawn; }
    uint64_t local_controller() const { return m_local_controller; }
    uint64_t local_player_state() const { return m_local_player_state; }

    // View angles from ASQSoldier::ControlRotation (double precision)
    FRotator view_angles() const { return m_view_angles; }

    // Active sensitivity derived from USQGameUserSettings
    float sensitivity() const { return m_sensitivity; }

    float soldier_sensitivity() const { return m_soldier_sensitivity; }
    float steady_sensitivity() const { return m_steady_aim_sensitivity; }
    float global_sensitivity() const { return m_global_sensitivity; }

    // Is local player ADS?
    bool is_ads() const { return m_is_ads; }
    bool is_focusing() const { return m_is_focusing; }
    float focus_zoom_alpha() const { return m_focus_zoom_alpha; }

    // Local weapon HUD info
    const std::string& weapon_name() const { return m_weapon_name; }
    int ammo_mag() const { return m_ammo_mag; }
    int ammo_reserve() const { return m_ammo_reserve; }

    // Team tickets (IndexedTeamStates order; count = ticket_teams)
    const int* tickets() const { return m_tickets; }
    const int* ticket_ids() const { return m_ticket_ids; }
    int ticket_team_count() const { return m_ticket_teams; }

    // diagnostics: settings + weapon chain pointers
    void chain_diag(uint64_t& vp, uint64_t& en, uint64_t& st, std::string& ec, std::string& sc,
                    uint64_t& inv, uint64_t& wp) const
    {
        vp = m_dbg_viewport; en = m_dbg_engine; st = m_dbg_settings;
        ec = m_dbg_engine_cls; sc = m_dbg_settings_cls;
        inv = m_dbg_inv; wp = m_dbg_weapon;
    }

    // Current local sway/punch values
    FRotator local_weapon_punch_sway() const { return m_weapon_punch_sway; }
    FRotator local_weapon_punch_alignment() const { return m_weapon_punch_alignment; }

    // Local player world position
    FVector local_position() const { return m_local_position; }
    const CoreDebugStats& debug_stats() const { return m_debug_stats; }

    // -----------------------------------------------------------------------
    // World-to-Screen (UE camera convention)
    // Axes from FRotationTranslationMatrix (engine source, stable UE4->UE5):
    //   forward = (CP*CY, CP*SY, SP)
    //   right   = (SR*SP*CY - CR*SY, SR*SP*SY + CR*CY, -SR*CP)
    //   up      = (-CR*SP*CY - SR*SY, -CR*SP*SY + SR*CY, CR*CP)
    // Projection matches UE reversed-Z perspective with horizontal FOV:
    // both axes scale by (screenW/2) / tan(fov/2)  -- NOT half height.
    // -----------------------------------------------------------------------
    bool world_to_screen(const FVector& world, FVector2D& screen) const
    {
        double pitch_rad = m_camera.rotation.pitch * (M_PI / 180.0);
        double yaw_rad   = m_camera.rotation.yaw   * (M_PI / 180.0);
        double roll_rad  = m_camera.rotation.roll  * (M_PI / 180.0);

        double cp = cos(pitch_rad), sp = sin(pitch_rad);
        double cy = cos(yaw_rad),   sy = sin(yaw_rad);
        double cr = cos(roll_rad),  sr = sin(roll_rad);

        FVector forward = { cp * cy, cp * sy, sp };
        FVector right   = { sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp };
        FVector up      = { -cr * sp * cy - sr * sy, -cr * sp * sy + sr * cy, cr * cp };

        FVector delta = world - m_camera.location;

        double dot_forward = delta.dot(forward);
        if (dot_forward <= 0.1) return false;  // behind camera

        double dot_right = delta.dot(right);
        double dot_up    = delta.dot(up);

        // UE FOV is horizontal (degrees); identical scale factor for both axes
        double scale = (m_screen_w * 0.5) / tan(m_camera.fov * (M_PI / 360.0));

        screen.x = m_screen_w * 0.5 + (dot_right / dot_forward) * scale;
        screen.y = m_screen_h * 0.5 - (dot_up    / dot_forward) * scale;

        return (screen.x >= -50 && screen.x <= m_screen_w + 50 &&
                 screen.y >= -50 && screen.y <= m_screen_h + 50);
    }

    // Projection without screen-margin culling (cham corners). With
    // clamp_behind=true, points behind the camera are projected at near depth
    // instead of being rejected - box edges crossing the view plane stay drawn.
    bool project_unclamped(const FVector& world, FVector2D& screen, bool clamp_behind = false) const
    {
        double pitch_rad = m_camera.rotation.pitch * (M_PI / 180.0);
        double yaw_rad   = m_camera.rotation.yaw   * (M_PI / 180.0);
        double roll_rad  = m_camera.rotation.roll  * (M_PI / 180.0);

        double cp = cos(pitch_rad), sp = sin(pitch_rad);
        double cy = cos(yaw_rad),   sy = sin(yaw_rad);
        double cr = cos(roll_rad),  sr = sin(roll_rad);

        FVector forward = { cp * cy, cp * sy, sp };
        FVector right   = { sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp };
        FVector up      = { -cr * sp * cy - sr * sy, -cr * sp * sy + sr * cy, cr * cp };

        FVector delta = world - m_camera.location;
        double dot_forward = delta.dot(forward);
        if (dot_forward <= 0.1) {
            if (!clamp_behind) return false;
            dot_forward = 0.1;
        }

        double scale = (m_screen_w * 0.5) / tan(m_camera.fov * (M_PI / 360.0));
        double px = m_screen_w * 0.5 + (delta.dot(right) / dot_forward) * scale;
        double py = m_screen_h * 0.5 - (delta.dot(up)    / dot_forward) * scale;
        // clamp to sane range for int conversion in draw calls
        if (px > 100000.0) px = 100000.0; else if (px < -100000.0) px = -100000.0;
        if (py > 100000.0) py = 100000.0; else if (py < -100000.0) py = -100000.0;
        screen.x = px;
        screen.y = py;
        return true;
    }

    // -----------------------------------------------------------------------
    // Get bone world position for a player
    // -----------------------------------------------------------------------
    FVector get_bone_world(const PlayerData& p, int bone_index) const
    {
        if (!p.bones_valid || bone_index < 0 || bone_index > 140)
            return {};
        return p.bones_ws[bone_index];
    }

private:
    SquadDriver& m_drv;

    // World state
    uint64_t m_world = 0;
    uint64_t m_game_instance = 0;
    uint64_t m_local_player = 0;
    uint64_t m_local_controller = 0;
    uint64_t m_local_pawn = 0;
    uint64_t m_local_player_state = 0;
    uint64_t m_camera_manager = 0;
    uint64_t m_persistent_level = 0;
    int32_t  m_local_team = -1;

    CameraData m_camera{};
    FRotator   m_view_angles{};
    FRotator   m_weapon_punch_sway{};
    FRotator   m_weapon_punch_alignment{};
    FVector    m_local_position{};
    float      m_sensitivity = 1.0f;
    float      m_global_sensitivity = 1.0f;
    float      m_steady_aim_sensitivity = 1.0f;
    float      m_soldier_sensitivity = 1.0f;
    float      m_focus_zoom_alpha = 0.0f;

    // Local weapon HUD
    std::string m_weapon_name;
    int         m_ammo_mag = -1;
    int         m_ammo_reserve = -1;

    // diagnostics for chains (printed in debug text)
    uint64_t    m_dbg_viewport = 0, m_dbg_engine = 0, m_dbg_settings = 0;
    std::string m_dbg_engine_cls, m_dbg_settings_cls;
    uint64_t    m_dbg_inv = 0, m_dbg_weapon = 0;

    // GObjects resolver (TWeakObjectPtr) + name caches
    uint64_t    m_gobj_chunks = 0;
    int32_t     m_gobj_num = 0;
    int         m_tickets[4] = { 0, 0, 0, 0 };
    int         m_ticket_ids[4] = { 0, 0, 0, 0 };
    int         m_ticket_teams = 0;
    std::unordered_map<uint64_t, std::string> m_obj_name_cache;  // role/weapon obj -> display name

    // Level-actor soldier enumeration (bots have no PlayerState).
    // Soldier detection is STRUCTURAL: probe actor+CharacterMesh for a valid
    // 100..300-bone FTransform array. No class names or hierarchy needed -
    // pop-up targets/projectiles/scenery fail the probe naturally.
    NameResolver m_names;
    bool m_names_tried = false;

    std::unordered_map<uint64_t, uint64_t> m_actor_uclass;  // actor -> UClass* (labels/names)
    std::unordered_map<uint64_t, std::string> m_uclass_name; // uclass -> class name
    // category: 0=none, 1=soldier, 2=vehicle, 3=deployable, 4=fob
    struct ActorProbe { uint8_t category = 0; uint64_t mesh = 0; };
    std::unordered_map<uint64_t, ActorProbe> m_actor_probe;  // actor -> structural result
    // vehicle actor -> cached weak point components (comp ptr, type, class name)
    struct WpComp { uint64_t comp; uint8_t type; std::string cname; };
    std::unordered_map<uint64_t, std::vector<WpComp>> m_vehicle_weakpoints;
    std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> m_actor_fail; // cooldowns
    std::chrono::steady_clock::time_point m_class_cache_time = std::chrono::steady_clock::now();
    size_t m_scan_cursor = 0;

public:
    // Debug entity (classified actor + live position) for on-screen labels
    struct DebugEntity {
        uint64_t    actor;
        std::string cname;
        FVector     pos;
        double      dist;
    };
    void set_entity_labels(bool on) { m_entity_labels = on; }
    const std::vector<DebugEntity>& debug_entities() const { return m_debug_entities; }

private:
    bool m_entity_labels = false;
    std::vector<DebugEntity> m_debug_entities;
    std::vector<WorldEntity> m_entities;

public:
    // Top-N class census of level actors (debug)
    std::string level_class_census(int top_n = 20) const
    {
        std::unordered_map<std::string, int> counts;
        for (const auto& kv : m_actor_uclass) {
            auto it = m_uclass_name.find(kv.second);
            if (it != m_uclass_name.end() && !it->second.empty())
                counts[it->second]++;
        }
        std::vector<std::pair<std::string, int>> v(counts.begin(), counts.end());
        std::sort(v.begin(), v.end(), [](const auto& a, const auto& b){ return a.second > b.second; });
        std::string out;
        char buf[256];
        int n = 0;
        for (const auto& kv : v) {
            if (n++ >= top_n) break;
            snprintf(buf, sizeof(buf), "  %5d  %s\n", kv.second, kv.first.c_str());
            out += buf;
        }
        return out;
    }
    std::string local_class_name() const
    {
        std::string s;
        if (m_local_pawn && m_names.ok()) m_names.class_name(m_drv, m_local_pawn, s);
        return s;
    }

    // Debug: dump root component + attach children (class name + world pos)
    // for the first few bot players - finds where the visual mesh really is.
    std::string dump_bot_components(int max_bots = 3) const
    {
        std::string out;
        char buf[256];
        int dumped = 0;
        for (const auto& p : m_players) {
            if (!p.is_bot || dumped >= max_bots) continue;
            dumped++;
            uint64_t root = m_drv.read<uint64_t>(p.actor + squad::RootComponent);
            snprintf(buf, sizeof(buf), "bot actor=0x%llX root=0x%llX\n",
                     (unsigned long long)p.actor, (unsigned long long)root);
            out += buf;
            if (!root) continue;

            // USceneComponent::AttachChildren TArray at 0xE8 [SDK]
            struct { uint64_t data; int32_t count; int32_t max; } children{};
            m_drv.read_raw(root + 0xE8, &children, sizeof(children));
            snprintf(buf, sizeof(buf), "  children=%d\n", children.count);
            out += buf;
            if (!children.data || children.count <= 0 || children.count > 64) continue;

            std::vector<uint64_t> kids(children.count);
            m_drv.read_raw(children.data, kids.data(), children.count * 8);
            for (uint64_t kid : kids) {
                if (kid < 0x10000 || kid > 0x800000000000ULL) continue;
                std::string cname;
                m_names.class_name(m_drv, kid, cname);
                FVector t{};
                m_drv.read_raw(kid + squad::ComponentToWorld + 0x20, &t, sizeof(t));
                snprintf(buf, sizeof(buf), "    %-40s (%.0f, %.0f, %.0f)\n",
                         cname.c_str(), t.x, t.y, t.z);
                out += buf;
            }
        }
        return out;
    }

private:
    bool       m_is_ads = false;
    bool       m_is_focusing = false;
    CoreDebugStats m_debug_stats{};
    std::vector<PlayerData> m_players;
    int m_screen_w = 1920;
    int m_screen_h = 1080;

    // -----------------------------------------------------------------------
    // Read UWorld chain
    // -----------------------------------------------------------------------
    bool read_world()
    {
        uint64_t base = m_drv.base();

        // Squad: GWorld is a direct pointer
        m_world = m_drv.read<uint64_t>(base + squad::GWorld);
        if (!m_world) return false;

        m_persistent_level = m_drv.read<uint64_t>(m_world + squad::PersistentLevel);
        if (!m_persistent_level) return false;

        return true;
    }

    // -----------------------------------------------------------------------
    // Read local player info
    // -----------------------------------------------------------------------
    bool read_local_player()
    {
        uint64_t base = m_drv.base();

        // UWorld -> OwningGameInstance -> ULocalPlayers[0] -> PlayerController
        m_game_instance = m_drv.read<uint64_t>(m_world + squad::OwningGameInstance);
        if (!m_game_instance) return false;

        // ULocalPlayers is a TArray<ULocalPlayer*>
        uint64_t local_players_data = m_drv.read<uint64_t>(m_game_instance + squad::ULocalPlayers);
        if (!local_players_data) return false;

        m_local_player = m_drv.read<uint64_t>(local_players_data); // [0]
        if (!m_local_player) return false;

        m_local_controller = m_drv.read<uint64_t>(m_local_player + squad::PlayerController);
        if (!m_local_controller) return false;

        m_local_pawn = m_drv.read<uint64_t>(m_local_controller + squad::AcknowledgedPawn);
        // Pawn can be null when in spectator/deploy screen

        m_camera_manager = m_drv.read<uint64_t>(m_local_controller + squad::PlayerCameraManager);

        // Read local team
        if (m_local_pawn) {
            m_local_player_state = m_drv.read<uint64_t>(m_local_pawn + squad::PlayerState);
            m_debug_stats.local_pawn_ptr = m_local_pawn;
            m_debug_stats.local_player_state_ptr = m_local_player_state;

            if (m_local_player_state) {
                m_local_team = m_drv.read<int32_t>(m_local_player_state + squad::PS_TeamId);
                m_debug_stats.local_ps_soldier_ptr = m_drv.read<uint64_t>(m_local_player_state + squad::PS_Soldier);
            }
        }

        return true;
    }

    // -----------------------------------------------------------------------
    // Read camera (from PlayerCameraManager)
    // -----------------------------------------------------------------------
    bool read_camera()
    {
        if (!m_camera_manager) return false;

        // CameraCachePrivate -> FCameraCacheEntry -> FMinimalViewInfo POV
        // FMinimalViewInfo layout: Location(FVector, 0x0), Rotation(FRotator, 0x18), FOV(float, 0x30)
        uint64_t cache_addr = m_camera_manager + squad::CameraCacheEntry + squad::CachePOV;

        // Batch read location + rotation + fov in one call
        struct {
            FVector  location;      // 0x00 - 24 bytes (double)
            FRotator rotation;      // 0x18 - 24 bytes (double)
            float    fov;           // 0x30 - 4 bytes
        } pov{};

        if (!m_drv.read_raw(cache_addr, &pov, sizeof(pov))) return false;

        m_camera.location = pov.location;
        m_camera.rotation = pov.rotation;
        m_camera.fov = pov.fov;

        // Sanity check
        if (m_camera.fov < 10.f || m_camera.fov > 170.f) m_camera.fov = 90.f;

        return true;
    }

    // -----------------------------------------------------------------------
    // Read local player extras: view angles, sensitivity, ADS, position
    // -----------------------------------------------------------------------
    void read_local_extras()
    {
        if (!m_local_pawn) return;

        // Read soldier-side aim state / sway in one shot
        std::vector<SquadDriver::BatchReq> local_batch = {
            { m_local_pawn + squad::SoldierControlRotation, sizeof(FRotator), &m_view_angles },
            { m_local_pawn + squad::WeaponPunchSway, sizeof(FRotator), &m_weapon_punch_sway },
            { m_local_pawn + squad::WeaponPunchAlignment, sizeof(FRotator), &m_weapon_punch_alignment },
            { m_local_pawn + squad::FocusZoomAlpha, sizeof(float), &m_focus_zoom_alpha },
            { m_local_pawn + squad::IsFocusing, sizeof(bool), &m_is_focusing },
        };
        m_drv.batch_read(local_batch);

        // Sensitivity: ULocalPlayer -> ViewportClient(0x78) -> Outer(0x20) is
        // UEngine -> GameUserSettings(0x2C8). The settings object is validated
        // by class name ("GameUserSettings") before trusting the floats.
        if (m_names.ok() && m_local_player) {
            uint64_t viewport = m_drv.read<uint64_t>(m_local_player + squad::ViewportClient);
            uint64_t engine   = viewport ? m_drv.read<uint64_t>(viewport + squad::ObjOuterPrivate) : 0;
            uint64_t settings = engine ? m_drv.read<uint64_t>(engine + squad::EngineGameUserSettings) : 0;
            m_dbg_viewport = viewport; m_dbg_engine = engine; m_dbg_settings = settings;
            if (m_dbg_engine_cls.empty() && engine)
                m_names.class_name(m_drv, engine, m_dbg_engine_cls);
            if (m_dbg_settings_cls.empty() && settings)
                m_names.class_name(m_drv, settings, m_dbg_settings_cls);
            if (settings) {
                uint64_t uc = m_drv.read<uint64_t>(settings + squad::ObjClassPrivate);
                std::string nm;
                if (uc) {
                    uint32_t id = m_drv.read<uint32_t>(uc + squad::FieldNamePrivate);
                    m_names.resolve(m_drv, id, nm);
                }
                if (nm.find("GameUserSettings") != std::string::npos) {
                    float g = 1.f, st = 1.f, so = 1.f;
                    std::vector<SquadDriver::BatchReq> sb = {
                        { settings + squad::SettingsGlobalSensitivity, sizeof(float), &g },
                        { settings + squad::SettingsSteadyAimSensitivity, sizeof(float), &st },
                        { settings + squad::SettingsSoldierSensitivity, sizeof(float), &so },
                    };
                    if (m_drv.batch_read(sb)) {
                        if (g > 0.001f && g < 100.f)  m_global_sensitivity = g;
                        if (st > 0.001f && st < 100.f) m_steady_aim_sensitivity = st;
                        if (so > 0.001f && so < 100.f) m_soldier_sensitivity = so;
                    }
                }
            }
        }

        // Practical ADS proxy for external: focus state / zoom alpha.
        // This is sufficient for FOV switching and steady-aim sensitivity selection.
        m_is_ads = m_is_focusing || (m_focus_zoom_alpha > 0.05f);

        float base_sens = (m_soldier_sensitivity > 0.001f) ? m_soldier_sensitivity : m_global_sensitivity;
        m_sensitivity = m_is_ads
            ? ((m_steady_aim_sensitivity > 0.001f) ? m_steady_aim_sensitivity : base_sens)
            : base_sens;

        // Local position
        uint64_t local_root = m_drv.read<uint64_t>(m_local_pawn + squad::RootComponent);
        if (local_root) {
            m_drv.read_raw(local_root + squad::ComponentToWorld + 0x20, &m_local_position, sizeof(FVector));
        }

        read_local_weapon();
    }

    // -----------------------------------------------------------------------
    // Local weapon: inventory -> current weapon -> class name + magazine data
    // -----------------------------------------------------------------------
    void read_local_weapon()
    {
        m_weapon_name.clear();
        m_ammo_mag = -1;
        m_ammo_reserve = -1;

        uint64_t inv = m_drv.read<uint64_t>(m_local_pawn + squad::InventoryComponent);
        if (!inv) return;
        uint64_t weapon = m_drv.read<uint64_t>(inv + squad::InvCurrentWeapon);
        m_dbg_inv = inv; m_dbg_weapon = weapon;
        if (!weapon) return;

        if (m_names.ok()) {
            std::string nm;
            if (m_names.class_name(m_drv, weapon, nm) && !nm.empty()) {
                // strip BP_ prefix / _C suffix for display
                if (nm.rfind("BP_", 0) == 0) nm = nm.substr(3);
                if (nm.size() > 2 && nm.compare(nm.size() - 2, 2, "_C") == 0) nm.resize(nm.size() - 2);
                m_weapon_name = nm;
            }
        }

        // Magazines (ASQWeapon only; bandages/binocs fail the sanity check)
        struct { uint64_t data; int32_t count; int32_t max; } mags{};
        if (!m_drv.read_raw(weapon + squad::WeaponMagazines, &mags, sizeof(mags))) return;
        if (!mags.data || mags.count <= 0 || mags.count > 64) return;

        std::vector<int32_t> rounds((size_t)mags.count * 2);
        if (!m_drv.read_raw(mags.data, rounds.data(), (size_t)mags.count * 8)) return;

        m_ammo_mag = rounds[1];  // Magazines[0].RemainingRounds
        if (m_ammo_mag < 0 || m_ammo_mag > 500) { m_ammo_mag = -1; m_ammo_reserve = -1; return; }
        int reserve = 0;
        for (int i = 1; i < mags.count; i++) reserve += rounds[(size_t)i * 2 + 1];
        m_ammo_reserve = reserve;
    }

    // -----------------------------------------------------------------------
    // Read all players from AGameStateBase::PlayerArray
    // -----------------------------------------------------------------------
    void read_players()
    {
        uint64_t game_state = m_drv.read<uint64_t>(m_world + squad::GameState);
        if (!game_state) return;
        m_debug_stats.game_state_ptr = game_state;
        read_tickets(game_state);

        struct { uint64_t data; int32_t count; int32_t max; } player_array{};
        m_drv.read_raw(game_state + squad::PlayerArray, &player_array, sizeof(player_array));

        if (!player_array.data || player_array.count <= 0 || player_array.count > 512) return;
        m_debug_stats.player_array_count = player_array.count;

        int count = player_array.count;
        std::vector<uint64_t> player_states(count);
        m_drv.read_raw(player_array.data, player_states.data(), count * sizeof(uint64_t));

        for (int i = 0; i < count; i++) {
            uint64_t ps = player_states[i];
            if (!ps) continue;

            if (!m_debug_stats.first_player_state_ptr) {
                m_debug_stats.first_player_state_ptr = ps;
                m_debug_stats.first_player_team = m_drv.read<int32_t>(ps + squad::PS_TeamId);
                m_debug_stats.first_player_soldier_ptr = m_drv.read<uint64_t>(ps + squad::PS_Soldier);
            }

            m_debug_stats.player_states_scanned++;
            process_player_state(ps);
        }
    }

    // -----------------------------------------------------------------------
    // Process a single PlayerState - resolve Soldier and read player data
    // -----------------------------------------------------------------------
    void process_player_state(uint64_t ps)
    {
        if (!ps) return;
        m_debug_stats.with_valid_player_state++;

        // Read team ID from ASQPlayerState
        int32_t team_id = m_drv.read<int32_t>(ps + squad::PS_TeamId);
        if (team_id <= 0 || team_id > 32) return;
        m_debug_stats.valid_team++;

        // Resolve the actual ASQSoldier from PlayerState
        uint64_t actor = m_drv.read<uint64_t>(ps + squad::PS_Soldier);
        if (!actor) return;
        if (actor == m_local_pawn) return;
        m_debug_stats.valid_soldier++;

        // Back-check: actor should point to the same PlayerState
        uint64_t actor_ps = m_drv.read<uint64_t>(actor + squad::PlayerState);
        if (!actor_ps || actor_ps != ps) return;

        // Read mesh component (ACharacter::Mesh at 0x330)
        uint64_t mesh = m_drv.read<uint64_t>(actor + squad::CharacterMesh);
        if (!mesh) return;

        // Read root component for position
        uint64_t root = m_drv.read<uint64_t>(actor + squad::RootComponent);
        if (!root) return;

        // Batch read: position, health, dying_flags
        FVector position{};
        float health = 0.f;
        uint8_t dying_flags = 0;

        std::vector<SquadDriver::BatchReq> batch = {
            { root + squad::ComponentToWorld + 0x20, sizeof(FVector), &position },  // translation from ComponentToWorld
            { actor + squad::Health, sizeof(float), &health },
            { actor + squad::DyingFlags, sizeof(uint8_t), &dying_flags },
        };
        m_drv.batch_read(batch);

        // Filter: skip dead/invalid
        bool is_dying = (dying_flags & 0x01) != 0;
        if (health < 0.f || health > 500.f) return;
        m_debug_stats.valid_health++;
        if (is_dying && health <= 0.f) return;

        // Skip zero position
        if (position.is_zero()) return;

        // Build player data
        PlayerData pd{};
        pd.actor = actor;
        pd.mesh = mesh;
        pd.player_state = ps;
        pd.position = position;
        pd.health = health;
        pd.team_id = team_id;
        pd.dying_flags = dying_flags;
        pd.is_alive = !is_dying;

        // Distance from local
        if (m_local_pawn) {
            uint64_t local_root = m_drv.read<uint64_t>(m_local_pawn + squad::RootComponent);
            if (local_root) {
                FVector local_pos{};
                m_drv.read_raw(local_root + squad::ComponentToWorld + 0x20, &local_pos, sizeof(FVector));
                pd.distance = position.distance_m(local_pos);
            }
        }

        // Read player name
        pd.name = m_drv.read_fstring(ps + squad::PlayerNamePrivate);

        // Role (kit) name via CurrentRole asset
        pd.role_name = read_role_name(ps);

        // Current weapon via TWeakObjectPtr -> GObjects
        {
            uint64_t w = resolve_weak(actor + 0x311C);  // CurrentHeldWeapon
            if (w) pd.weapon_name = obj_display_name(w);
        }

        // Read bones
        pd.bones_valid = read_bones(mesh, pd);
        if (!pd.bones_valid || pd.head_pos.is_zero()) return;
        m_debug_stats.valid_bones++;

        m_players.push_back(std::move(pd));
        m_debug_stats.pushed_players++;
    }

    // -----------------------------------------------------------------------
    // Enumerate soldier-class actors from ULevel::Actors.
    // Bots (ASQBot / AI pawns) have no PlayerState and never appear in
    // PlayerArray - this is the only way to see training-range dummies.
    // -----------------------------------------------------------------------
    // -----------------------------------------------------------------------
    // Staged structural probe: soldier -> vehicle -> deployable/FOB.
    // -----------------------------------------------------------------------
    ActorProbe probe_soldier(uint64_t actor)
    {
        ActorProbe p{};
        uint64_t mesh = m_drv.read<uint64_t>(actor + squad::CharacterMesh);
        if (mesh > 0x10000 && mesh < 0x800000000000ULL) {
            struct { uint64_t data; int32_t count; int32_t capacity; } bh{};
            if (m_drv.read_raw(mesh + squad::BoneArray, &bh, sizeof(bh)) &&
                bh.data > 0x10000 && bh.data < 0x800000000000ULL &&
                bh.count >= 100 && bh.count <= 300 &&
                bh.capacity >= bh.count && bh.capacity <= bh.count + 64) {
                FTransform t0{};
                if (m_drv.read_raw(bh.data, &t0, sizeof(t0))) {
                    double n = std::sqrt(t0.rotation.x*t0.rotation.x + t0.rotation.y*t0.rotation.y +
                                         t0.rotation.z*t0.rotation.z + t0.rotation.w*t0.rotation.w);
                    auto sc = [](double v){ return v > 0.001 && v < 100.0; };
                    if (n > 0.9 && n < 1.1 && sc(t0.scale3d.x) && sc(t0.scale3d.y) && sc(t0.scale3d.z)) {
                        p.category = 1;
                        p.mesh = mesh;
                        return p;
                    }
                }
            }
        }

        // vehicle: ASQVehicle::VehicleSeats TArray with 1..16 valid components
        {
            struct { uint64_t data; int32_t count; int32_t max; } seats{};
            if (m_drv.read_raw(actor + squad::VehicleSeats, &seats, sizeof(seats)) &&
                seats.data > 0x10000 && seats.data < 0x800000000000ULL &&
                seats.count >= 1 && seats.count <= 16) {
                uint64_t s0 = m_drv.read<uint64_t>(seats.data);
                if (s0 > 0x10000 && s0 < 0x800000000000ULL) {
                    // confirm health pattern (reduces false positives)
                    float hp = 0.f, mhp = 0.f;
                    std::vector<SquadDriver::BatchReq> b = {
                        { actor + squad::VehicleHealth, sizeof(float), &hp },
                        { actor + squad::VehicleMaxHealth, sizeof(float), &mhp },
                    };
                    if (m_drv.batch_read(b) && mhp > 0.f && mhp < 100000.f && hp >= 0.f && hp <= mhp) {
                        p.category = 2;
                        return p;
                    }
                }
            }
        }

        // deployable / FOB: bPlaced && team 1..2 && maxhealth sane
        // Team(0x2E0 int32) / bIsFob(0x2E4) / bPlaced(0x2E5) are contiguous
        {
            struct { int32_t team; uint8_t is_fob; uint8_t placed; uint8_t pad[2]; } dp{};
            if (m_drv.read_raw(actor + squad::DeployableTeam, &dp, 8) &&
                dp.placed && dp.team >= 1 && dp.team <= 2) {
                float mhp = m_drv.read<float>(actor + squad::DeployableMaxHealth);
                if (mhp > 0.f && mhp < 1000000.f) {
                    p.category = dp.is_fob ? 4 : 3;
                    return p;
                }
            }
        }
        return p;
    }

    // -----------------------------------------------------------------------
    // GObjects / TWeakObjectPtr resolution
    // TUObjectArray: chunks ptr array at +0, NumElements at +0x14,
    // item = chunk + (idx % 0x10000) * 0x18, Object at item + 0x8
    // -----------------------------------------------------------------------
    uint64_t gobject_by_index(int32_t idx)
    {
        if (idx < 0) return 0;
        if (!m_gobj_chunks) {
            m_gobj_chunks = m_drv.read<uint64_t>(m_drv.base() + squad::GObjects);
            m_gobj_num    = m_drv.read<int32_t>(m_drv.base() + squad::GObjects + 0x14);
        }
        if (!m_gobj_chunks || idx >= m_gobj_num) return 0;
        uint64_t chunk = m_drv.read<uint64_t>(m_gobj_chunks + (uint64_t)(idx >> 16) * 8);
        if (!chunk) return 0;
        return m_drv.read<uint64_t>(chunk + (uint64_t)(idx & 0xFFFF) * 0x18 + 0x8);
    }

    uint64_t resolve_weak(uint64_t addr)
    {
        struct { int32_t index; int32_t serial; } wp{};
        if (!m_drv.read_raw(addr, &wp, sizeof(wp))) return 0;
        return gobject_by_index(wp.index);
    }

    // Display name for a UObject via its class name (cached, BP_/_C stripped)
    std::string obj_display_name(uint64_t obj)
    {
        auto it = m_obj_name_cache.find(obj);
        if (it != m_obj_name_cache.end()) return it->second;
        std::string nm;
        if (m_names.ok()) m_names.class_name(m_drv, obj, nm);
        if (nm.rfind("BP_", 0) == 0) nm = nm.substr(3);
        if (nm.size() > 2 && nm.compare(nm.size() - 2, 2, "_C") == 0) nm.resize(nm.size() - 2);
        m_obj_name_cache.emplace(obj, nm);
        return nm;
    }

    // Role name from ASQPlayerState::CurrentRole (role asset UObject name)
    std::string read_role_name(uint64_t ps)
    {
        uint64_t role = m_drv.read<uint64_t>(ps + 0x810);  // CurrentRole USQRoleSettings*
        if (role < 0x10000 || role > 0x800000000000ULL) return "";
        auto it = m_obj_name_cache.find(role);
        if (it != m_obj_name_cache.end()) return it->second;
        std::string nm;
        if (m_names.ok()) {
            uint32_t id = m_drv.read<uint32_t>(role + squad::FieldNamePrivate); // asset's own name
            m_names.resolve(m_drv, id, nm);
            if (nm.rfind("BP_", 0) == 0) nm = nm.substr(3);
            if (nm.size() > 2 && nm.compare(nm.size() - 2, 2, "_C") == 0) nm.resize(nm.size() - 2);
        }
        m_obj_name_cache.emplace(role, nm);
        return nm;
    }

    // Class name of a UClass object (for labels/census)
    std::string uclass_name(uint64_t uclass)
    {
        std::string nm;
        uint32_t id = m_drv.read<uint32_t>(uclass + 0x18);  // UField::NamePrivate (FName)
        m_names.resolve(m_drv, id, nm);
        return nm;
    }

    // Team tickets: GameState -> IndexedTeamStates(0x3F8) -> Tickets(0x2B8), ID(0x2E8)
    void read_tickets(uint64_t game_state)
    {
        m_ticket_teams = 0;
        struct { uint64_t data; int32_t count; int32_t max; } ts{};
        if (!m_drv.read_raw(game_state + 0x3F8, &ts, sizeof(ts))) return;
        if (!ts.data || ts.count <= 0 || ts.count > 4) return;
        std::vector<uint64_t> teams(ts.count);
        if (!m_drv.read_raw(ts.data, teams.data(), ts.count * 8)) return;
        for (uint64_t t : teams) {
            if (!t || m_ticket_teams >= 4) continue;
            int i = m_ticket_teams++;
            m_ticket_ids[i] = m_drv.read<int32_t>(t + 0x2E8);
            m_tickets[i]    = m_drv.read<int32_t>(t + 0x2B8);
        }
    }

    void read_level_soldiers()
    {
        if (!m_persistent_level) return;

        if (!m_names_tried) {
            m_names_tried = true;
            m_names.init(m_drv, m_drv.base());
        }
        if (!m_names.ok()) return;

        // Gather actors from ALL loaded levels. Tutorial bots spawn in
        // streamed sub-levels - PersistentLevel alone only has static scenery.
        std::vector<uint64_t> levels;
        levels.push_back(m_persistent_level);
        struct { uint64_t data; int32_t count; int32_t max; } lv{};
        if (m_drv.read_raw(m_world + squad::WorldLevels, &lv, sizeof(lv)) &&
            lv.data && lv.count > 0 && lv.count < 1024) {
            std::vector<uint64_t> larr(lv.count);
            if (m_drv.read_raw(lv.data, larr.data(), (size_t)lv.count * 8)) {
                for (uint64_t l : larr)
                    if (l && l != m_persistent_level) levels.push_back(l);
            }
        }
        m_debug_stats.level_count = (int)levels.size();

        std::vector<uint64_t> arr;
        for (uint64_t lvl : levels) {
            struct { uint64_t data; int32_t count; int32_t max; } actors{};
            if (!m_drv.read_raw(lvl + squad::LevelActors, &actors, sizeof(actors))) continue;
            if (!actors.data || actors.count <= 0 || actors.count > 65536) continue;
            size_t old = arr.size();
            arr.resize(old + actors.count);
            if (!m_drv.read_raw(actors.data, arr.data() + old, (size_t)actors.count * 8)) {
                arr.resize(old);
                continue;
            }
        }
        if (arr.empty()) return;
        m_debug_stats.level_actor_count = (int)arr.size();

        int classify_budget = 64;  // incremental: avoid first-frame resolve storm

        // periodic cache reset: UE can reuse freed actor addresses
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - m_class_cache_time).count() > 60) {
            m_actor_uclass.clear();
            m_uclass_name.clear();
            m_actor_probe.clear();
            m_actor_fail.clear();
            m_class_cache_time = now;
        }

        m_debug_entities.clear();
        std::vector<std::pair<uint64_t, std::string>> label_actors;
        if (m_entity_labels) label_actors.reserve(arr.size());

        // Round-robin scan with shared incremental budget. Failures get a
        // cooldown so they never starve the budget (that starvation caused
        // the label flickering).
        const size_t total = arr.size();
        size_t scanned = 0;
        for (; scanned < total; scanned++) {
            uint64_t actor = arr[(m_scan_cursor + scanned) % total];
            if (actor < 0x10000 || actor > 0x800000000000ULL) continue;
            if (actor == m_local_pawn) continue;

            // skip actors already tracked via PlayerArray
            bool dup = false;
            for (const auto& p : m_players) if (p.actor == actor) { dup = true; break; }
            if (dup) continue;

            // --- staged structural probe (cached) ---
            auto pit = m_actor_probe.find(actor);
            if (pit == m_actor_probe.end()) {
                auto fit = m_actor_fail.find(actor);
                if (fit != m_actor_fail.end() &&
                    std::chrono::duration_cast<std::chrono::seconds>(now - fit->second).count() < 3)
                    goto skip_actor;
                if (classify_budget <= 0) break;
                classify_budget--;
                // result (including genuine negatives) is cached; the 60s
                // reset handles any transient misreads
                pit = m_actor_probe.emplace(actor, probe_soldier(actor)).first;
            }

            switch (pit->second.category) {
            case 1:
                m_debug_stats.level_classified++;
                process_soldier_actor(actor, pit->second.mesh);
                break;
            case 2:
            case 3:
            case 4:
                process_entity_actor(actor, pit->second.category);
                break;
            default: break;
            }

        skip_actor:
            // --- label naming (best-effort, independent of soldier probe) ---
            if (m_entity_labels) {
                std::string cname;
                auto uit = m_actor_uclass.find(actor);
                if (uit != m_actor_uclass.end()) {
                    auto nit = m_uclass_name.find(uit->second);
                    if (nit != m_uclass_name.end()) cname = nit->second;
                }
                if (!cname.empty())
                    label_actors.emplace_back(actor, std::move(cname));
            }
        }
        m_scan_cursor = (scanned >= total) ? 0 : (m_scan_cursor + scanned) % total;

        // Lazily resolve class names for label display (small extra budget)
        if (m_entity_labels) {
            int name_budget = 32;
            for (uint64_t actor : arr) {
                if (name_budget <= 0) break;
                if (actor < 0x10000 || actor > 0x800000000000ULL || actor == m_local_pawn) continue;
                if (m_actor_uclass.find(actor) != m_actor_uclass.end()) continue;
                uint64_t uc = m_drv.read<uint64_t>(actor + 0x10);
                if (uc < 0x10000 || uc > 0x800000000000ULL) continue;
                m_actor_uclass.emplace(actor, uc);
                if (m_uclass_name.find(uc) == m_uclass_name.end()) {
                    std::string nm = uclass_name(uc);
                    if (!nm.empty()) m_uclass_name.emplace(uc, std::move(nm));
                }
                name_budget--;
            }
        }

        // On-screen debug labels: batch-read positions of all classified actors
        if (m_entity_labels && !label_actors.empty()) {
            const size_t n = label_actors.size();
            std::vector<uint64_t> roots(n, 0);
            for (size_t base = 0; base < n; base += 16) {
                std::vector<SquadDriver::BatchReq> req;
                size_t end = (base + 16 < n) ? base + 16 : n;
                for (size_t i = base; i < end; i++)
                    req.push_back({ label_actors[i].first + squad::RootComponent, sizeof(uint64_t), &roots[i] });
                m_drv.batch_read(req);
            }
            std::vector<FVector> positions(n);
            for (size_t base = 0; base < n; base += 16) {
                std::vector<SquadDriver::BatchReq> req;
                size_t end = (base + 16 < n) ? base + 16 : n;
                for (size_t i = base; i < end; i++)
                    if (roots[i])
                        req.push_back({ roots[i] + squad::ComponentToWorld + 0x20, sizeof(FVector), &positions[i] });
                if (!req.empty()) m_drv.batch_read(req);
            }
            for (size_t i = 0; i < n; i++) {
                if (!roots[i] || positions[i].is_zero()) continue;
                DebugEntity e;
                e.actor = label_actors[i].first;
                e.cname = std::move(label_actors[i].second);
                e.pos   = positions[i];
                e.dist  = positions[i].distance_m(m_local_position);
                m_debug_entities.push_back(std::move(e));
            }
        }
    }

    // -----------------------------------------------------------------------
    // Discover vehicle weak-point components by walking AttachChildren and
    // classifying each child by class name. USQVehicleComponent-derived
    // classes are UStaticMeshComponents (engine/tracks/turret/ammo).
    // -----------------------------------------------------------------------
    FVector quat_rotate(const FQuat& q, const FVector& v) const
    {
        double ix = q.w * v.x + q.y * v.z - q.z * v.y;
        double iy = q.w * v.y + q.z * v.x - q.x * v.z;
        double iz = q.w * v.z + q.x * v.y - q.y * v.x;
        double iw = -q.x * v.x - q.y * v.y - q.z * v.z;
        FVector r;
        r.x = ix * q.w + iw * -q.x + iy * -q.z - iz * -q.y;
        r.y = iy * q.w + iw * -q.y + iz * -q.x - ix * -q.z;
        r.z = iz * q.w + iw * -q.z + ix * -q.y - iy * -q.x;
        return r;
    }

    // Weak point world position: prefer RelativeLocation composed with the
    // vehicle root transform (logic comps have stale ComponentToWorld)
    FVector weakpoint_world(uint64_t comp, uint64_t root, const FVector& veh_pos, const FQuat& veh_rot)
    {
        uint64_t parent = m_drv.read<uint64_t>(comp + 0x110);  // AttachParent
        if (parent == root) {
            FVector rel{};
            if (m_drv.read_raw(comp + squad::RelativeLocation, &rel, sizeof(rel)))
                return veh_pos + quat_rotate(veh_rot, rel);
        }
        FVector wp{};
        m_drv.read_raw(comp + squad::ComponentToWorld + 0x20, &wp, sizeof(wp));
        return wp;
    }

    void scan_weakpoint_children(uint64_t comp, int depth, std::vector<WpComp>& found)
    {
        if (depth > 2 || found.size() >= 10) return;
        struct { uint64_t data; int32_t count; int32_t max; } kids{};
        if (!m_drv.read_raw(comp + 0xE8, &kids, sizeof(kids))) return;   // AttachChildren
        if (!kids.data || kids.count <= 0 || kids.count > 64) return;
        std::vector<uint64_t> arr(kids.count);
        if (!m_drv.read_raw(kids.data, arr.data(), kids.count * 8)) return;
        for (uint64_t kid : arr) {
            if (kid < 0x10000 || kid > 0x800000000000ULL) continue;
            std::string nm;
            m_names.class_name(m_drv, kid, nm);
            uint8_t type = 0;
            if (nm.find("Ammocook") == std::string::npos &&
                nm.find("Audio") == std::string::npos &&
                nm.find("State") == std::string::npos) {
                if (nm.find("Engine") != std::string::npos) type = 1;
                else if (nm.find("Track") != std::string::npos) type = 2;
                else if (nm.find("Ammo") != std::string::npos) type = 3;
                else if (nm.find("Turret") != std::string::npos) type = 4;
            }
            if (type) {
                found.push_back({ kid, type, nm });
                if (found.size() >= 10) return;
            }
            scan_weakpoint_children(kid, depth + 1, found);
        }
    }

    void discover_weakpoints(uint64_t actor, uint64_t root)
    {
        if (!m_names.ok()) { m_vehicle_weakpoints[actor] = {}; return; }
        std::vector<WpComp> found;
        scan_weakpoint_children(root, 0, found);
        m_vehicle_weakpoints[actor] = std::move(found);  // cache negative too
    }

    // -----------------------------------------------------------------------
    // Process a vehicle / deployable / FOB actor into the entity list
    // -----------------------------------------------------------------------
    void process_entity_actor(uint64_t actor, uint8_t category)
    {
        uint64_t root = m_drv.read<uint64_t>(actor + squad::RootComponent);
        if (!root) return;

        FVector position{};
        if (!m_drv.read_raw(root + squad::ComponentToWorld + 0x20, &position, sizeof(position))) return;
        if (position.is_zero()) return;

        WorldEntity e{};
        e.actor = actor;
        e.type = (EntType)category;
        e.position = position;
        if (!m_local_position.is_zero())
            e.dist = position.distance_m(m_local_position);

        // full CTW rotation for oriented cham box (all entity types)
        {
            FTransform ctw{};
            if (m_drv.read_raw(root + squad::ComponentToWorld, &ctw, sizeof(ctw))) {
                e.rotation = ctw.rotation;
                e.has_rotation = true;
            }
        }

        if (category == 2) {            // vehicle
            uint8_t team = 0;
            std::vector<SquadDriver::BatchReq> b = {
                { actor + squad::VehicleHealth, sizeof(float), &e.health },
                { actor + squad::VehicleMaxHealth, sizeof(float), &e.max_health },
                { actor + squad::PawnTeam, sizeof(uint8_t), &team },
            };
            m_drv.batch_read(b);
            e.team = (team <= 2) ? (int32_t)team : -1;
            // Bounds SIZE only: accept a component's CachedWorldOrLocalSpaceBounds
            // (0x858) only if its own CTW translation is within 3m of the root
            // (proves it's the live visual mesh, not a stale logic comp).
            // Center/axes ALWAYS come from the root CTW - that is proven correct
            // by the weak-point boxes.
            {
                struct { FVector origin; FVector extent; double radius; } bs{};
                auto read_bounds = [&](uint64_t comp, FVector& org, FVector& ext, double& mag) -> bool {
                    if (!m_drv.read_raw(comp + 0x858, &bs, sizeof(bs))) return false;
                    auto ok = [](double v){ return v > 20.0 && v < 4000.0; };
                    mag = std::sqrt(bs.extent.x*bs.extent.x + bs.extent.y*bs.extent.y + bs.extent.z*bs.extent.z);
                    if (!ok(bs.extent.x) || !ok(bs.extent.y) || !ok(bs.extent.z)) return false;
                    if (bs.radius <= 0.0 || mag <= 0.0) return false;
                    if (std::fabs(bs.radius - mag) / mag >= 0.5) return false;
                    org = bs.origin; ext = bs.extent;
                    return true;
                };
                auto comp_near_root = [&](uint64_t comp) -> bool {
                    FVector t{};
                    if (!m_drv.read_raw(comp + squad::ComponentToWorld + 0x20, &t, sizeof(t))) return false;
                    return t.distance_m(position) < 300.0;
                };

                uint64_t bcomp = 0;
                FVector borg{}, bext{}; double bmag = 0.0;
                if (read_bounds(root, borg, bext, bmag) && comp_near_root(root)) {
                    bcomp = root;
                } else {
                    struct { uint64_t data; int32_t count; int32_t max; } kids{};
                    if (m_drv.read_raw(root + 0xE8, &kids, sizeof(kids)) &&
                        kids.data && kids.count > 0 && kids.count <= 64) {
                        std::vector<uint64_t> arr(kids.count);
                        if (m_drv.read_raw(kids.data, arr.data(), kids.count * 8)) {
                            for (uint64_t kid : arr) {
                                if (kid < 0x10000 || kid > 0x800000000000ULL) continue;
                                if (read_bounds(kid, borg, bext, bmag) && comp_near_root(kid)) { bcomp = kid; break; }
                            }
                        }
                    }
                }

                if (bcomp) {
                    e.bounds_extent = bext;
                    // bounds origin: near actor => world-space, else local offset
                    // composed with the ROOT rotation (never the mesh comp's CTW)
                    if (borg.distance_m(position) < bmag * 2.0 + 300.0)
                        e.bounds_origin = borg;
                    else
                        e.bounds_origin = position + quat_rotate(e.rotation, borg);
                    e.has_bounds = true;
                }
            }
            // seat occupancy: USQVehicleSeatComponent::SeatedSoldier (0x2F8)
            {
                struct { uint64_t data; int32_t count; int32_t max; } seats{};
                if (m_drv.read_raw(actor + squad::VehicleSeats, &seats, sizeof(seats)) &&
                    seats.data && seats.count >= 1 && seats.count <= 16) {
                    std::vector<uint64_t> comps(seats.count);
                    if (m_drv.read_raw(seats.data, comps.data(), seats.count * 8)) {
                        std::vector<uint64_t> soldiers(seats.count, 0);
                        std::vector<SquadDriver::BatchReq> sb;
                        for (int i = 0; i < seats.count; i++)
                            if (comps[i]) sb.push_back({ comps[i] + 0x2F8, sizeof(uint64_t), &soldiers[i] });
                        if (!sb.empty()) m_drv.batch_read(sb);
                        int occ = 0;
                        for (int i = 0; i < seats.count; i++) if (soldiers[i]) occ++;
                        e.seats_occupied = occ;
                        e.seats_total = seats.count;
                    }
                }
            }
            // weak points: discover once, refresh positions each frame.
            // Positions composed from RelativeLocation (stale-CTW-proof),
            // rejected beyond 4m from the vehicle.
            {
                auto wit = m_vehicle_weakpoints.find(actor);
                if (wit == m_vehicle_weakpoints.end())
                    discover_weakpoints(actor, root);
                wit = m_vehicle_weakpoints.find(actor);
                if (wit != m_vehicle_weakpoints.end()) {
                    for (const auto& wp : wit->second) {
                        if (e.wp_count >= 10) break;
                        FVector wp_pos = weakpoint_world(wp.comp, root, position, e.rotation);
                        if (!wp_pos.is_zero() && wp_pos.distance_m(position) < 400.0) {
                            auto& dst = e.wps[e.wp_count];
                            dst.pos = wp_pos;
                            dst.type = wp.type;
                            strncpy_s(dst.cname, wp.cname.c_str(), sizeof(dst.cname) - 1);
                            e.wp_count++;
                        }
                    }
                }
            }
            m_debug_stats.ent_vehicles++;
        } else {                        // deployable / FOB
            int32_t team = -1;
            uint8_t explosive = 0;
            std::vector<SquadDriver::BatchReq> b = {
                { actor + squad::DeployableHealth, sizeof(float), &e.health },
                { actor + squad::DeployableMaxHealth, sizeof(float), &e.max_health },
                { actor + squad::DeployableTeam, sizeof(int32_t), &team },
                { actor + squad::DeployableExplosiveType, sizeof(uint8_t), &explosive },
            };
            m_drv.batch_read(b);
            e.team = (team >= 1 && team <= 2) ? team : -1;
            e.explosive_type = explosive;
            if (category == 4) {        // FOB: name + ammo
                e.name = m_drv.read_fstring(actor + squad::FobName);
                e.fob_ammo = m_drv.read<float>(actor + squad::FobAmmo);
                m_debug_stats.ent_fobs++;
            } else {
                m_debug_stats.ent_deploys++;
            }
        }

        // display name fallback: class name (stripped)
        if (e.name.empty() && m_names.ok()) {
            uint64_t uc = m_drv.read<uint64_t>(actor + squad::ObjClassPrivate);
            if (uc > 0x10000 && uc < 0x800000000000ULL) {
                std::string nm;
                auto nit = m_uclass_name.find(uc);
                if (nit != m_uclass_name.end()) nm = nit->second;
                else { nm = uclass_name(uc); if (!nm.empty()) m_uclass_name.emplace(uc, nm); }
                if (nm.rfind("BP_", 0) == 0) nm = nm.substr(3);
                if (nm.size() > 2 && nm.compare(nm.size() - 2, 2, "_C") == 0) nm.resize(nm.size() - 2);
                e.name.assign(nm.begin(), nm.end());
            }
        }

        m_entities.push_back(std::move(e));
    }

    // -----------------------------------------------------------------------
    // Process a soldier-class actor found via level enumeration (bot path)
    // -----------------------------------------------------------------------
    void process_soldier_actor(uint64_t actor, uint64_t mesh)
    {
        m_debug_stats.bot_matched++;

        uint64_t root = m_drv.read<uint64_t>(actor + squad::RootComponent);
        if (!root) { m_debug_stats.bot_fail_pos++; return; }

        FVector position{};
        float health = 0.f;
        uint8_t dying_flags = 0;

        std::vector<SquadDriver::BatchReq> batch = {
            { root + squad::ComponentToWorld + 0x20, sizeof(FVector), &position },
            { actor + squad::Health, sizeof(float), &health },
            { actor + squad::DyingFlags, sizeof(uint8_t), &dying_flags },
        };
        m_drv.batch_read(batch);

        if (position.is_zero()) { m_debug_stats.bot_fail_pos++; return; }

        // A bot has no PlayerState; if one exists with a sane team this is a
        // human pawn that just wasn't in PlayerArray (treat as normal player)
        int32_t team_id = -1;
        std::wstring name;
        bool is_bot = true;
        uint64_t ps = m_drv.read<uint64_t>(actor + squad::PlayerState);
        if (ps) {
            int32_t t = m_drv.read<int32_t>(ps + squad::PS_TeamId);
            if (t >= 1 && t <= 32) {
                team_id = t;
                is_bot = false;
                name = m_drv.read_fstring(ps + squad::PlayerNamePrivate);
            }
        }
        if (is_bot) {
            // best-effort class name for display (few actors, cheap)
            std::string cname;
            uint64_t uc = m_drv.read<uint64_t>(actor + 0x10);
            if (uc > 0x10000 && uc < 0x800000000000ULL) {
                auto nit = m_uclass_name.find(uc);
                if (nit != m_uclass_name.end()) cname = nit->second;
                else {
                    cname = uclass_name(uc);
                    if (!cname.empty()) m_uclass_name.emplace(uc, cname);
                }
            }
            if (!cname.empty())
                name.assign(cname.begin(), cname.end());
            else
                name = L"BOT";
        }

        PlayerData pd{};
        pd.actor = actor;
        pd.mesh = mesh;
        pd.player_state = ps;
        pd.position = position;
        pd.team_id = team_id;
        pd.is_bot = is_bot;
        pd.name = std::move(name);

        if (m_local_pawn) {
            uint64_t local_root = m_drv.read<uint64_t>(m_local_pawn + squad::RootComponent);
            if (local_root) {
                FVector local_pos{};
                m_drv.read_raw(local_root + squad::ComponentToWorld + 0x20, &local_pos, sizeof(FVector));
                pd.distance = position.distance_m(local_pos);
            }
        }

        // Only real soldier pawns (141-bone skeleton) are drawn. Anything
        // without a valid bone array (pop-up targets etc.) is rejected here.
        pd.bones_valid = mesh && read_bones(mesh, pd);
        if (!pd.bones_valid) { m_debug_stats.bot_fail_bones++; return; }

        // Current weapon via TWeakObjectPtr -> GObjects
        {
            uint64_t w = resolve_weak(actor + 0x311C);
            if (w) pd.weapon_name = obj_display_name(w);
        }

        bool is_dying = (dying_flags & squad::DyingBit) != 0;
        if (health < 0.f || health > 500.f) { m_debug_stats.bot_fail_hp++; return; }
        if (is_dying && health <= 0.f) { m_debug_stats.bot_fail_hp++; return; }
        pd.health = health;
        pd.dying_flags = dying_flags;
        pd.is_alive = !is_dying;
        if (pd.head_pos.is_zero()) { m_debug_stats.bot_fail_pos++; return; }
        m_debug_stats.bot_pushed_bones++;

        m_players.push_back(std::move(pd));
        m_debug_stats.level_soldiers_found++;
    }

    // -----------------------------------------------------------------------
    // Read skeleton bones for a mesh component
    // -----------------------------------------------------------------------
    bool read_bones(uint64_t mesh, PlayerData& pd)
    {
        // Read ComponentToWorld transform
        FTransform comp_to_world{};
        if (!m_drv.read_raw(mesh + squad::ComponentToWorld, &comp_to_world, sizeof(FTransform)))
            return false;

        // Read bone array pointer and count
        struct { uint64_t data; int32_t count; int32_t max; } bone_arr{};
        if (!m_drv.read_raw(mesh + squad::BoneArray, &bone_arr, sizeof(bone_arr)))
            return false;

        if (!bone_arr.data || bone_arr.count < 100 || bone_arr.count > 300)
            return false;

        // We need bones up to index 140 (IK_Right_Foot)
        // But we only read the ones we actually use for skeleton drawing
        // For efficiency, read a contiguous block of transforms
        int max_bone = (bone_arr.count > 141) ? 141 : bone_arr.count;

        // Read all bone transforms in one big read (max_bone * 0x60 bytes)
        size_t read_size = max_bone * sizeof(FTransform);
        if (read_size > 50000) return false; // sanity

        std::vector<FTransform> bone_transforms(max_bone);
        if (!m_drv.read_raw(bone_arr.data, bone_transforms.data(), read_size))
            return false;

        // Transform each bone to world space
        for (int i = 0; i < max_bone; i++) {
            pd.bones_ws[i] = transform_bone_to_world(comp_to_world, bone_transforms[i]);
        }

        // Cache head position (prefer Head, fallback HeadNub)
        if (max_bone > squad::bones::Head) {
            pd.head_pos = pd.bones_ws[squad::bones::Head];
            if (pd.head_pos.is_zero() && max_bone > squad::bones::HeadNub)
                pd.head_pos = pd.bones_ws[squad::bones::HeadNub];
        }

        return true;
    }
};
