#pragma once
#include <cstdint>

// ============================================================
// Squad v9.0.2 - UE 5.5.4 Offsets
// SDK: 5.5.4-478836+__Squad_v9.0.2-SquadGame
// All vectors are double (UE5 LargeWorldCoordinates)
// ============================================================

namespace squad
{
    // ==================== Global Pointers ====================
    // From squadOffsets.txt - module base relative
    constexpr uint64_t GWorld   = 0x0CDCE4E8;
    constexpr uint64_t GObjects = 0x0CC450DC + 4;
    constexpr uint64_t GNames   = 0x0CB61580;

    // ==================== UWorld ====================
    // UWorld -> UGameInstance*
    constexpr uint64_t OwningGameInstance = 0x01E0;
    // UWorld -> ULevel* PersistentLevel
    constexpr uint64_t PersistentLevel   = 0x0030;
    // UWorld -> TArray<ULevel*> Levels
    constexpr uint64_t Levels            = 0x0178;
    // UWorld -> AGameStateBase*
    constexpr uint64_t GameState         = 0x0160;

    // ==================== AGameStateBase ====================
    // Latest SDK: AGameStateBase::PlayerArray
    constexpr uint64_t PlayerArray       = 0x02C8;

    // ==================== UGameInstance ====================
    constexpr uint64_t ULocalPlayers     = 0x0038;

    // ==================== ULocalPlayer ====================
    constexpr uint64_t PlayerController  = 0x0030;

    // ==================== APlayerController ====================
    // Latest SDK verified from merged_AudioMixer... header
    constexpr uint64_t AcknowledgedPawn    = 0x0370;
    constexpr uint64_t PlayerCameraManager = 0x0380;
    constexpr uint64_t PlayerInput         = 0x0440;

    // ==================== ULevel ====================
    // ULevel -> TArray<AActor*> Actors
    constexpr uint64_t ActorArray        = 0x00A0;

    // ==================== AActor ====================
    // AActor -> USceneComponent* RootComponent
    constexpr uint64_t RootComponent     = 0x01C0;
    // AActor -> APlayerState*
    constexpr uint64_t PlayerState       = 0x02D0;

    // ==================== USceneComponent ====================
    // USceneComponent -> FVector RelativeLocation
    constexpr uint64_t RelativeLocation  = 0x0170;
    // USceneComponent -> FTransform ComponentToWorld
    constexpr uint64_t ComponentToWorld  = 0x01E0;

    // ==================== USkeletalMeshComponent ====================
    // Offset from AActor to USkeletalMeshComponent* (Mesh/3P mesh)
    // ACharacter -> USkeletalMeshComponent* Mesh (inherited)
    constexpr uint64_t CharacterMesh     = 0x0330;
    // Bone array (TArray<FTransform>) inside USkeletalMeshComponent
    constexpr uint64_t BoneArray         = 0x05C8;

    // ==================== ASQSoldier (extends ACharacter) ====================
    // SDK: class ASQSoldier : public ACharacter @ 0x0668 start of custom fields

    // ==================== ASQPlayerState (extends APlayerState) ====================
    // SDK verified: TeamId at 0x0500 (line 40305 in tooLongPackage_0.h)
    constexpr uint64_t PS_TeamId         = 0x0500;  // SDK v9.0.2 VERIFIED
    // ASQPlayerState -> ASQSoldier* Soldier (line 40322)
    constexpr uint64_t PS_Soldier        = 0x07E8;  // SDK VERIFIED (was 0x07D0)
    // ASQPlayerState -> ASQSquadState* SquadState (line 40321)
    constexpr uint64_t PS_SquadState     = 0x07E0;  // SDK VERIFIED (was 0x07C8)
    // ASQPlayerState -> ASQTeamState* TeamState (line 40320)
    constexpr uint64_t PS_TeamState      = 0x07D8;  // SDK VERIFIED (was 0x07C0)

    // ==================== APlayerState (Engine base) ====================
    // APlayerState -> FString PlayerNamePrivate
    constexpr uint64_t PlayerNamePrivate = 0x0350;

    // ==================== ASQPlayerController ====================
    // ASQPlayerController -> ASQSoldier* LastSpawnedSoldier (SDK line 3318)
    constexpr uint64_t LastSpawnedSoldier = 0x08C8;  // SDK VERIFIED

