#pragma once
#include "squad_driver.hpp"
#include "squad_structs.hpp"
#include <vector>
#include <string>
#include <fstream>
#include <cmath>
#include <cstdio>

// ============================================================================
// Squad Bone Debug Tool (v10.5.1 / UE 5.7.4)
// Runtime discovery + dump of the full bone chain for skeleton ESP.
//
// Non-reflected members are NOT in the Dumper-7 SDK, so this tool locates
// them at runtime with signature heuristics instead of hardcoded offsets:
//   - USkinnedMeshComponent::BoneSpaceTransforms (or reflected Cached*)
//   - USkeletalMesh::RefSkeleton bone name array (FMeshBoneInfo)
//   - FNamePool base (probed via FName::AppendString RIP-relative refs)
//   - USceneComponent::ComponentToWorld (validated against camera position)
//
// Output: squad_bones_debug.txt (index / name / parent / world position)
// ============================================================================

namespace bones_debug {

// --- v10.5.1 reflected offsets (Dumper-7 SDK, verified) ---
namespace off {
    constexpr uint64_t GWorld                    = 0x0D1C9EB8;
    constexpr uint64_t AppendString              = 0x013ED940;

    constexpr uint64_t World_GameState           = 0x01B0;
    constexpr uint64_t World_OwningGameInstance  = 0x0230;
    constexpr uint64_t GameInstance_LocalPlayers = 0x0038;
    constexpr uint64_t Player_PlayerController   = 0x0030;
    constexpr uint64_t Controller_AckPawn        = 0x0378;
    constexpr uint64_t Controller_CameraManager  = 0x0388;
    constexpr uint64_t CameraManager_Cache       = 0x15B0;  // CameraCachePrivate
    constexpr uint64_t GameState_PlayerArray     = 0x02D0;
    constexpr uint64_t PS_TeamId                 = 0x0508;
    constexpr uint64_t PS_Soldier                = 0x07F0;
    constexpr uint64_t Character_Mesh            = 0x0338;
    constexpr uint64_t Actor_RootComponent       = 0x01C0;

    constexpr uint64_t SkinnedMesh_SkeletalMesh  = 0x05A8;  // USkinnedMeshComponent::SkeletalMesh
    constexpr uint64_t SkeletalMesh_Skeleton     = 0x0118;  // USkeletalMesh::Skeleton
    constexpr uint64_t Skeleton_BoneTree         = 0x0038;  // USkeleton::BoneTree
    constexpr uint64_t CachedBoneSpaceTransforms = 0x09F0;  // USkeletalMeshComponent (reflected)
    constexpr uint64_t CachedCompSpaceTransforms = 0x0A00;  // USkeletalMeshComponent (reflected)
}

struct TArrayHdr { uint64_t data; int32_t count; int32_t capacity; };

struct DiscoverResult {
    int      bone_count       = 0;
    uint64_t bone_tree_data   = 0;   // node array in USkeleton::BoneTree
    bool     bone_tree_ok     = false;
    int      tree_stride      = 8;   // discovered node stride
    int      tree_parent_off  = 0;   // offset of ParentIndex inside a node

    int      names_stride     = 0x10; // discovered FMeshBoneInfo stride (0xC or 0x10)

    uint64_t transforms_data  = 0;   // FTransform[] component-space
    uint64_t transforms_off   = 0;   // offset in mesh component (0 = none)
    bool     transforms_ok    = false;
    const char* transforms_src = "none";

    uint64_t names_data       = 0;   // FMeshBoneInfo[] {FName;int32 parent} stride 0x10
    uint64_t names_off        = 0;   // offset in USkeletalMesh (0 = none)
    bool     names_ok         = false;

    uint64_t name_pool        = 0;   // FNamePool base
    int      name_pool_shard_off = 0x10;
    bool     name_pool_ok     = false;

    uint64_t ctw_off          = 0;   // USceneComponent::ComponentToWorld
    bool     ctw_ok           = false;

