#pragma once
#include <cstdint>

// ============================================================
// Squad v10.5.1 - UE 5.7.4 Offsets
// SDK: Dumper-7 5.7.4-627303+__Squad_v10.5.1-SquadGame
// All vectors/rotators/quats are double (UE5 LargeWorldCoordinates)
//
// Verification tags:
//   [RT]  = runtime-verified on live game (bone debug tool, F10 dump)
//   [SDK] = from Dumper-7 SDK reflection data (high confidence)
//   [??]  = unverified, do not trust
// ============================================================

namespace squad
{
    // ==================== Global Pointers ====================
    constexpr uint64_t GWorld   = 0x0D1C9EB8;  // [RT] world chain fully works
    constexpr uint64_t GObjects = 0x0D0331B0;  // [SDK] Basic.hpp (unused)
    // GNames unused in this build - FName::AppendString at base+AppendString.
    // FNamePool is discovered at runtime from AppendString code refs (squad_bones).
    constexpr uint64_t AppendString = 0x013ED940;  // [SDK]

    // ==================== UWorld ====================
    constexpr uint64_t OwningGameInstance = 0x0230;  // [RT]
    constexpr uint64_t PersistentLevel    = 0x0030;  // [SDK]
    constexpr uint64_t GameState          = 0x01B0;  // [RT]

    // ==================== AGameStateBase ====================
    constexpr uint64_t PlayerArray        = 0x02D0;  // [SDK]

    // ==================== ULevel ====================
    // ULevel -> TArray<AActor*> Actors (bot enumeration: bots have no PlayerState)
    constexpr uint64_t LevelActors        = 0x00A0;  // [SDK]
    // UWorld -> TArray<ULevel*> Levels (includes streamed sub-levels - tutorial
    // bots spawn in sub-levels, NOT in PersistentLevel)
    constexpr uint64_t WorldLevels        = 0x01C8;  // [SDK]

    // ==================== UGameInstance ====================
    constexpr uint64_t ULocalPlayers      = 0x0038;  // [RT]

    // ==================== ULocalPlayer (UPlayer) ====================
    constexpr uint64_t PlayerController   = 0x0030;  // [RT]
    constexpr uint64_t ViewportClient     = 0x0078;  // [SDK] ULocalPlayer -> UGameViewportClient

    // ==================== UObject base (universal, engine-stable) ====================
    constexpr uint64_t ObjClassPrivate    = 0x0010;  // UObject -> UClass*
    constexpr uint64_t ObjOuterPrivate    = 0x0020;  // UObject -> Outer (ViewportClient outer = UEngine)
    constexpr uint64_t FieldNamePrivate   = 0x0018;  // UField -> FName

    // ==================== Local weapon ====================
    constexpr uint64_t InventoryComponent = 0x2AB0;  // [SDK] ASQSoldier -> USQPawnInventoryComponent
    constexpr uint64_t InvCurrentWeapon   = 0x01C0;  // [SDK] -> ASQEquipableItem* (direct ptr)
    constexpr uint64_t ItemStaticInfo     = 0x02B8;  // [SDK] ASQEquipableItem -> USQItemStaticInfo
    constexpr uint64_t WeaponMagazines    = 0x0858;  // [SDK] ASQWeapon -> TArray<FSQMagazineData(0x8)>
    // FSQMagazineData: +0x0 int32 DefaultRoundsPerMag, +0x4 int32 RemainingRounds
    // Magazines[0] = current mag, [1..n] = reserve

    // ==================== Vehicles ====================
    constexpr uint64_t PawnTeam           = 0x0346;  // [SDK] ASQPawn::Team (uint8: 0/1/2)
    constexpr uint64_t VehicleHealth      = 0x09B8;  // [SDK] ASQVehicle
    constexpr uint64_t VehicleMaxHealth   = 0x09BC;  // [SDK]
    constexpr uint64_t VehicleSeats       = 0x08C8;  // [SDK] TArray<USQVehicleSeatComponent*>

    // ==================== Deployables / FOB / Rally ====================
    constexpr uint64_t DeployableTeam     = 0x02E0;  // [SDK] ASQDeployable::Team (int32)
    constexpr uint64_t DeployableIsFob    = 0x02E4;  // [SDK] bool
    constexpr uint64_t DeployablePlaced   = 0x02E5;  // [SDK] bool
    constexpr uint64_t DeployableExplosiveType = 0x0350; // [SDK] uint8
    constexpr uint64_t DeployableMaxHealth = 0x043C; // [SDK]
    constexpr uint64_t DeployableHealth   = 0x0444;  // [SDK]
    constexpr uint64_t FobName            = 0x0568;  // [SDK] ASQForwardBase::Name FString
    constexpr uint64_t FobAmmo            = 0x0624;  // [SDK] float
    constexpr uint64_t RallyMesh          = 0x04C8;  // [SDK] ASQGameRallyPoint::Mesh
    constexpr uint64_t RallySquadState    = 0x0500;  // [SDK] ASQSquadRallyPoint::SquadState
    constexpr uint64_t SquadStateTeamId   = 0x034C;  // [SDK] ASQSquadState::TeamId

    // ==================== APlayerController ====================
    constexpr uint64_t AcknowledgedPawn    = 0x0378;  // [RT]
    constexpr uint64_t PlayerCameraManager = 0x0388;  // [RT]

    // ==================== AActor ====================
    constexpr uint64_t RootComponent      = 0x01C0;  // [RT]
    // APawn -> APlayerState*
    constexpr uint64_t PlayerState        = 0x02D8;  // [SDK]

    // ==================== USceneComponent ====================
    constexpr uint64_t RelativeLocation   = 0x0148;  // [SDK]
    // Non-reflected: discovered at runtime, validated vs camera pos (0.34m)
    constexpr uint64_t ComponentToWorld   = 0x0200;  // [RT]