    // ==================== ASQSoldier (extends ACharacter) ====================
    // SDK: class ASQSoldier : public ACharacter @ 0x0668 start of custom fields
    // ASQSoldier -> USkeletalMeshComponent* Mesh1P (line 10179)
    constexpr uint64_t Mesh1P            = 0x0760;  // SDK VERIFIED (was 0x0758)
    // ASQSoldier -> UCameraComponent* FirstPersonCameraComponent (line 10189)
    constexpr uint64_t FPCamera          = 0x07B0;  // SDK VERIFIED (was 0x07A8)
    // ASQSoldier -> USQSoldierMovement* SoldierMovement (line 10197)
    constexpr uint64_t SoldierMovement   = 0x07D0;  // SDK VERIFIED (was 0x07C8)
    
    // Weapon sway/punch (SDK lines 10211-10213)
    constexpr uint64_t WeaponPunchSway      = 0x1D60;  // SDK VERIFIED
    constexpr uint64_t WeaponPunchAlignment = 0x1D78;  // SDK VERIFIED
    constexpr uint64_t WeaponPunchLocation  = 0x1D90;  // SDK VERIFIED
    
    // Free aim (SDK lines 10290-10291)
    constexpr uint64_t FreeAimVerticalInput   = 0x1F9C;  // SDK VERIFIED
    constexpr uint64_t FreeAimHorizontalInput = 0x1FA0;  // SDK VERIFIED
    
    // Health/dying (SDK lines 10305-10310)
    constexpr uint64_t DyingFlags        = 0x26CC;  // SDK VERIFIED
    constexpr uint64_t Health            = 0x26D0;  // SDK VERIFIED
    
    // Breath hold (SDK lines 10332-10333)
    constexpr uint64_t BreathHoldStamina    = 0x2740;  // SDK VERIFIED
    constexpr uint64_t BreathHoldStaminaMax = 0x2744;  // SDK VERIFIED

    // Focus / zoom (SDK lines 10338-10339)
    constexpr uint64_t FocusZoomAlpha       = 0x2758;  // SDK VERIFIED
    constexpr uint64_t IsFocusing           = 0x275C;  // SDK VERIFIED

    // Equipped weapon references (SDK lines 10548-10550)
    constexpr uint64_t CurrentHeldItemWeak   = 0x30A4; // TWeakObjectPtr<ASQEquipableItem>
    constexpr uint64_t CurrentHeldWeaponWeak = 0x30AC; // TWeakObjectPtr<ASQWeapon>
    
    // Control rotation (SDK line 10435)
    constexpr uint64_t SoldierControlRotation = 0x2AA8;  // SDK VERIFIED - ASQSoldier has its own ControlRotation!

    // ==================== ASQWeapon ====================
    // SDK verified near line 9656 in tooLongPackage_0.h
    constexpr uint64_t WeaponADS          = 0x0834;

    // ==================== UEngine / USQGameUserSettings ====================
    // Latest SDK: UEngine::GameUserSettings at 0x02A0
    constexpr uint64_t EngineGameUserSettings = 0x02A0;
    // Latest SDK: USQGameInstance::GameUserSettings at 0x0CE8
    constexpr uint64_t GameInstanceGameUserSettings = 0x0CE8;
    // USQGameUserSettings fields
    constexpr uint64_t SettingsFOV                = 0x026C;
    constexpr uint64_t SettingsGlobalSensitivity  = 0x0274;
    constexpr uint64_t SettingsSteadyAimSensitivity = 0x0278;
    constexpr uint64_t SettingsSoldierSensitivity = 0x02B8;

    // ==================== Camera ====================
    // APlayerCameraManager cached POV
    // FMinimalViewInfo: Location(0x0), Rotation(0x18), FOV(0x30)
    // CameraCachePrivate offset in APlayerCameraManager
    // SDK dump: CameraCachePrivate at 0x1460 in APlayerCameraManager
    constexpr uint64_t CameraCacheEntry  = 0x1460;  // verified from SDK: APlayerCameraManager + 0x1460
    // Inside FCameraCacheEntry: FMinimalViewInfo POV at +0x10
    constexpr uint64_t CachePOV          = 0x10;

    // ==================== Bone Indices (Bip01 skeleton) ====================
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

        constexpr int R_Thigh           = 130;
        constexpr int R_Calf            = 131;
        constexpr int R_Foot            = 132;

        constexpr int CameraBone        = 121;
    }
}