    uint64_t mesh             = 0;
    uint64_t skel_mesh        = 0;
    uint64_t skeleton         = 0;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
inline bool quat_sane(const FQuat& q) {
    double n = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    return n > 0.9 && n < 1.1;
}
inline bool scale_sane(const FVector& s) {
    auto ok = [](double v){ return v > 0.001 && v < 100.0; };
    return ok(s.x) && ok(s.y) && ok(s.z);
}
inline bool user_ptr(uint64_t p) {
    return p > 0x10000 && p < 0x800000000000ULL;
}

// forward declarations (pool resolution used by name-array validation)
inline bool resolve_fname(SquadDriver& drv, const DiscoverResult& r, uint32_t id, std::string& out);

// ---------------------------------------------------------------------------
// Step 1: BoneTree -> bone count + hex dump + multi-layout parent probing
// ---------------------------------------------------------------------------
inline bool probe_bone_tree(SquadDriver& drv, uint64_t skeleton, DiscoverResult& r, std::string& log)
{
    char buf[512];
    TArrayHdr hdr{};
    if (!drv.read_raw(skeleton + off::Skeleton_BoneTree, &hdr, sizeof(hdr))) return false;
    snprintf(buf, sizeof(buf), "[BoneTree] data=0x%llX count=%d cap=%d\n",
             (unsigned long long)hdr.data, hdr.count, hdr.capacity);
    log += buf;
    if (!hdr.data || hdr.count < 20 || hdr.count > 512) return false;

    // hex dump first 64 bytes so layout is directly readable from the log
    uint8_t first[64]{};
    drv.read_raw(hdr.data, first, sizeof(first));
    log += "[BoneTree] raw[0..63]:";
    for (int i = 0; i < 64; i++) { snprintf(buf, sizeof(buf), "%s%02X", (i % 8 == 0) ? "\n  " : " ", first[i]); log += buf; }
    log += "\n";

    // try candidate node layouts: {stride, parent_offset}
    struct Layout { int stride; int poff; };
    const Layout layouts[] = {
        { 0x8, 0x0 }, { 0x8, 0x4 },
        { 0xC, 0x0 }, { 0xC, 0x4 }, { 0xC, 0x8 },
        { 0x10, 0x8 }, { 0x10, 0xC },
    };
    std::vector<uint8_t> nodes(hdr.count * 0x10);  // big enough for max stride
    if (!drv.read_raw(hdr.data, nodes.data(), (size_t)hdr.count * 0x10)) return false;

    for (const auto& L : layouts) {
        if ((size_t)hdr.count * L.stride > nodes.size()) continue;
        auto parent_of = [&](int i) { return *(const int32_t*)(nodes.data() + (size_t)i * L.stride + L.poff); };
        if (parent_of(0) != -1) continue;
        int bad = 0;
        for (int i = 1; i < hdr.count; i++) {
            int32_t p = parent_of(i);
            if (p < -1 || p >= i) { if (++bad > 3) break; }
        }
        snprintf(buf, sizeof(buf), "[BoneTree] layout stride=0x%X poff=0x%X: %s (bad=%d) p[1]=%d p[2]=%d p[3]=%d\n",
                 L.stride, L.poff, bad <= 3 ? "OK" : "FAIL", bad,
                 parent_of(1), parent_of(2), parent_of(3));
        log += buf;
        if (bad <= 3) {
            r.tree_stride = L.stride;
            r.tree_parent_off = L.poff;
            r.bone_tree_ok = true;
        }
    }
    if (!r.bone_tree_ok) {
        // Cooked builds can strip FBoneNode down to a single retarget-mode byte
        // (Dumper-7: sizeof(FBoneNode)==0x1). Hierarchy then only exists in
        // FMeshBoneInfo - not an error, names step handles parents.
        bool byte_modes = true;
        for (int i = 0; i < hdr.count && i < 64; i++)
            if (nodes[i] > 4) { byte_modes = false; break; }
        if (byte_modes)
            log += "[BoneTree] = 1-byte retarget modes only (cooked). Hierarchy will come from FMeshBoneInfo.\n";
        else
            log += "[BoneTree] no layout passed - inspect raw hex above\n";
    }

    r.bone_count = hdr.count;
    r.bone_tree_data = hdr.data;
    return true;
}

// ---------------------------------------------------------------------------
// Step 2: locate component-space FTransform array
// ---------------------------------------------------------------------------
inline bool validate_transforms(SquadDriver& drv, uint64_t data, int count)
{
    if (!user_ptr(data)) return false;
    FTransform t[4]{};
    int n = count < 4 ? count : 4;
    if (!drv.read_raw(data, t, n * sizeof(FTransform))) return false;
    int good = 0;
    for (int i = 0; i < n; i++)
        if (quat_sane(t[i].rotation) && scale_sane(t[i].scale3d)) good++;
    return good >= n - 1;
}

inline void find_transforms(SquadDriver& drv, uint64_t mesh, int bone_count, DiscoverResult& r, std::string& log)
{
    char buf[256];

    // 2a. reflected cached arrays first
    for (auto cand : { off::CachedCompSpaceTransforms, off::CachedBoneSpaceTransforms }) {
        TArrayHdr hdr{};
        if (drv.read_raw(mesh + cand, &hdr, sizeof(hdr)) && hdr.count == bone_count
            && validate_transforms(drv, hdr.data, bone_count)) {
            r.transforms_data = hdr.data;
            r.transforms_off  = cand;
            r.transforms_ok   = true;
            r.transforms_src  = (cand == off::CachedCompSpaceTransforms)
                                ? "CachedComponentSpaceTransforms (reflected 0xA00)"
                                : "CachedBoneSpaceTransforms (reflected 0x9F0)";
            snprintf(buf, sizeof(buf), "[Transforms] reflected array hit @ mesh+0x%llX count=%d\n",
                     (unsigned long long)cand, hdr.count);
            log += buf;
            return;
        }
        snprintf(buf, sizeof(buf), "[Transforms] reflected @ mesh+0x%llX count=%d (no match)\n",
                 (unsigned long long)cand, hdr.count);
        log += buf;
    }

    // 2b. scan USkinnedMeshComponent for TArray<FTransform> with count == bone_count
    constexpr uint64_t scan_start = 0x500, scan_len = 0x1000;
    std::vector<uint8_t> mem(scan_len);
    if (!drv.read_raw(mesh + scan_start, mem.data(), scan_len)) { log += "[Transforms] component read failed\n"; return; }

    for (uint64_t o = 0; o + sizeof(TArrayHdr) <= scan_len; o += 8) {
        auto* hdr = (const TArrayHdr*)(mem.data() + o);
        if (hdr->count != bone_count || hdr->capacity < bone_count || hdr->capacity > bone_count + 64) continue;
        if (!user_ptr(hdr->data)) continue;
        if (validate_transforms(drv, hdr->data, bone_count)) {
            r.transforms_data = hdr->data;
            r.transforms_off  = scan_start + o;
            r.transforms_ok   = true;
            r.transforms_src  = "discovered TArray<FTransform>";
            snprintf(buf, sizeof(buf), "[Transforms] discovered @ mesh+0x%llX data=0x%llX count=%d\n",
                     (unsigned long long)r.transforms_off, (unsigned long long)hdr->data, hdr->count);
            log += buf;
            return;
        }
    }
    log += "[Transforms] FAILED - no valid FTransform array found\n";
}

// ---------------------------------------------------------------------------
// Step 3: locate FMeshBoneInfo name array (validated by resolving via pool)
// ---------------------------------------------------------------------------
inline bool names_candidate_ok(SquadDriver& drv, const DiscoverResult& r, uint64_t data, int bone_count, int stride, std::string& log)
{
    char buf[256];
    // FMeshBoneInfo (shipping): { FName Name(0x0, align 4); int32 ParentIndex } -> stride 0xC or 0x10
    std::vector<uint8_t> entries((size_t)bone_count * stride);
    if (!drv.read_raw(data, entries.data(), entries.size())) return false;

    // primary validation: resolve names via the validated pool -> must be printable
    int resolved = 0;
    std::string sample;
    for (int i = 0; i < bone_count && i < 12; i++) {
        uint32_t id = *(const uint32_t*)(entries.data() + (size_t)i * stride);
        std::string s;
        if (!resolve_fname(drv, r, id, s)) break;
        bool printable = !s.empty() && s.size() <= 64;
        for (char c : s)
            if (!(isalnum((unsigned char)c) || c == '_' || c == ' ' || c == '.')) { printable = false; break; }
        if (!printable) break;
        resolved++;
        sample += " [" + std::to_string(i) + "]=" + s;
    }
    if (resolved < 8) return false;

    // secondary: parent chain (informational only - log, don't gate)
    int32_t p0 = *(const int32_t*)(entries.data() + 0x8);
    int bad = (p0 == -1) ? 0 : 1;
    for (int i = 1; i < bone_count; i++) {
        int32_t par = *(const int32_t*)(entries.data() + (size_t)i * stride + 0x8);
        if (par < -1 || par >= i) { if (++bad > 5) break; }
    }
    snprintf(buf, sizeof(buf), "[Names] candidate stride=0x%X: resolved=%d/12 parent_bad=%d%s\n",
             stride, resolved, bad, sample.c_str());
    log += buf;
    return true;
}

inline void find_names(SquadDriver& drv, const DiscoverResult& rin, int bone_count, DiscoverResult& r, std::string& log)
{
    char buf[256];
    if (!r.name_pool_ok) { log += "[Names] skipped - no pool\n"; return; }

    const int strides[] = { 0x0C, 0x10, 0x14, 0x18 };

    // 3a. maybe the BoneTree array itself IS the name array (names inline)
    for (int st : strides) {
        if (rin.bone_tree_data && names_candidate_ok(drv, r, rin.bone_tree_data, bone_count, st, log)) {
            r.names_data = rin.bone_tree_data;
            r.names_off  = 0;
            r.names_stride = st;
            r.names_ok   = true;
            log += "[Names] BoneTree array itself contains FMeshBoneInfo (names inline)\n";
            return;
        }
    }

    // 3b. scan USkeletalMesh (wide range) then USkeleton
    struct Region { uint64_t obj; uint64_t start; uint64_t len; const char* tag; };
    const Region regions[] = {
        { rin.skel_mesh, 0x100, 0x1800, "skelMesh" },
        { rin.skeleton,  0x38,  0x400,  "skeleton" },
    };
    for (const auto& reg : regions) {
        if (!reg.obj) continue;
        std::vector<uint8_t> mem(reg.len);
        if (!drv.read_raw(reg.obj + reg.start, mem.data(), reg.len)) continue;
        for (uint64_t o = 0; o + sizeof(TArrayHdr) <= reg.len; o += 8) {
            auto* hdr = (const TArrayHdr*)(mem.data() + o);
            if (hdr->count != bone_count || hdr->capacity < bone_count || hdr->capacity > bone_count + 64) continue;
            if (!user_ptr(hdr->data)) continue;
            for (int st : strides) {
                if (!names_candidate_ok(drv, r, hdr->data, bone_count, st, log)) continue;
                r.names_data = hdr->data;
                r.names_off  = reg.start + o;
                r.names_stride = st;
                r.names_ok   = true;
                snprintf(buf, sizeof(buf), "[Names] discovered FMeshBoneInfo @ %s+0x%llX data=0x%llX count=%d stride=0x%X\n",
                         reg.tag, (unsigned long long)r.names_off, (unsigned long long)hdr->data, hdr->count, st);
                log += buf;
                return;
            }
        }
    }
    log += "[Names] FAILED - no FMeshBoneInfo array found\n";
}

// ---------------------------------------------------------------------------
// Step 4: locate FNamePool via FName::AppendString code references
// ---------------------------------------------------------------------------
inline bool pool_probe_none(SquadDriver& drv, uint64_t pool, int shard_off)
{
    // Entry 0 in shard 0 must be FName "None" (4 ansi chars)
    uint64_t shard0 = drv.read<uint64_t>(pool + shard_off);
    if (!user_ptr(shard0)) return false;
    uint16_t hdr = drv.read<uint16_t>(shard0);
    int len  = hdr >> 6;
    bool wide = (hdr & 1) != 0;
    if (wide || len != 4) return false;
    char s[5]{};
    if (!drv.read_raw(shard0 + 2, s, 4)) return false;
    return memcmp(s, "None", 4) == 0;
}

inline bool resolve_fname(SquadDriver& drv, const DiscoverResult& r, uint32_t id, std::string& out)
{
    uint64_t shard = drv.read<uint64_t>(r.name_pool + r.name_pool_shard_off + (uint64_t)(id >> 16) * 8);
    if (!user_ptr(shard)) return false;
    uint64_t entry = shard + (uint64_t)(id & 0xFFFF) * 2;
    uint16_t hdr = drv.read<uint16_t>(entry);
    int len   = hdr >> 6;
    bool wide = (hdr & 1) != 0;
    if (len <= 0 || len > 128) return false;
    if (wide) {
        std::wstring w(len, L'\0');
        if (!drv.read_raw(entry + 2, w.data(), len * 2)) return false;
        out.assign(w.begin(), w.end());  // ASCII-safe only; bones are ASCII
    } else {
        out.resize(len);
        if (!drv.read_raw(entry + 2, out.data(), len)) return false;
    }
    return true;
}

inline void find_name_pool(SquadDriver& drv, uint64_t game_base, const DiscoverResult& r, DiscoverResult& out, std::string& log)
{
    char buf[256];
    constexpr size_t code_len = 0x400;
    std::vector<uint8_t> code(code_len);
    uint64_t fn = game_base + off::AppendString;
    if (!drv.read_raw(fn, code.data(), code_len)) { log += "[NamePool] AppendString read failed\n"; return; }

    // scan for RIP-relative lea/mov (REX 48/4C, op 8B/8D, modrm mod=0 rm=101)
    std::vector<uint64_t> cands;
    for (size_t i = 0; i + 7 <= code_len; i++) {
        if (code[i] != 0x48 && code[i] != 0x4C) continue;
        if (code[i+1] != 0x8B && code[i+1] != 0x8D) continue;
        if ((code[i+2] & 0xC7) != 0x05) continue;
        int32_t disp = *(const int32_t*)(code.data() + i + 3);
        uint64_t target = fn + i + 7 + disp;
        if (target > 0x10000 && target < 0x800000000000ULL) cands.push_back(target);
    }
    snprintf(buf, sizeof(buf), "[NamePool] %zu RIP-relative candidates from AppendString\n", cands.size());
    log += buf;

    for (uint64_t c : cands) {
        for (int shard_off : { 0x10, 0x18, 0x08, 0x00 }) {
            if (!pool_probe_none(drv, c, shard_off)) continue;
            out.name_pool = c;
            out.name_pool_shard_off = shard_off;
            out.name_pool_ok = true;
            snprintf(buf, sizeof(buf), "[NamePool] VALIDATED pool=0x%llX shard_off=0x%X ('None' entry OK)\n",
                     (unsigned long long)c, shard_off);
            log += buf;
            return;
        }
    }
    log += "[NamePool] FAILED - no candidate passed the 'None' probe\n";
}

// ---------------------------------------------------------------------------
// Step 5: locate ComponentToWorld (validated against camera position)
// ---------------------------------------------------------------------------
inline void find_ctw(SquadDriver& drv, uint64_t root_comp, const FVector& cam_loc, DiscoverResult& r, std::string& log)
{
    char buf[256];
    constexpr uint64_t scan_start = 0x180, scan_len = 0x100;
    std::vector<uint8_t> mem(scan_len);
    if (!drv.read_raw(root_comp + scan_start, mem.data(), scan_len)) { log += "[CTW] component read failed\n"; return; }

    for (uint64_t o = 0; o + sizeof(FTransform) <= scan_len; o += 0x10) {
        auto* t = (const FTransform*)(mem.data() + o);
        if (!quat_sane(t->rotation) || !scale_sane(t->scale3d)) continue;
        double d = t->translation.distance_m(cam_loc);   // local root sits ~2m under camera
        snprintf(buf, sizeof(buf), "[CTW] candidate @ root+0x%llX  dist_to_cam=%.2fm  t=(%.1f, %.1f, %.1f)\n",
                 (unsigned long long)(scan_start + o), d, t->translation.x, t->translation.y, t->translation.z);
        log += buf;
        if (d < 5.0 && !r.ctw_ok) {
            r.ctw_off = scan_start + o;
            r.ctw_ok  = true;
        }
    }
    if (!r.ctw_ok) log += "[CTW] FAILED - no candidate within 5m of camera\n";
    else { snprintf(buf, sizeof(buf), "[CTW] selected root+0x%llX\n", (unsigned long long)r.ctw_off); log += buf; }
}

// ---------------------------------------------------------------------------
// Main entry: run full discovery + dump
// ---------------------------------------------------------------------------
inline bool run(SquadDriver& drv, uint64_t game_base)
{
    char buf[512];
    std::string log;
    log += "==== Squad Bone Debug Dump (v10.5.1 / UE 5.7.4) ====\n\n";

    auto flush = [&]() {
        std::ofstream f("squad_bones_debug.txt", std::ios::trunc);
        if (f.is_open()) { f << log; f.flush(); }
        printf("%s", log.c_str());
    };

    // --- world chain ---
    uint64_t world = drv.read<uint64_t>(game_base + off::GWorld);
    snprintf(buf, sizeof(buf), "[Chain] UWorld=0x%llX\n", (unsigned long long)world); log += buf;
    if (!world) { log += "[Chain] FAILED at UWorld\n"; flush(); return false; }

    uint64_t game_state, game_inst, local_plr, ctrl, cam_mgr;
    game_state = drv.read<uint64_t>(world + off::World_GameState);
    game_inst  = drv.read<uint64_t>(world + off::World_OwningGameInstance);
    local_plr  = game_inst ? drv.read<uint64_t>(game_inst + off::GameInstance_LocalPlayers) : 0;
    if (local_plr) local_plr = drv.read<uint64_t>(local_plr);          // LocalPlayers[0]
    ctrl       = local_plr ? drv.read<uint64_t>(local_plr + off::Player_PlayerController) : 0;
    cam_mgr    = ctrl ? drv.read<uint64_t>(ctrl + off::Controller_CameraManager) : 0;
    snprintf(buf, sizeof(buf), "[Chain] GameState=0x%llX Ctrl=0x%llX CamMgr=0x%llX\n",
             (unsigned long long)game_state, (unsigned long long)ctrl, (unsigned long long)cam_mgr);
    log += buf;

    FVector cam_loc{};
    {
        struct { FVector loc; FRotator rot; float fov; } pov{};
        if (cam_mgr && drv.read_raw(cam_mgr + off::CameraManager_Cache + 0x10, &pov, sizeof(pov)))
            cam_loc = pov.loc;
    }
    snprintf(buf, sizeof(buf), "[Chain] camera loc = (%.1f, %.1f, %.1f)\n", cam_loc.x, cam_loc.y, cam_loc.z);
    log += buf;

    // --- pick a target: prefer local pawn, fallback first PlayerArray soldier ---
    uint64_t soldier = ctrl ? drv.read<uint64_t>(ctrl + off::Controller_AckPawn) : 0;
    if (!soldier && game_state) {
        TArrayHdr pa{};
        drv.read_raw(game_state + off::GameState_PlayerArray, &pa, sizeof(pa));
        snprintf(buf, sizeof(buf), "[Chain] PlayerArray count=%d\n", pa.count); log += buf;
        if (pa.data && pa.count > 0 && pa.count < 512) {
            std::vector<uint64_t> ps_arr(pa.count);
            drv.read_raw(pa.data, ps_arr.data(), pa.count * 8);
            for (uint64_t ps : ps_arr) {
                if (!user_ptr(ps)) continue;
                int32_t team = drv.read<int32_t>(ps + off::PS_TeamId);
                uint64_t s   = drv.read<uint64_t>(ps + off::PS_Soldier);
                if (team >= 1 && team <= 32 && user_ptr(s)) { soldier = s; break; }
            }
        }
    }
    snprintf(buf, sizeof(buf), "[Chain] soldier=0x%llX\n", (unsigned long long)soldier); log += buf;
    if (!soldier) { log += "[Chain] FAILED - no soldier\n"; flush(); return false; }

    DiscoverResult r{};
    r.mesh      = drv.read<uint64_t>(soldier + off::Character_Mesh);
    r.skel_mesh = r.mesh ? drv.read<uint64_t>(r.mesh + off::SkinnedMesh_SkeletalMesh) : 0;
    r.skeleton  = r.skel_mesh ? drv.read<uint64_t>(r.skel_mesh + off::SkeletalMesh_Skeleton) : 0;
    snprintf(buf, sizeof(buf), "[Chain] mesh=0x%llX skelMesh=0x%llX skeleton=0x%llX\n\n",
             (unsigned long long)r.mesh, (unsigned long long)r.skel_mesh, (unsigned long long)r.skeleton);
    log += buf;
    if (!r.mesh || !r.skel_mesh || !r.skeleton) { log += "[Chain] FAILED at mesh/skeleton\n"; flush(); return false; }

    // --- discovery steps ---
    if (!probe_bone_tree(drv, r.skeleton, r, log)) { log += "[BoneTree] FAILED\n"; flush(); return false; }
    log += "\n";
    find_transforms(drv, r.mesh, r.bone_count, r, log);
    find_name_pool(drv, game_base, r, r, log);   // pool first: name validation depends on it
    find_names(drv, r, r.bone_count, r, log);
    {
        uint64_t root = drv.read<uint64_t>(soldier + off::Actor_RootComponent);
        if (root && !cam_loc.is_zero()) find_ctw(drv, root, cam_loc, r, log);
    }
    log += "\n";

    // --- summary ---
    snprintf(buf, sizeof(buf),
        "==== SUMMARY ====\n"
        "bone_count      = %d\n"
        "BoneTree        = %s\n"
        "Transforms      = %s (off=0x%llX src=%s)\n"
        "Names           = %s (skelMesh+0x%llX)\n"
        "NamePool        = %s (0x%llX shard+0x%X)\n"
        "ComponentToWorld= %s (root+0x%llX)\n\n",
        r.bone_count,
        r.bone_tree_ok ? "OK" : "FAIL",
        r.transforms_ok ? "OK" : "FAIL", (unsigned long long)r.transforms_off, r.transforms_src,
        r.names_ok ? "OK" : "FAIL", (unsigned long long)r.names_off,
        r.name_pool_ok ? "OK" : "FAIL", (unsigned long long)r.name_pool, r.name_pool_shard_off,
        r.ctw_ok ? "OK" : "FAIL", (unsigned long long)r.ctw_off);
    log += buf;

    // --- full bone dump: index / name / parent / world pos ---
    if (r.transforms_ok && r.ctw_ok) {
        FTransform ctw{};
        bool have_ctw = drv.read_raw(r.mesh + r.ctw_off, &ctw, sizeof(ctw));

        std::vector<uint8_t> names_raw(r.names_ok ? (size_t)r.bone_count * r.names_stride : 0);
        if (r.names_ok) drv.read_raw(r.names_data, names_raw.data(), names_raw.size());
        std::vector<uint8_t> tree_raw(r.bone_tree_ok ? (size_t)r.bone_count * r.tree_stride : 0);
        if (r.bone_tree_ok) drv.read_raw(r.bone_tree_data, tree_raw.data(), tree_raw.size());

        std::vector<FTransform> bones(r.bone_count);
        bool have_bones = drv.read_raw(r.transforms_data, bones.data(), r.bone_count * sizeof(FTransform));

        // sanity: bones[0] in component space should be near identity
        if (have_bones) {
            snprintf(buf, sizeof(buf), "[Sanity] bones[0] t=(%.3f, %.3f, %.3f) scale=(%.2f, %.2f, %.2f)\n\n",
                     bones[0].translation.x, bones[0].translation.y, bones[0].translation.z,
                     bones[0].scale3d.x, bones[0].scale3d.y, bones[0].scale3d.z);
            log += buf;
        }

        log += "idx  parent  name                                 world_x      world_y      world_z\n";
        log += "-------------------------------------------------------------------------------------\n";
        for (int i = 0; i < r.bone_count; i++) {
            int32_t par = -999;
            if (r.names_ok)
                par = *(const int32_t*)(names_raw.data() + (size_t)i * r.names_stride + 0x8);
            else if (r.bone_tree_ok)
                par = *(const int32_t*)(tree_raw.data() + (size_t)i * r.tree_stride + r.tree_parent_off);

            std::string name = "?";
            if (r.names_ok && r.name_pool_ok) {
                uint32_t id = *(const uint32_t*)(names_raw.data() + (size_t)i * r.names_stride);
                resolve_fname(drv, r, id, name);
            }

            if (have_bones && have_ctw) {
                FVector ws = transform_bone_to_world(ctw, bones[i]);
                snprintf(buf, sizeof(buf), "%3d  %6d  %-36s %11.1f %11.1f %11.1f\n",
                         i, par, name.c_str(), ws.x, ws.y, ws.z);
            } else {
                snprintf(buf, sizeof(buf), "%3d  %6d  %-36s (no transform)\n", i, par, name.c_str());
            }
            log += buf;
        }
    } else {
        log += "[Dump] skipped bone list - transforms or CTW missing (see FAIL lines above)\n";
    }

    flush();
    return true;
}

} // namespace bones_debug