    // ==================== USkeletalMeshComponent ====================
    constexpr uint64_t CharacterMesh      = 0x0338;  // [RT]
    // USkinnedMeshComponent::SkeletalMesh (reflected)
    constexpr uint64_t SkinnedMeshAsset   = 0x05A8;  // [RT]
    // BoneSpaceTransforms TArray<FTransform> (non-reflected, discovered).
    // Component-space transforms, count == 141 for soldier skeleton.
    constexpr uint64_t BoneArray          = 0x0620;  // [RT]
    // USkeletalMesh::Skeleton
    constexpr uint64_t SkeletalMeshSkeleton = 0x0118;  // [RT]
    // USkeleton::BoneTree - cooked build: 1 byte per bone (retarget mode only)
    constexpr uint64_t SkeletonBoneTree   = 0x0038;  // [RT]
    // FMeshBoneInfo[] (FName + int32 ParentIndex, stride 0xC) inside USkeletalMesh
    constexpr uint64_t SkeletalMeshBoneInfo = 0x02E0;  // [RT]
    constexpr uint64_t BoneInfoStride     = 0x0C;    // [RT]

    // ==================== ASQPlayerState ====================
    constexpr uint64_t PS_TeamId          = 0x0508;  // [SDK]
    constexpr uint64_t PS_Soldier         = 0x07F0;  // [SDK]

    // ==================== APlayerState (Engine base) ====================
    constexpr uint64_t PlayerNamePrivate  = 0x0358;  // [SDK]

    // ==================== ASQSoldier ====================
    constexpr uint64_t Mesh1P             = 0x0780;  // [SDK]
    constexpr uint64_t FPCamera           = 0x07D0;  // [SDK]
    constexpr uint64_t SoldierMovement    = 0x07F0;  // [SDK]

    // Weapon sway/punch
    constexpr uint64_t WeaponPunchSway      = 0x1ED0;  // [SDK]
    constexpr uint64_t WeaponPunchAlignment = 0x1EE8;  // [SDK]
    constexpr uint64_t WeaponPunchLocation  = 0x1F00;  // [SDK]

    // Free aim
    constexpr uint64_t FreeAimVerticalInput   = 0x2104;  // [SDK]
    constexpr uint64_t FreeAimHorizontalInput = 0x2108;  // [SDK]

    // Health/dying - 0x273C bitfield: bit0=bIsDying, bit2=bIsWounded
    constexpr uint64_t DyingFlags        = 0x273C;  // [SDK]
    constexpr uint8_t  DyingBit          = 0x01;
    constexpr uint8_t  WoundedBit        = 0x04;
    constexpr uint64_t Health            = 0x2740;  // [SDK]

    // Breath hold
    constexpr uint64_t BreathHoldStamina    = 0x27B0;  // [SDK]
    constexpr uint64_t BreathHoldStaminaMax = 0x27B4;  // [SDK]

    // Focus / zoom
    constexpr uint64_t FocusZoomAlpha       = 0x27C8;  // [SDK]
    constexpr uint64_t IsFocusing           = 0x27CC;  // [SDK]

    // ASQSoldier's own ControlRotation (double FRotator)
    constexpr uint64_t SoldierControlRotation = 0x2B18;  // [SDK]

    // ==================== UEngine / USQGameUserSettings ====================
    // UEngine::GameUserSettings. NOTE: GEngine pointer offset is NOT in the
    // Dumper-7 dump - settings chain currently disconnected, sens defaults
    // to 1.0 until GEngine is located (or read via GObjects iteration).
    constexpr uint64_t EngineGameUserSettings   = 0x02C8;  // [SDK]
    constexpr uint64_t SettingsGlobalSensitivity  = 0x0284;  // [SDK]
    constexpr uint64_t SettingsSteadyAimSensitivity = 0x0288;  // [SDK]
    constexpr uint64_t SettingsSoldierSensitivity = 0x02C8;  // [SDK]

    // ==================== Camera ====================
    // APlayerCameraManager::CameraCachePrivate (FCameraCacheEntry)
    constexpr uint64_t CameraCacheEntry  = 0x15B0;  // [RT]
    // FCameraCacheEntry -> FMinimalViewInfo POV
    // FMinimalViewInfo: Location(0x0), Rotation(0x18), FOV(0x30)
    constexpr uint64_t CachePOV          = 0x10;    // [RT]

    // ==================== Bone Indices (Bip01 skeleton, 141 bones) ====================
    // [RT] verified against v10.5.1 FMeshBoneInfo dump - unchanged from v9
    namespace bones
    {
        constexpr int Root              = 0;
        constexpr int Bip01             = 1;
        constexpr int Pelvis            = 2;
        constexpr int Spine             = 3;
        constexpr int Spine1            = 4;
        constexpr int Spine2            = 5;
        constexpr int Neck              = 6;
        constexpr int Head              = 7;
        constexpr int HeadNub           = 8;

        constexpr int R_Clavicle        = 65;
        constexpr int R_UpperArm        = 66;
        constexpr int R_Forearm         = 67;
        constexpr int R_Hand            = 68;

        constexpr int L_Clavicle        = 92;
        constexpr int L_UpperArm        = 93;
        constexpr int L_Forearm         = 94;
        constexpr int L_Hand            = 95;

        constexpr int L_Thigh           = 125;
        constexpr int L_Calf            = 126;
        constexpr int L_Foot            = 127;
        constexpr int L_Toe0            = 128;

        constexpr int R_Thigh           = 130;
        constexpr int R_Calf            = 131;
        constexpr int R_Foot            = 132;
        constexpr int R_Toe0            = 133;

        constexpr int CameraBone        = 121;
    }
}
