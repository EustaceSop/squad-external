#pragma once
#include "squad_driver.hpp"
#include "squad_structs.hpp"
#include "squad_offsets.hpp"
#include <vector>
#include <string>
#include <cmath>

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
    uint8_t     dying_flags;    // bit0=dying, bit2=bleeding, bit3=wounded
    bool        is_alive;
    bool        is_visible;     // placeholder for future LOS check
    double      distance;       // meters from local player
    std::wstring name;

    // Bone world positions (only the ones we need for skeleton ESP)
    FVector bones_ws[141];      // full bone array world positions
    bool    bones_valid;
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
        m_debug_stats = {};

        if (!read_world()) return false;
        if (!read_local_player()) return false;
        if (!read_camera()) return false;
        read_local_extras();
        read_players();

        return true;
    }

    // Accessors
    const std::vector<PlayerData>& players() const { return m_players; }
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

    // Current local sway/punch values
    FRotator local_weapon_punch_sway() const { return m_weapon_punch_sway; }
    FRotator local_weapon_punch_alignment() const { return m_weapon_punch_alignment; }

    // Local player world position
    FVector local_position() const { return m_local_position; }
    const CoreDebugStats& debug_stats() const { return m_debug_stats; }

    // -----------------------------------------------------------------------
    // World-to-Screen (external W2S from camera rotation + FOV)
    // -----------------------------------------------------------------------
    bool world_to_screen(const FVector& world, FVector2D& screen) const
    {
        // Camera forward/right/up from rotation
        double pitch_rad = m_camera.rotation.pitch * (M_PI / 180.0);
        double yaw_rad   = m_camera.rotation.yaw   * (M_PI / 180.0);
        double roll_rad  = m_camera.rotation.roll   * (M_PI / 180.0);

        double cp = cos(pitch_rad), sp = sin(pitch_rad);
        double cy = cos(yaw_rad),   sy = sin(yaw_rad);
        double cr = cos(roll_rad),  sr = sin(roll_rad);

        FVector forward = { cp * cy, cp * sy, sp };
        FVector right   = { -(sr * sp * cy - cr * sy), -(sr * sp * sy + cr * cy), sr * cp };
        FVector up      = { cr * sp * cy + sr * sy,  cr * sp * sy - sr * cy, -cr * cp };

        FVector delta = world - m_camera.location;

        double dot_forward = delta.dot(forward);
        if (dot_forward <= 0.1) return false;  // behind camera

        double dot_right = delta.dot(right);
        double dot_up    = delta.dot(up);

        double fov_rad = m_camera.fov * (M_PI / 360.0); // half fov
        double tan_fov = tan(fov_rad);

        double half_w = m_screen_w * 0.5;
        double half_h = m_screen_h * 0.5;

        screen.x = half_w + (dot_right / dot_forward / tan_fov) * half_w;
        screen.y = half_h - (dot_up    / dot_forward / tan_fov) * half_h;

        return (screen.x >= -50 && screen.x <= m_screen_w + 50 &&
                screen.y >= -50 && screen.y <= m_screen_h + 50);
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

        // Squad settings live in USQGameInstance -> GameUserSettings
        uint64_t settings = 0;
        if (m_game_instance) {
            settings = m_drv.read<uint64_t>(m_game_instance + squad::GameInstanceGameUserSettings);
        }

        if (settings) {
            float global_sens = 1.0f;
            float steady_sens = 1.0f;
            float soldier_sens = 1.0f;

            std::vector<SquadDriver::BatchReq> sens_batch = {
                { settings + squad::SettingsGlobalSensitivity, sizeof(float), &global_sens },
                { settings + squad::SettingsSteadyAimSensitivity, sizeof(float), &steady_sens },
                { settings + squad::SettingsSoldierSensitivity, sizeof(float), &soldier_sens },
            };
            if (m_drv.batch_read(sens_batch)) {
                if (global_sens > 0.001f && global_sens < 100.f)  m_global_sensitivity = global_sens;
                if (steady_sens > 0.001f && steady_sens < 100.f)  m_steady_aim_sensitivity = steady_sens;
                if (soldier_sens > 0.001f && soldier_sens < 100.f) m_soldier_sensitivity = soldier_sens;
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
    }

    // -----------------------------------------------------------------------
    // Read all players from AGameStateBase::PlayerArray
    // -----------------------------------------------------------------------
    void read_players()
    {
        uint64_t game_state = m_drv.read<uint64_t>(m_world + squad::GameState);
        if (!game_state) return;
        m_debug_stats.game_state_ptr = game_state;

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

        // Read bones
        pd.bones_valid = read_bones(mesh, pd);
        if (!pd.bones_valid || pd.head_pos.is_zero()) return;
        m_debug_stats.valid_bones++;

        m_players.push_back(std::move(pd));
        m_debug_stats.pushed_players++;
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
