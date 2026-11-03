#pragma once

#include <cstdint>

namespace ext {

// ============================================================================
//  SIGNATURE TABLE
// ----------------------------------------------------------------------------
//  EVERY entry in this table is a PLACEHOLDER. None of these patterns are
//  guaranteed to exist in any shipping Fortnite build. They encode the SHAPE
//  of the instruction that has to be found, not the bytes of a live client.
//
//  Workflow:
//    1. Run `r6fn.exe --dump` against a live client.
//    2. For every MISS, open the module in a disassembler, find the instruction
//       that performs the access, and replace `pattern` with bytes that survive
//       the next patch (prefer operand-independent tails over raw call sites).
//    3. Rebuild. `--dump` must report OK for every sig before the feature loop
//       is meaningful.
//
//  Kinds:
//    RipSlot     - `mov reg,[rip+disp32]` / `lea reg,[rip+disp32]`.
//                  target = match + instr_len + disp32. For `mov` the 8 bytes at
//                  that target are the pointer slot.
//    DispOffset  - `mov reg,[reg+disp32]`. The disp32 IS the struct offset the
//                  game uses for that field. This is how an offset gets
//                  sig-derived instead of hardcoded: the scanner reads it back
//                  out of the instruction rather than trusting a number.
//    ByteImm     - `imul reg,reg,imm8`. The imm8 is the element stride the
//                  indexer uses (`sizeof(FTransform)`, `sizeof(AActor*)`).
//    Code        - a raw code address (function or block). Used for optional
//                  helpers such as a raycast.
// ============================================================================

enum class SigKind : std::uint8_t {
    RipSlot,
    DispOffset,
    ByteImm,
    Code,
};

struct SigDef {
    const char* name;
    const char* pattern;
    SigKind kind;
    std::uint8_t disp_offset;
    std::uint8_t instr_len;
    const char* resolves;
    bool placeholder;
};

// clang-format off
inline constexpr SigDef kSigs[] = {
    // ---- globals -----------------------------------------------------------
    { "uworld",              "48 8B 05 ? ? ? ? 48 85 C0 74 ? 48 8B 88 ? ? ? ? 48 85 C9",
      SigKind::RipSlot, 3, 7,
      "UWorld* pointer slot", true },

    { "gworld",              "48 8B 1D ? ? ? ? 48 85 DB 74 ? 48 8B 03 48 85 C0",
      SigKind::RipSlot, 3, 7,
      "GWorld fallback pointer slot", true },

    { "fname_pool",          "48 8B 0D ? ? ? ? E8 ? ? ? ? 48 8B 15 ? ? ? ? 48 85 D2",
      SigKind::RipSlot, 3, 7,
      "FNamePool* global (FName comparison-index resolution)", true },

    // ---- UWorld chain ------------------------------------------------------
    { "game_instance",       "48 8B 8B ? ? ? ? 48 85 C9 74 ? 48 8B 01 48 8B 40",
      SigKind::DispOffset, 3, 7,
      "UWorld::OwningGameInstance offset", true },

    { "local_players",       "48 8B 8B ? ? ? ? 48 8B 09 48 8B 01 48 85 C0 74",
      SigKind::DispOffset, 3, 7,
      "UGameInstance::LocalPlayers TArray offset", true },

    { "player_controller",   "48 8B 8B ? ? ? ? 48 85 C9 74 ? 48 8B 01 48 8B 80",
      SigKind::DispOffset, 3, 7,
      "ULocalPlayer::PlayerController offset", true },

    { "acknowledged_pawn",   "48 8B 8B ? ? ? ? 48 85 C9 74 ? 48 8B 01 FF 90",
      SigKind::DispOffset, 3, 7,
      "APlayerController::AcknowledgedPawn offset", true },

    { "player_state_pc",     "48 8B 8B ? ? ? ? 48 85 C9 74 ? 48 8B 01 FF 90 ? ? ? ? 48 8B D8",
      SigKind::DispOffset, 3, 7,
      "APlayerController::PlayerState offset", true },

    { "game_state",          "48 8B 8B ? ? ? ? 48 85 C9 74 ? 48 8B 01 48 8B 80 ? ? ? ? 48 85 C0",
      SigKind::DispOffset, 3, 7,
      "UWorld::GameState offset", true },

    { "player_array",        "48 8B 8B ? ? ? ? 48 8B 01 48 8B 40 ? 48 85 C0 74 ? 48 8B 48",
      SigKind::DispOffset, 3, 7,
      "AGameStateBase::PlayerArray TArray offset", true },

    { "level",               "48 8B 8B ? ? ? ? 48 85 C9 74 ? 48 8B 41 ? 48 85 C0 74",
      SigKind::DispOffset, 3, 7,
      "UWorld::PersistentLevel offset", true },

    { "actors",              "48 8B 8B ? ? ? ? 48 8B 49 ? 48 85 C9 74 ? 48 8B 01 48 8B 40",
      SigKind::DispOffset, 3, 7,
      "ULevel::Actors TArray offset (actor-walk fallback)", true },

    { "actors_stride",       "48 6B D2 ? 48 03 55 ? 48 8B 02 48 85 C0 74",
      SigKind::ByteImm, 3, 4,
      "TArray<AActor*> element stride (index scaling)", true },

    { "class_name",          "48 8B 81 ? ? ? ? 48 85 C0 74 ? 48 8B 48 ? 48 8B 01 48 8B 40",
      SigKind::DispOffset, 3, 7,
      "UObject::ClassPrivate offset (actor class filter)", true },

    // ---- player ------------------------------------------------------------
    { "root_component",      "48 8B 8B ? ? ? ? 48 85 C9 74 ? 48 8B 01 48 8B 40 ? 48 85 C0",
      SigKind::DispOffset, 3, 7,
      "AActor::RootComponent offset", true },

    { "relative_location",   "F3 0F 10 8B ? ? ? ? F3 0F 11 4D ? 48 8B 45",
      SigKind::DispOffset, 4, 8,
      "USceneComponent::RelativeLocation offset", true },

    { "mesh",                "48 8B 8B ? ? ? ? 48 85 C9 74 ? 48 8B 01 FF 90 ? ? ? ? 48 85 C0",
      SigKind::DispOffset, 3, 7,
      "ACharacter::Mesh (USkeletalMeshComponent*) offset", true },

    { "pawn_from_player_state", "48 8B 8B ? ? ? ? 48 85 C9 74 ? 48 8B 01 48 8B 80 ? ? ? ? 48 85 C0",
      SigKind::DispOffset, 3, 7,
      "APlayerState::PawnPrivate offset", true },

    { "team",                "0F B7 8B ? ? ? ? 66 85 C9 74 ? 0F BF C1 48 83 C4",
      SigKind::DispOffset, 3, 7,
      "APlayerState::TeamIndex offset", true },

    { "player_name",         "48 8B 8B ? ? ? ? 48 85 C9 74 ? 48 8B 01 48 8B 40 ? 48 8B 40",
      SigKind::DispOffset, 3, 7,
      "APlayerState::PlayerNamePrivate (FString) offset", true },

    { "health",              "F3 0F 10 8B ? ? ? ? 0F 2F C8 76 ? F3 0F 11 8B",
      SigKind::DispOffset, 4, 8,
      "pawn health float offset", true },

    { "max_health",          "F3 0F 10 83 ? ? ? ? F3 0F 5D C1 0F 2F C8",
      SigKind::DispOffset, 4, 8,
      "pawn max-health float offset", true },

    { "dead_flag",           "80 BB ? ? ? ? 00 0F 85 ? ? ? ? 48 8B 8B",
      SigKind::DispOffset, 2, 7,
      "pawn bIsDying / death flag offset", true },

    { "recently_rendered",   "80 BB ? ? ? ? 00 74 ? 48 8B 8B ? ? ? ? 48 85 C9",
      SigKind::DispOffset, 2, 7,
      "bRecentlyRendered offset (best-effort visibility)", true },

    // ---- bones -------------------------------------------------------------
    { "bone_array",          "48 8B 8B ? ? ? ? 48 8B 49 ? 48 8B 04 C8 48 85 C0",
      SigKind::DispOffset, 3, 7,
      "USkeletalMeshComponent::ComponentSpaceTransforms TArray offset", true },

    { "component_to_world",  "0F 10 83 ? ? ? ? 0F 11 45 ? 0F 10 8B ? ? ? ? 0F 11 4D",
      SigKind::DispOffset, 3, 7,
      "USceneComponent::ComponentToWorld (FTransform) offset", true },

    { "bone_stride",         "48 6B C0 ? 48 03 C8 48 8B 01 48 85 C0 75",
      SigKind::ByteImm, 3, 4,
      "sizeof(FTransform) as used by the bone TArray indexer", true },

    { "skeletal_asset",      "48 8B 8B ? ? ? ? 48 85 C9 74 ? 48 8B 01 48 8B 80 ? ? ? ? 48 85 C0",
      SigKind::DispOffset, 3, 7,
      "USkinnedMeshComponent::SkeletalMesh (USkeletalMesh*) offset", true },

    { "ref_skeleton",        "48 8B 8B ? ? ? ? 48 8B 49 ? 48 8B 41 ? 48 85 C0 74",
      SigKind::DispOffset, 3, 7,
      "USkeletalMesh::RefSkeleton offset", true },

    { "bone_info_array",     "48 8B 8B ? ? ? ? 48 8B 49 ? 48 85 C9 74 ? 8B 41 ? 85 C0",
      SigKind::DispOffset, 3, 7,
      "FReferenceSkeleton bone-info TArray offset", true },

    { "bone_info_stride",    "48 6B C9 ? 48 03 4D ? 48 8B 01 48 85 C0 74",
      SigKind::ByteImm, 3, 4,
      "sizeof(FMeshBoneInfo) as used by the skeleton indexer", true },

    // ---- camera ------------------------------------------------------------
    { "camera_manager",      "48 8B 8B ? ? ? ? 48 85 C9 74 ? 48 8B 01 FF 90 ? ? ? ? 48 85 C0",
      SigKind::DispOffset, 3, 7,
      "APlayerController::PlayerCameraManager offset", true },

    { "camera_cache",        "48 8B 89 ? ? ? ? 48 8D 91 ? ? ? ? 48 8B 02 48 89 01",
      SigKind::DispOffset, 3, 7,
      "APlayerCameraManager::CameraCache offset", true },

    { "camera_pov",          "F3 0F 10 81 ? ? ? ? F3 0F 11 45 ? F3 0F 10 89",
      SigKind::DispOffset, 4, 8,
      "FCameraCacheEntry::POV (FMinimalViewInfo) offset", true },

    // ---- weapon ------------------------------------------------------------
    { "current_weapon",      "48 8B 8B ? ? ? ? 48 85 C9 74 ? 48 8B 01 48 8B 40 ? 48 85 C0",
      SigKind::DispOffset, 3, 7,
      "AFortPawn::CurrentWeapon offset", true },

    { "recoil",              "F3 0F 10 8B ? ? ? ? F3 0F 59 C1 F3 0F 11 45 ? F3 0F 10 83",
      SigKind::DispOffset, 4, 8,
      "weapon recoil/spread multiplier float offset", true },

    // ---- optional helpers --------------------------------------------------
    { "visibility",          "48 89 5C 24 ? 57 48 83 EC ? 48 8B D9 E8 ? ? ? ? 48 8B F8 48 85 C0",
      SigKind::Code, 0, 0,
      "LineOfSightTo / raycast entry (optional, not called from an external)", true },
};
// clang-format on

} // namespace ext
